// P16 Step 4 -- the production wall-function differential, on the VALIDATED
// Re_tau = 590 channel.
//
// The four gates left open after the cavity differential all needed one thing:
// a case in which the wall function is genuinely ACTIVE and the CPU reference
// genuinely CONVERGES. The cavity cannot provide it -- with wall functions on
// it reaches NonFiniteState in 3 outer iterations on BOTH backends, because its
// corners put y+ far outside the law's valid band.
//
// So this reuses the repository's accepted high-Re wall-function configuration
// from tests/integration/turbulence_channel/test_wall_function_channel.cpp
// rather than inventing another one: same geometry, same Re_bulk, same inlet
// turbulence estimates, same relaxation and tolerances, same solver types.
//
// THE ONE THING THE EARLIER FAILED CHANNEL GOT WRONG, recorded because it cost
// a 400-second probe to find: it applied fixed_gradient pressure on EVERY
// patch. With no Dirichlet pressure anywhere the pressure equation has no
// reference, and the CPU reference diverged -- continuity 6.2e-2 -> 1.5e+9. The
// validated case fixes pressure at the OUTLET, which is why it converges.
#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "cfd/algebra/LinearSolver.hpp"
#include "cfd/boundary/FixedGradient.hpp"
#include "cfd/boundary/FixedValue.hpp"
#include "cfd/boundary/Inlet.hpp"
#include "cfd/boundary/Outlet.hpp"
#include "cfd/boundary/Wall.hpp"
#include "cfd/discretization/Gradient.hpp"
#include "cfd/fields/ScalarField.hpp"
#include "cfd/fields/SurfaceField.hpp"
#include "cfd/fields/VectorField.hpp"
#include "cfd/gpu/DeviceFaceFlux.hpp"
#include "cfd/gpu/DeviceKEpsilon.hpp"
#include "cfd/gpu/GPUBackend.hpp"
#include "cfd/gpu/GPUExecutionStats.hpp"
#include "cfd/gpu/GpuKEpsilonCorrection.hpp"
#include "cfd/gpu/GpuSimpleResidentTurbulence.hpp"
#include "cfd/gpu/TurbulenceBackendTrace.hpp"
#include "cfd/mesh/MeshGeometry.hpp"
#include "cfd/physics/FluidProperties.hpp"
#include "cfd/physics/MassFlux.hpp"
#include "cfd/pressure_velocity/SIMPLE.hpp"
#include "cfd/turbulence/KEpsilonModel.hpp"
#include "cfd/turbulence/LaminarModel.hpp"
#include "cfd/turbulence/WallTreatment.hpp"

#include "KEpsilonModelTestAccess.hpp"

using cfd::Index;
using cfd::Real;
using cfd::algebra::LinearSolverBackend;
using cfd::algebra::LinearSolverType;
using cfd::boundary::BoundaryConditionSet;
using cfd::fields::ScalarField;
using cfd::fields::VectorField;
using cfd::gpu::ExecutionBackend;
using cfd::gpu::TurbulenceOperation;
using cfd::mesh::Mesh;
using cfd::mesh::MeshGeometry;
using cfd::physics::FluidProperties;
using cfd::pressure_velocity::SIMPLE;
using cfd::pressure_velocity::SIMPLEResult;
using cfd::pressure_velocity::SIMPLESettings;
using cfd::pressure_velocity::SIMPLEStatus;
using cfd::turbulence::KEpsilonConfig;
using cfd::turbulence::KEpsilonModel;

namespace {

void skipIfNoDevice() {
  if (!cfd::gpu::cudaAvailable()) GTEST_SKIP() << "no usable CUDA device at runtime";
}

// --- the validated constants, copied from the accepted harness --------------
constexpr Real kChannelHeight = 2.0;
constexpr Real kChannelLength = 16.0;
constexpr Real kDensity = 1.0;
constexpr Real kBulkVelocity = 1.0;
constexpr Real kReBulk = 21725.0;  // Re_tau = 590
constexpr Real kTurbulenceIntensity = 0.05;
constexpr Real kCmu = 0.09;

// Smaller than the accepted sweep's nx = 64 so this runs as a unit test; ny
// stays inside the validity band the accepted case established
// (kMaximumWallFunctionNy = 19), which is what keeps the wall function inside
// the regime it was validated in.
constexpr Index kNx = 24;
constexpr Index kNy = 12;

Real dynamicViscosity() { return kDensity * kBulkVelocity * kChannelHeight / kReBulk; }
Real inletK() {
  const Real fluct = kTurbulenceIntensity * kBulkVelocity;
  return 1.5 * fluct * fluct;
}
Real inletEpsilon() {
  const Real mixingLength = 0.07 * kChannelHeight;
  return std::pow(kCmu, 0.75) * std::pow(inletK(), 1.5) / mixingLength;
}

struct ChannelCase {
  Mesh mesh;
  FluidProperties fluid;
  BoundaryConditionSet velocityBoundaries;
  BoundaryConditionSet pressureBoundaries;
  BoundaryConditionSet kBoundaries;
  BoundaryConditionSet epsilonBoundaries;

  // `ny` is parameterised for ONE purpose: the residency ledger needs a second
  // mesh with the SAME nx -- hence the same wall-face count -- and a different
  // cell and face count, so that a transfer which is O(N) can be told apart
  // from one which is O(N_wall). Every existing caller default-constructs and
  // is unaffected.
  explicit ChannelCase(Index ny = kNy)
      : mesh(MeshGeometry::createCartesian2D(kNx, ny, kChannelLength, kChannelHeight)),
        fluid(kDensity, dynamicViscosity()) {
    velocityBoundaries.set(
        mesh, "left", std::make_unique<cfd::boundary::Inlet>(cfd::Vector2{kBulkVelocity, 0.0}));
    velocityBoundaries.set(mesh, "right", std::make_unique<cfd::boundary::Outlet>());
    velocityBoundaries.set(mesh, "bottom", std::make_unique<cfd::boundary::Wall>());
    velocityBoundaries.set(mesh, "top", std::make_unique<cfd::boundary::Wall>());

    // THE OUTLET FIXES PRESSURE. Without this the pressure equation has no
    // Dirichlet reference and the solve diverges -- measured, see the header.
    pressureBoundaries.set(mesh, "right", std::make_unique<cfd::boundary::FixedValue>(0.0));
    for (const char* patch : {"left", "bottom", "top"}) {
      pressureBoundaries.set(mesh, patch, std::make_unique<cfd::boundary::FixedGradient>(0.0));
    }

    kBoundaries.set(mesh, "left", std::make_unique<cfd::boundary::FixedValue>(inletK()));
    epsilonBoundaries.set(mesh, "left",
                          std::make_unique<cfd::boundary::FixedValue>(inletEpsilon()));
    for (const char* patch : {"right", "bottom", "top"}) {
      kBoundaries.set(mesh, patch, std::make_unique<cfd::boundary::FixedGradient>(0.0));
      epsilonBoundaries.set(mesh, patch, std::make_unique<cfd::boundary::FixedGradient>(0.0));
    }
  }
};

KEpsilonConfig turbulenceConfig(LinearSolverBackend backend, bool wallFunctions = true) {
  KEpsilonConfig c;
  c.initialK = inletK();
  c.initialEpsilon = inletEpsilon();
  c.wallFunctions = wallFunctions;  // THE POINT of this case
  c.kSolver.backend = backend;
  c.kSolver.type = LinearSolverType::BiCGSTAB;
  c.epsilonSolver = c.kSolver;
  return c;
}

// The accepted harness's solver.json, expressed in C++.
// `strictOuter` tightens ONLY the outer stopping criterion, for the
// differential. The validated case stops at velocityTolerance 2e-5 and
// pressureTolerance 5e-4, so two arms that both satisfy it are pinned to each
// other only at ~2e-5 -- measured: cpu 52 iters, gpu 50 iters, U differing by
// 1.8e-5 and p by 5.2e-4, i.e. each right at its own stopping tolerance.
// Comparing that against a 1e-6 field bound is asking a question finer than
// the criterion that ended the solve, which is an instrument mismatch, not a
// defect.
//
// The repository's own CPU/GPU equivalence test
// (tests/solver/simple/test_simple_gpu_solver.cpp) pairs its 1e-6 field bound
// with 1e-6 OUTER tolerances for exactly this reason. Those already-qualified
// strict settings are adopted here for the differential only. Production
// defaults are untouched, the physics, geometry, relaxation and linear-solver
// settings are the validated ones, and the FIELD BOUND IS NOT LOOSENED.
// `strictKrylov` tightens the LINEAR solver stopping criteria, for the
// same-state diagnostic only. Production defaults are untouched: this is the
// already-approved strict equivalence-test setting the turbulence differentials
// use (1e-14 / 1e-16), applied to the pressure and momentum solves so a
// stopping-point difference cannot masquerade as an implementation difference.
//
// The FIELD bound is not involved and is not changed.
SIMPLESettings settingsFor(LinearSolverBackend backend, Index outerIterations,
                           bool strictOuter = false, bool strictKrylov = false) {
  SIMPLESettings settings;
  settings.maxIterations = outerIterations;
  settings.velocityRelaxation = 0.5;
  settings.pressureRelaxation = 0.3;
  settings.velocityTolerance = strictOuter ? 1e-9 : 2e-5;
  settings.pressureTolerance = strictOuter ? 1e-9 : 5e-4;
  settings.continuityTolerance = strictOuter ? 1e-9 : 1e-6;
  settings.momentumSolver.type = LinearSolverType::BiCGSTAB;
  settings.momentumSolver.backend = backend;
  settings.momentumSolver.absoluteTolerance = strictKrylov ? 1e-16 : 1e-10;
  settings.momentumSolver.relativeTolerance = strictKrylov ? 1e-14 : 1e-8;
  settings.momentumSolver.maxIterations = strictKrylov ? 5000 : 500;
  settings.pressureSolver.type = LinearSolverType::BiCGSTAB;
  settings.pressureSolver.backend = backend;
  settings.pressureSolver.absoluteTolerance = strictKrylov ? 1e-16 : 1e-9;
  settings.pressureSolver.relativeTolerance = strictKrylov ? 1e-14 : 1e-7;
  settings.pressureSolver.maxIterations = strictKrylov ? 20000 : 2000;
  settings.enableGpuDiscretization = backend == LinearSolverBackend::GPU;
  return settings;
}

struct Outcome {
  SIMPLEResult result;
  ScalarField k, epsilon, muT, yPlus, wallShear;
  cfd::fields::SurfaceField wallViscosity;
  Index wallFacesOutsideBand{};
  Real minimumYPlus{}, maximumYPlus{};
};

Outcome run(ChannelCase& c, LinearSolverBackend backend, Index outerIterations,
            bool strictOuter = false, bool wallFunctions = true, bool strictKrylov = false) {
  const Index n = c.mesh.numberOfCells();
  KEpsilonModel model(c.mesh, c.fluid, c.velocityBoundaries, c.kBoundaries, c.epsilonBoundaries,
                      turbulenceConfig(backend, wallFunctions));
  VectorField velocity(n, cfd::Vector2{kBulkVelocity, 0.0});
  ScalarField pressure(n, 0.0);

  cfd::gpu::turbulenceBackendTrace().reset();
  cfd::gpu::resetGpuExecutionStats();
  const SIMPLE simple(settingsFor(backend, outerIterations, strictOuter, strictKrylov), 0, &model);
  Outcome out;
  out.result =
      simple.solve(c.mesh, c.fluid, c.velocityBoundaries, c.pressureBoundaries, velocity, pressure);
  // From the RESULT. solve() takes the initial fields by value.
  out.k = model.k();
  out.epsilon = model.epsilon();
  out.muT = model.turbulentViscosity();
  out.yPlus = model.yPlus();
  out.wallShear = model.wallShearStress();
  if (model.wallViscosity() != nullptr) out.wallViscosity = *model.wallViscosity();
  return out;
}

// Same measure for the face-length wall viscosity. Separate overload rather
// than a template so the two field types stay distinguishable at the call site.
Real relativeL2(const cfd::fields::SurfaceField& reference,
                const cfd::fields::SurfaceField& candidate) {
  Real num = 0.0;
  Real den = 0.0;
  for (Index f = 0; f < reference.size(); ++f) {
    const Real d = candidate[f] - reference[f];
    num += d * d;
    den += reference[f] * reference[f];
  }
  return den > 0.0 ? std::sqrt(num) / std::sqrt(den) : std::sqrt(num);
}

Real relativeL2(const ScalarField& reference, const ScalarField& candidate) {
  Real num = 0.0;
  Real den = 0.0;
  for (Index i = 0; i < reference.size(); ++i) {
    const Real d = candidate[i] - reference[i];
    num += d * d;
    den += reference[i] * reference[i];
  }
  return den > 0.0 ? std::sqrt(num) / std::sqrt(den) : std::sqrt(num);
}

}  // namespace

// =============================================================================
// The CPU baseline must be valid and non-trivial BEFORE anything is compared.
// =============================================================================

TEST(ResidentChannelProduction, CpuReferenceIsConvergedAndNonTrivial) {
  ChannelCase c;
  const Outcome cpu = run(c, LinearSolverBackend::CPU, 4000);

  EXPECT_EQ(cpu.result.status, SIMPLEStatus::Converged)
      << "the CPU reference did not converge; a differential against it would measure nothing";

  const Index n = c.mesh.numberOfCells();
  Real maxU = 0.0;
  Real maxK = 0.0;
  Real maxMuT = 0.0;
  for (Index i = 0; i < n; ++i) {
    maxU = std::max(maxU, std::abs(cpu.result.velocity[i].x));
    maxK = std::max(maxK, cpu.k[i]);
    maxMuT = std::max(maxMuT, cpu.muT[i]);
  }
  // Non-trivial: a solved flow, a live turbulence state, and a wall function
  // that actually produced something.
  EXPECT_GT(maxU, 0.5 * kBulkVelocity) << "velocity solution is trivial";
  EXPECT_GT(maxK, 0.0) << "turbulence state is dead";
  EXPECT_GT(maxMuT, 0.0) << "mu_t is identically zero";

  Index nonZeroWallViscosity = 0;
  Real maxYPlus = 0.0;
  for (Index f = 0; f < cpu.wallViscosity.size(); ++f) {
    if (cpu.wallViscosity[f] != 0.0) ++nonZeroWallViscosity;
  }
  for (Index i = 0; i < n; ++i) maxYPlus = std::max(maxYPlus, cpu.yPlus[i]);

  // THE WALL FUNCTION IS GENUINELY ACTIVE. This is the gate the cavity could
  // not satisfy, and without it the wall-viscosity chain below is untestable.
  EXPECT_GT(nonZeroWallViscosity, 0)
      << "no wall face carries a viscosity override, so the wall function is not active and this "
         "case cannot qualify the wall path";
  EXPECT_GT(maxYPlus, 0.0) << "y+ is identically zero";
  EXPECT_TRUE(std::isfinite(maxYPlus));

  std::printf(
      "\n  CPU channel baseline: status=%d iters=%lld  maxU=%.4f  maxK=%.4e  maxMuT=%.4e\n"
      "    wall faces with override: %lld   max y+ = %.2f   continuity=%.3e  mass=%.3e\n",
      static_cast<int>(cpu.result.status), static_cast<long long>(cpu.result.iterations), maxU,
      maxK, maxMuT, static_cast<long long>(nonZeroWallViscosity), maxYPlus,
      cpu.result.finalContinuityResidual, cpu.result.globalMassImbalance);
}

// =============================================================================
// Production provenance -- the wall-function path genuinely executed on GPU.
// =============================================================================

TEST(ResidentChannelProduction, ProvenanceShowsGpuTurbulenceWithCpuWallTreatment) {
  skipIfNoDevice();
  ChannelCase c;
  const Outcome gpu = run(c, LinearSolverBackend::GPU, 200);
  ASSERT_TRUE(gpu.result.residentSimpleLoop)
      << "the resident loop did not engage, so this provenance would describe the CPU path";

  const auto& trace = cfd::gpu::turbulenceBackendTrace();
  const TurbulenceOperation gpuOps[] = {TurbulenceOperation::StrainRate,
                                        TurbulenceOperation::Production,
                                        TurbulenceOperation::TurbulentViscosity,
                                        TurbulenceOperation::KAssembly,
                                        TurbulenceOperation::KSolve,
                                        TurbulenceOperation::EpsilonAssembly,
                                        TurbulenceOperation::EpsilonSolve};
  for (const auto op : gpuOps) {
    EXPECT_EQ(trace.backendFor(op), ExecutionBackend::Gpu)
        << cfd::gpu::turbulenceOperationName(op) << " did not run on the device\n"
        << trace.describe();
  }
  EXPECT_EQ(trace.backendFor(TurbulenceOperation::WallTreatment), ExecutionBackend::Cpu)
      << "the wall treatment is intentionally host-side for P16\n"
      << trace.describe();

  EXPECT_TRUE(cfd::gpu::turbulenceGpuPathQualified(trace)) << trace.describe();
  const TurbulenceOperation everything[] = {TurbulenceOperation::StrainRate,
                                            TurbulenceOperation::Production,
                                            TurbulenceOperation::TurbulentViscosity,
                                            TurbulenceOperation::KAssembly,
                                            TurbulenceOperation::KSolve,
                                            TurbulenceOperation::EpsilonAssembly,
                                            TurbulenceOperation::EpsilonSolve,
                                            TurbulenceOperation::WallTreatment};
  EXPECT_FALSE(trace.allOnGpu(everything, 8))
      << "allOnGpu must stay false -- the wall law is deliberately on the host, and a true here "
         "would mean its meaning had been changed rather than the code";
}

// TEMPORARY PROBE: where the outer residuals plateau, so the differential's
// stopping criterion can be chosen from measurement rather than guessed.
TEST(ResidentChannelProduction, DISABLED_OuterResidualPlateauProbe) {
  ChannelCase c;
  const Outcome cpu = run(c, LinearSolverBackend::CPU, 600, /*strictOuter=*/true);
  std::printf(
      "\n  CPU, outer tol 1e-9, budget 600: status=%d iters=%lld\n"
      "    finalUResidual=%.3e  finalPressureResidual=%.3e  continuity=%.3e\n",
      static_cast<int>(cpu.result.status), static_cast<long long>(cpu.result.iterations),
      cpu.result.finalUResidual, cpu.result.finalPressureResidual,
      cpu.result.finalContinuityResidual);
}

// =============================================================================
// CPU/GPU differential on the validated case, wall functions active.
// =============================================================================

TEST(ResidentChannelProduction, GpuChannelMatchesTheCpuProductionSolve) {
  skipIfNoDevice();
  ChannelCase cpuCase;
  ChannelCase gpuCase;
  // BOTH ARMS RUN TO THE SAME FIXED POINT, rather than each to its own
  // stopping tolerance.
  //
  // Measured: this case's outer PRESSURE residual plateaus at ~1.1e-5 and
  // cannot be driven to 1e-6, so a tolerance-based stop always leaves the two
  // arms at different points of the trajectory -- at the validated tolerances
  // they stopped at 52 and 50 iterations and differed by exactly their own
  // stopping tolerances (U 1.8e-5, p 5.2e-4). Comparing that against a 1e-6
  // field bound asks a finer question than the criterion that ended the solve.
  //
  // So the tolerance is set unreachably tight and the budget fixed: both arms
  // take the SAME number of outer iterations, well past the point where
  // continuity has reached 1e-11, and what remains is the genuine CPU/GPU
  // difference at a common state rather than the gap between two stopping
  // points. The FIELD BOUND IS UNCHANGED.
  constexpr Index kFixedOuter = 600;
  const Outcome cpu = run(cpuCase, LinearSolverBackend::CPU, kFixedOuter, /*strictOuter=*/true);
  const Outcome gpu = run(gpuCase, LinearSolverBackend::GPU, kFixedOuter, /*strictOuter=*/true);

  ASSERT_EQ(cpu.result.iterations, kFixedOuter)
      << "the CPU arm stopped early, so the two arms are not at a common state";
  ASSERT_EQ(gpu.result.iterations, kFixedOuter) << "the GPU arm stopped early";
  // The common state must itself be a converged-quality one, or "the same
  // point" would be the same transient.
  ASSERT_LT(cpu.result.finalContinuityResidual, 1e-9)
      << "the shared stopping point is not a converged state";
  ASSERT_TRUE(gpu.result.residentSimpleLoop) << "GPU arm did not take the resident loop";

  const Index n = cpuCase.mesh.numberOfCells();
  Real uNum = 0.0;
  Real uDen = 0.0;
  for (Index i = 0; i < n; ++i) {
    const Real du = gpu.result.velocity[i].x - cpu.result.velocity[i].x;
    const Real dv = gpu.result.velocity[i].y - cpu.result.velocity[i].y;
    uNum += du * du + dv * dv;
    uDen += cpu.result.velocity[i].x * cpu.result.velocity[i].x +
            cpu.result.velocity[i].y * cpu.result.velocity[i].y;
  }
  ASSERT_GT(uDen, 0.0) << "the CPU velocity is identically zero";
  const Real uError = std::sqrt(uNum) / std::sqrt(uDen);

  Real fluxNum = 0.0;
  Real fluxDen = 0.0;
  for (Index f = 0; f < cpuCase.mesh.numberOfFaces(); ++f) {
    const Real d = gpu.result.massFlux[f] - cpu.result.massFlux[f];
    fluxNum += d * d;
    fluxDen += cpu.result.massFlux[f] * cpu.result.massFlux[f];
  }
  ASSERT_GT(fluxDen, 0.0);
  const Real fluxError = std::sqrt(fluxNum) / std::sqrt(fluxDen);

  Real wallViscNum = 0.0;
  Real wallViscDen = 0.0;
  for (Index f = 0; f < cpu.wallViscosity.size(); ++f) {
    const Real d = gpu.wallViscosity[f] - cpu.wallViscosity[f];
    wallViscNum += d * d;
    wallViscDen += cpu.wallViscosity[f] * cpu.wallViscosity[f];
  }
  ASSERT_GT(wallViscDen, 0.0) << "the CPU wall viscosity is identically zero -- the wall function "
                                 "is not active and this differential would not cover it";
  const Real wallViscError = std::sqrt(wallViscNum) / std::sqrt(wallViscDen);

  std::printf(
      "\n  CPU/GPU VALIDATED CHANNEL differential (Re_tau 590, wall functions ON)\n"
      "    %lld cells, cpu %lld iters, gpu %lld iters\n"
      "    U %.3e  p %.3e  flux %.3e\n"
      "    k %.3e  eps %.3e  mu_t %.3e\n"
      "    y+ %.3e  tau_w %.3e  mu_wall %.3e\n"
      "    continuity cpu %.3e gpu %.3e   mass cpu %.3e gpu %.3e\n",
      static_cast<long long>(n), static_cast<long long>(cpu.result.iterations),
      static_cast<long long>(gpu.result.iterations), uError,
      relativeL2(cpu.result.pressure, gpu.result.pressure), fluxError, relativeL2(cpu.k, gpu.k),
      relativeL2(cpu.epsilon, gpu.epsilon), relativeL2(cpu.muT, gpu.muT),
      relativeL2(cpu.yPlus, gpu.yPlus), relativeL2(cpu.wallShear, gpu.wallShear), wallViscError,
      cpu.result.finalContinuityResidual, gpu.result.finalContinuityResidual,
      cpu.result.globalMassImbalance, gpu.result.globalMassImbalance);

  // DIAGNOSTIC: is the pressure difference a uniform LEVEL offset, or a real
  // field difference? An incompressible pressure is determined only up to its
  // reference, and a level shift moves no physics -- velocity, flux and every
  // wall quantity would be unaffected, which is what the numbers above suggest.
  Real cpuMean = 0.0;
  Real gpuMean = 0.0;
  for (Index i = 0; i < n; ++i) {
    cpuMean += cpu.result.pressure[i];
    gpuMean += gpu.result.pressure[i];
  }
  cpuMean /= static_cast<Real>(n);
  gpuMean /= static_cast<Real>(n);
  Real centredNum = 0.0;
  Real centredDen = 0.0;
  for (Index i = 0; i < n; ++i) {
    const Real d = (gpu.result.pressure[i] - gpuMean) - (cpu.result.pressure[i] - cpuMean);
    centredNum += d * d;
    centredDen += (cpu.result.pressure[i] - cpuMean) * (cpu.result.pressure[i] - cpuMean);
  }
  std::printf(
      "    pressure level: cpu mean %.6e  gpu mean %.6e  offset %.3e\n"
      "    pressure with the level removed: %.3e\n",
      cpuMean, gpuMean, gpuMean - cpuMean,
      centredDen > 0.0 ? std::sqrt(centredNum) / std::sqrt(centredDen) : std::sqrt(centredNum));

  EXPECT_EQ(static_cast<int>(cpu.result.status), static_cast<int>(gpu.result.status));
  EXPECT_LT(gpu.result.finalContinuityResidual, 1e-9);

  constexpr Real kBound = 1e-6;
  EXPECT_LE(uError, kBound) << "velocity";
  EXPECT_LE(relativeL2(cpu.result.pressure, gpu.result.pressure), kBound) << "pressure";
  EXPECT_LE(fluxError, kBound) << "face mass flux";
  EXPECT_LE(relativeL2(cpu.k, gpu.k), kBound) << "k";
  EXPECT_LE(relativeL2(cpu.epsilon, gpu.epsilon), kBound) << "epsilon";
  EXPECT_LE(relativeL2(cpu.muT, gpu.muT), kBound) << "mu_t";
  EXPECT_LE(relativeL2(cpu.yPlus, gpu.yPlus), kBound) << "y+";
  EXPECT_LE(relativeL2(cpu.wallShear, gpu.wallShear), kBound) << "wall shear";
  EXPECT_LE(wallViscError, kBound) << "wall viscosity";
}

// =============================================================================
// The pressure question -- measured before any criterion is touched.
// =============================================================================
//
// The absolute pressure field misses the 1e-6 bound by 106x while U misses it
// by nothing (2.3e-07) and the face flux by three orders (5.8e-09). Before
// concluding anything, this measures the quantity momentum actually consumes --
// the pressure GRADIENT, through the production operator at
// MomentumEquation.cpp:436, not a comparison-only derivative -- and localizes
// the absolute-pressure difference so its structure is a finding rather than a
// guess.
//
// Nothing here changes an acceptance criterion. It reports.
TEST(ResidentChannelProduction, PressureGradientAndLocalizationDiagnostic) {
  skipIfNoDevice();
  constexpr Index kFixedOuter = 600;
  ChannelCase cpuCase;
  ChannelCase gpuCase;
  const Outcome cpu = run(cpuCase, LinearSolverBackend::CPU, kFixedOuter, true);
  const Outcome gpu = run(gpuCase, LinearSolverBackend::GPU, kFixedOuter, true);
  ASSERT_EQ(cpu.result.iterations, kFixedOuter);
  ASSERT_EQ(gpu.result.iterations, kFixedOuter);

  const Index n = cpuCase.mesh.numberOfCells();

  // THE PRODUCTION OPERATOR, the one MomentumEquation.cpp:436 calls.
  const auto cpuGrad =
      cfd::discretization::gradient(cpuCase.mesh, cpu.result.pressure, cpuCase.pressureBoundaries,
                                    cfd::discretization::GradientScheme::GreenGauss);
  const auto gpuGrad =
      cfd::discretization::gradient(gpuCase.mesh, gpu.result.pressure, gpuCase.pressureBoundaries,
                                    cfd::discretization::GradientScheme::GreenGauss);

  Real gNum = 0.0, gDen = 0.0, gxNum = 0.0, gxDen = 0.0, gyNum = 0.0, gyDen = 0.0;
  for (Index i = 0; i < n; ++i) {
    const Real dx = gpuGrad[i].x - cpuGrad[i].x;
    const Real dy = gpuGrad[i].y - cpuGrad[i].y;
    gNum += dx * dx + dy * dy;
    gDen += cpuGrad[i].x * cpuGrad[i].x + cpuGrad[i].y * cpuGrad[i].y;
    gxNum += dx * dx;
    gxDen += cpuGrad[i].x * cpuGrad[i].x;
    gyNum += dy * dy;
    gyDen += cpuGrad[i].y * cpuGrad[i].y;
  }
  const Real gradError = gDen > 0.0 ? std::sqrt(gNum) / std::sqrt(gDen) : std::sqrt(gNum);
  const Real dpdxError = gxDen > 0.0 ? std::sqrt(gxNum) / std::sqrt(gxDen) : std::sqrt(gxNum);
  const Real dpdyError = gyDen > 0.0 ? std::sqrt(gyNum) / std::sqrt(gyDen) : std::sqrt(gyNum);

  // --- localization of the ABSOLUTE pressure difference --------------------
  Real maxAbs = 0.0;
  Index maxAt = 0;
  Real wallNum = 0.0, wallDen = 0.0, interiorNum = 0.0, interiorDen = 0.0;
  for (Index i = 0; i < n; ++i) {
    const Real d = std::abs(gpu.result.pressure[i] - cpu.result.pressure[i]);
    if (d > maxAbs) {
      maxAbs = d;
      maxAt = i;
    }
    const Index row = i / kNx;
    const bool nearWall = row == 0 || row == kNy - 1;
    if (nearWall) {
      wallNum += d * d;
      wallDen += cpu.result.pressure[i] * cpu.result.pressure[i];
    } else {
      interiorNum += d * d;
      interiorDen += cpu.result.pressure[i] * cpu.result.pressure[i];
    }
  }
  const Real wallError = wallDen > 0.0 ? std::sqrt(wallNum) / std::sqrt(wallDen) : 0.0;
  const Real interiorError =
      interiorDen > 0.0 ? std::sqrt(interiorNum) / std::sqrt(interiorDen) : 0.0;

  // --- is the difference smooth, or cell-to-cell alternating? --------------
  // A checkerboard difference reverses sign between neighbours, so the mean
  // absolute neighbour-to-neighbour CHANGE is about twice the mean magnitude.
  // A smooth drift gives a ratio near zero.
  Real meanMag = 0.0, meanJump = 0.0;
  Index jumps = 0;
  for (Index row = 0; row < kNy; ++row) {
    for (Index col = 0; col + 1 < kNx; ++col) {
      const Index a = row * kNx + col;
      const Index b = a + 1;
      const Real da = gpu.result.pressure[a] - cpu.result.pressure[a];
      const Real db = gpu.result.pressure[b] - cpu.result.pressure[b];
      meanMag += std::abs(da);
      meanJump += std::abs(db - da);
      ++jumps;
    }
  }
  meanMag /= static_cast<Real>(jumps);
  meanJump /= static_cast<Real>(jumps);

  // --- streamwise pressure drop, reference-invariant -----------------------
  const auto columnMean = [&](const ScalarField& f, Index col) {
    Real sum = 0.0;
    for (Index row = 0; row < kNy; ++row) sum += f[row * kNx + col];
    return sum / static_cast<Real>(kNy);
  };
  const Real cpuDrop =
      columnMean(cpu.result.pressure, 1) - columnMean(cpu.result.pressure, kNx - 2);
  const Real gpuDrop =
      columnMean(gpu.result.pressure, 1) - columnMean(gpu.result.pressure, kNx - 2);
  const Real dropError = std::abs(cpuDrop) > 0.0 ? std::abs(gpuDrop - cpuDrop) / std::abs(cpuDrop)
                                                 : std::abs(gpuDrop - cpuDrop);

  std::printf(
      "\n  PRESSURE DIAGNOSTIC (production gradient operator), %lld cells, %lld iters\n"
      "    grad(p) relL2        %.3e\n"
      "    dp/dx  relL2         %.3e\n"
      "    dp/dy  relL2         %.3e\n"
      "    channel drop  cpu %.6e  gpu %.6e   rel %.3e\n"
      "    absolute p relL2     %.3e\n"
      "      max |dp| %.3e at cell %lld (row %lld, col %lld)\n"
      "      wall-row relL2 %.3e   interior relL2 %.3e\n"
      "      mean |dp| %.3e   mean neighbour jump %.3e   jump/mag %.2f\n",
      static_cast<long long>(n), static_cast<long long>(kFixedOuter), gradError, dpdxError,
      dpdyError, cpuDrop, gpuDrop, dropError, relativeL2(cpu.result.pressure, gpu.result.pressure),
      maxAbs, static_cast<long long>(maxAt), static_cast<long long>(maxAt / kNx),
      static_cast<long long>(maxAt % kNx), wallError, interiorError, meanMag, meanJump,
      meanMag > 0.0 ? meanJump / meanMag : 0.0);

  // Reported, not gated: the decision about which quantity is the primary
  // momentum-relevant equivalence gate is not this test's to make.
  EXPECT_TRUE(std::isfinite(gradError));
}

// ATTRIBUTION: is the checkerboard pressure difference caused by the P16
// turbulence work, or does it exist in the pressure/momentum coupling already?
//
// The same channel, same solver settings, same fixed budget -- with a LAMINAR
// model, so none of the resident turbulence path runs. If the checkerboard is
// present here too, it predates every P16 change and belongs to the
// pressure-correction/Rhie-Chow path, not to k-epsilon.
TEST(ResidentChannelProduction, LaminarChannelPressureAttribution) {
  skipIfNoDevice();
  constexpr Index kFixedOuter = 600;

  const auto runLaminar = [&](LinearSolverBackend backend, ChannelCase& c) {
    const Index n = c.mesh.numberOfCells();
    cfd::turbulence::LaminarModel laminar(c.mesh);
    VectorField velocity(n, cfd::Vector2{kBulkVelocity, 0.0});
    ScalarField pressure(n, 0.0);
    const SIMPLE simple(settingsFor(backend, kFixedOuter, /*strictOuter=*/true), 0, &laminar);
    return simple.solve(c.mesh, c.fluid, c.velocityBoundaries, c.pressureBoundaries, velocity,
                        pressure);
  };
  ChannelCase cpuCase;
  ChannelCase gpuCase;
  const auto cpu = runLaminar(LinearSolverBackend::CPU, cpuCase);
  const auto gpu = runLaminar(LinearSolverBackend::GPU, gpuCase);
  ASSERT_EQ(cpu.iterations, kFixedOuter);
  ASSERT_EQ(gpu.iterations, kFixedOuter);
  ASSERT_TRUE(gpu.residentSimpleLoop) << "the laminar GPU arm did not take the resident loop";

  const Index n = cpuCase.mesh.numberOfCells();
  const auto cpuGrad =
      cfd::discretization::gradient(cpuCase.mesh, cpu.pressure, cpuCase.pressureBoundaries,
                                    cfd::discretization::GradientScheme::GreenGauss);
  const auto gpuGrad =
      cfd::discretization::gradient(gpuCase.mesh, gpu.pressure, gpuCase.pressureBoundaries,
                                    cfd::discretization::GradientScheme::GreenGauss);
  Real gNum = 0.0, gDen = 0.0;
  for (Index i = 0; i < n; ++i) {
    const Real dx = gpuGrad[i].x - cpuGrad[i].x;
    const Real dy = gpuGrad[i].y - cpuGrad[i].y;
    gNum += dx * dx + dy * dy;
    gDen += cpuGrad[i].x * cpuGrad[i].x + cpuGrad[i].y * cpuGrad[i].y;
  }
  Real meanMag = 0.0, meanJump = 0.0;
  Index jumps = 0;
  for (Index row = 0; row < kNy; ++row) {
    for (Index col = 0; col + 1 < kNx; ++col) {
      const Index a = row * kNx + col;
      const Real da = gpu.pressure[a] - cpu.pressure[a];
      const Real db = gpu.pressure[a + 1] - cpu.pressure[a + 1];
      meanMag += std::abs(da);
      meanJump += std::abs(db - da);
      ++jumps;
    }
  }
  meanMag /= static_cast<Real>(jumps);
  meanJump /= static_cast<Real>(jumps);

  Real uNum = 0.0, uDen = 0.0;
  for (Index i = 0; i < n; ++i) {
    const Real du = gpu.velocity[i].x - cpu.velocity[i].x;
    uNum += du * du;
    uDen += cpu.velocity[i].x * cpu.velocity[i].x;
  }

  std::printf(
      "\n  LAMINAR ATTRIBUTION (no turbulence path runs at all)\n"
      "    absolute p relL2 %.3e   grad(p) relL2 %.3e   U relL2 %.3e\n"
      "    mean |dp| %.3e   neighbour jump %.3e   jump/mag %.2f\n",
      relativeL2(cpu.pressure, gpu.pressure),
      gDen > 0.0 ? std::sqrt(gNum) / std::sqrt(gDen) : std::sqrt(gNum),
      uDen > 0.0 ? std::sqrt(uNum) / std::sqrt(uDen) : std::sqrt(uNum), meanMag, meanJump,
      meanMag > 0.0 ? meanJump / meanMag : 0.0);
  EXPECT_TRUE(std::isfinite(meanJump));
}

// NARROWING: turbulence feeds momentum through exactly two things -- the cell
// effective viscosity, and the sparse wall-face viscosity override. Running
// k-epsilon with wall functions OFF keeps the first and removes the second.
TEST(ResidentChannelProduction, TurbulentChannelWithoutWallFunctionsAttribution) {
  skipIfNoDevice();
  constexpr Index kFixedOuter = 600;
  ChannelCase cpuCase;
  ChannelCase gpuCase;
  const Outcome cpu =
      run(cpuCase, LinearSolverBackend::CPU, kFixedOuter, true, /*wallFunctions=*/false);
  const Outcome gpu =
      run(gpuCase, LinearSolverBackend::GPU, kFixedOuter, true, /*wallFunctions=*/false);
  ASSERT_EQ(cpu.result.iterations, kFixedOuter);
  ASSERT_TRUE(gpu.result.residentSimpleLoop);

  const Index n = cpuCase.mesh.numberOfCells();
  const auto cpuGrad =
      cfd::discretization::gradient(cpuCase.mesh, cpu.result.pressure, cpuCase.pressureBoundaries,
                                    cfd::discretization::GradientScheme::GreenGauss);
  const auto gpuGrad =
      cfd::discretization::gradient(gpuCase.mesh, gpu.result.pressure, gpuCase.pressureBoundaries,
                                    cfd::discretization::GradientScheme::GreenGauss);
  Real gNum = 0.0, gDen = 0.0;
  for (Index i = 0; i < n; ++i) {
    const Real dx = gpuGrad[i].x - cpuGrad[i].x;
    const Real dy = gpuGrad[i].y - cpuGrad[i].y;
    gNum += dx * dx + dy * dy;
    gDen += cpuGrad[i].x * cpuGrad[i].x + cpuGrad[i].y * cpuGrad[i].y;
  }
  Real meanMag = 0.0, meanJump = 0.0;
  Index jumps = 0;
  for (Index row = 0; row < kNy; ++row) {
    for (Index col = 0; col + 1 < kNx; ++col) {
      const Index a = row * kNx + col;
      const Real da = gpu.result.pressure[a] - cpu.result.pressure[a];
      const Real db = gpu.result.pressure[a + 1] - cpu.result.pressure[a + 1];
      meanMag += std::abs(da);
      meanJump += std::abs(db - da);
      ++jumps;
    }
  }
  meanMag /= static_cast<Real>(jumps);
  meanJump /= static_cast<Real>(jumps);
  std::printf(
      "\n  k-EPSILON, WALL FUNCTIONS OFF (mu_eff feed only)\n"
      "    absolute p relL2 %.3e   grad(p) relL2 %.3e   k relL2 %.3e   mu_t relL2 %.3e\n"
      "    mean |dp| %.3e   neighbour jump %.3e   jump/mag %.2f\n",
      relativeL2(cpu.result.pressure, gpu.result.pressure),
      gDen > 0.0 ? std::sqrt(gNum) / std::sqrt(gDen) : std::sqrt(gNum), relativeL2(cpu.k, gpu.k),
      relativeL2(cpu.muT, gpu.muT), meanMag, meanJump, meanMag > 0.0 ? meanJump / meanMag : 0.0);
  EXPECT_TRUE(std::isfinite(meanJump));
}

// WHEN does CPU/GPU divergence begin, and does it grow?
//
// The isolated turbulence differentials agree to ~1e-14 (Step 3 ladder). The
// coupled solve disagrees at ~1e-4. Either a wiring defect injects an error at
// some iteration, or a per-iteration rounding difference is amplified by the
// nonlinear coupling. Those look identical at iteration 600 and completely
// different at iteration 1.
TEST(ResidentChannelProduction, DivergenceOnsetCheckpointSweep) {
  skipIfNoDevice();
  std::printf(
      "\n  DIVERGENCE ONSET (common fixed budget, wall functions ON)\n"
      "  iters   p relL2     grad relL2   k relL2     mu_t relL2   tau_w relL2\n");
  for (const Index budget : {Index{1}, Index{2}, Index{5}, Index{10}, Index{25}, Index{50},
                             Index{100}, Index{300}, Index{600}}) {
    ChannelCase cc;
    ChannelCase gc;
    const Outcome cpu = run(cc, LinearSolverBackend::CPU, budget, /*strictOuter=*/true);
    const Outcome gpu = run(gc, LinearSolverBackend::GPU, budget, /*strictOuter=*/true);
    const Index n = cc.mesh.numberOfCells();
    const auto cg =
        cfd::discretization::gradient(cc.mesh, cpu.result.pressure, cc.pressureBoundaries,
                                      cfd::discretization::GradientScheme::GreenGauss);
    const auto gg =
        cfd::discretization::gradient(gc.mesh, gpu.result.pressure, gc.pressureBoundaries,
                                      cfd::discretization::GradientScheme::GreenGauss);
    Real gNum = 0.0, gDen = 0.0;
    for (Index i = 0; i < n; ++i) {
      const Real dx = gg[i].x - cg[i].x;
      const Real dy = gg[i].y - cg[i].y;
      gNum += dx * dx + dy * dy;
      gDen += cg[i].x * cg[i].x + cg[i].y * cg[i].y;
    }
    std::printf("  %-7lld %.3e   %.3e    %.3e   %.3e    %.3e\n", static_cast<long long>(budget),
                relativeL2(cpu.result.pressure, gpu.result.pressure),
                gDen > 0.0 ? std::sqrt(gNum) / std::sqrt(gDen) : std::sqrt(gNum),
                relativeL2(cpu.k, gpu.k), relativeL2(cpu.muT, gpu.muT),
                relativeL2(cpu.wallShear, gpu.wallShear));
  }
  SUCCEED();
}

// =============================================================================
// MANDATORY GATE -- the production residency ledger.
// =============================================================================
//
// One STEADY-STATE turbulent resident outer iteration, measured by differencing
// two complete production solves that differ by exactly one iteration. That is
// what makes it a steady-state measurement rather than an average polluted by
// iteration 0, which uploads the initial state and legitimately transfers whole
// fields.
TEST(ResidentChannelProduction, SteadyStateResidentIterationLedger) {
  skipIfNoDevice();
  ChannelCase caseA;
  ChannelCase caseB;
  constexpr Index kBase = 20;
  const Outcome shorter = run(caseA, LinearSolverBackend::GPU, kBase, /*strictOuter=*/true);
  const auto statsShort = cfd::gpu::gpuExecutionStats();
  const Outcome longer = run(caseB, LinearSolverBackend::GPU, kBase + 1, /*strictOuter=*/true);
  const auto statsLong = cfd::gpu::gpuExecutionStats();

  ASSERT_TRUE(shorter.result.residentSimpleLoop);
  ASSERT_TRUE(longer.result.residentSimpleLoop);
  ASSERT_EQ(shorter.result.iterations, kBase);
  ASSERT_EQ(longer.result.iterations, kBase + 1);

  const auto delta = [](std::uint64_t a, std::uint64_t b) -> long long {
    return static_cast<long long>(b) - static_cast<long long>(a);
  };
  const long long d2hBytes = delta(statsShort.deviceToHostBytes, statsLong.deviceToHostBytes);
  const long long d2hCalls = delta(statsShort.deviceToHostCalls, statsLong.deviceToHostCalls);
  const long long h2dBytes = delta(statsShort.hostToDeviceBytes, statsLong.hostToDeviceBytes);
  const long long h2dCalls = delta(statsShort.hostToDeviceCalls, statsLong.hostToDeviceCalls);
  const long long allocs = delta(statsShort.allocations, statsLong.allocations);
  const long long reallocs = delta(statsShort.reallocations, statsLong.reallocations);

  const Index n = caseA.mesh.numberOfCells();
  const Index faces = caseA.mesh.numberOfFaces();
  const std::uint64_t fullField = sizeof(Real) * static_cast<std::uint64_t>(n);

  std::printf(
      "\n  STEADY-STATE RESIDENT TURBULENT ITERATION (%lld cells, %lld faces)\n"
      "    D2H %lld bytes in %lld calls\n"
      "    H2D %lld bytes in %lld calls\n"
      "    allocations %lld   reallocations %lld\n"
      "    one full cell field = %llu bytes\n",
      static_cast<long long>(n), static_cast<long long>(faces), d2hBytes, d2hCalls, h2dBytes,
      h2dCalls, allocs, reallocs, static_cast<unsigned long long>(fullField));

  // THE RESIDENCY CONTRACT, with the right instrument for each half.
  //
  // ALLOCATIONS ARE BOUNDED, NOT EQUATED. This figure is a difference between
  // two whole solves, and once the steady state genuinely allocates nothing the
  // difference stops measuring the steady state at all -- it measures whatever
  // warm-up the two runs did differently. Measured here at one point: -7, which
  // is not a possible per-iteration count and is the proof that the instrument
  // has bottomed out rather than that seven allocations were returned.
  //
  // So this asserts only what a difference can still support: no POSITIVE
  // per-iteration growth. The exact steady-state figure is gated directly, on a
  // single correction with its rotation warmed, by
  // RepeatedCorrectionsAllocateNothing -- which requires exactly zero.
  EXPECT_LE(allocs, 0) << "a steady-state resident iteration allocated";
  EXPECT_EQ(reallocs, 0) << "a steady-state resident iteration reallocated";
  EXPECT_GE(d2hBytes, 0);
  EXPECT_GE(h2dBytes, 0);

  // H2D IS CHECKED BY SCALING, NOT BY SIZE.
  //
  // A first attempt compared the per-iteration H2D against one full cell field
  // and failed at 2712 vs 2304 bytes -- but that was the TEST being wrong, not
  // the code: on this mesh the legitimate O(N_wall) uploads (48 wall faces x
  // 8 bytes x 7 arrays = 2688, plus 24 bytes of validity flags) simply exceed
  // one 288-cell field. Byte count alone cannot distinguish "one full field"
  // from "a lot of wall faces".
  //
  // The property that actually matters is that H2D does not grow with the CELL
  // count. This mesh and the taller one below share nx, so they share their
  // wall-face count while differing in N -- if any upload were O(N), the two
  // would differ.
  // The taller mesh, and the comparison the paragraph above describes.
  //
  // Until P16 Step 4 this case was CONSTRUCTED AND THEN DISCARDED --
  // `(void)tallCase;` -- so the O(N_wall) claim was argued in prose and never
  // measured. It is measured now, on a mesh with twice the cells and the same
  // nx, so the wall-face count is identical while N and the face count are not.
  ChannelCase tallA(2 * kNy);
  ChannelCase tallB(2 * kNy);
  const Outcome tallShort = run(tallA, LinearSolverBackend::GPU, kBase, /*strictOuter=*/true);
  const auto tallStatsShort = cfd::gpu::gpuExecutionStats();
  const Outcome tallLonger = run(tallB, LinearSolverBackend::GPU, kBase + 1, /*strictOuter=*/true);
  const auto tallStatsLong = cfd::gpu::gpuExecutionStats();
  ASSERT_TRUE(tallShort.result.residentSimpleLoop);
  ASSERT_TRUE(tallLonger.result.residentSimpleLoop);

  const long long tallD2h =
      delta(tallStatsShort.deviceToHostBytes, tallStatsLong.deviceToHostBytes);
  const long long tallH2d =
      delta(tallStatsShort.hostToDeviceBytes, tallStatsLong.hostToDeviceBytes);
  const long long tallD2hCalls =
      delta(tallStatsShort.deviceToHostCalls, tallStatsLong.deviceToHostCalls);
  const Index tallN = tallA.mesh.numberOfCells();
  const Index tallFaces = tallA.mesh.numberOfFaces();

  std::printf(
      "    H2D at %lld wall faces: %lld bytes on %lld cells, %lld bytes on %lld cells\n"
      "    D2H per iteration: %lld bytes on %lld cells, %lld bytes on %lld cells\n",
      static_cast<long long>(2 * kNx), h2dBytes, static_cast<long long>(n), tallH2d,
      static_cast<long long>(tallN), d2hBytes, static_cast<long long>(n), tallD2h,
      static_cast<long long>(tallN));

  // H2D IS O(N_wall): the two meshes share nx, so if any upload were O(N) the
  // doubled cell count would show. Equality, not a bound -- the wall traffic is
  // the same wall traffic.
  EXPECT_EQ(tallH2d, h2dBytes)
      << "per-iteration H2D changed with the CELL count, so some upload is O(N)";

  // D2H IS MEASURED PER TRANSFER, NOT PER ITERATION -- and the first attempt
  // here got that wrong in an instructive way.
  //
  // It bounded the per-iteration D2H GROWTH by what a field download would
  // cost over the added cells (2 x dN x 8, or dF x 8 for the face flux) and
  // failed at 41160 vs 4608. The measurement that followed showed why the
  // criterion, not the code, was wrong:
  //
  //     D2H calls        648  ->  1144
  //     mean bytes/call  46.96 -> 62.58
  //
  // The bytes grew because the CALL COUNT grew: a mesh with twice the cells
  // needs more Krylov iterations, and each one fetches a handful of scalars.
  // That is solver work, not a field transfer, and a per-iteration byte budget
  // cannot tell the two apart -- it charges legitimate scalar traffic against a
  // field-sized allowance.
  //
  // What this scope CAN support is that neither mesh is in a regime where field
  // transfers dominate: the mean transfer stays two orders below one full cell
  // field. That is a coarse instrument -- one 4608-byte velocity download among
  // 1144 calls would move the mean by four bytes -- so it is NOT the gate.
  //
  // THE SENSITIVE GATE IS RepeatedCorrectionsAllocateNothing, which isolates a
  // single correction (where the flux rebuild lives) and bounds ITS D2H
  // directly, with a control proving the counter registers a field-sized
  // transfer. A claim this one cannot carry is made where it can be.
  const long long deltaCells = static_cast<long long>(tallN) - static_cast<long long>(n);
  const long long deltaFaces = static_cast<long long>(tallFaces) - static_cast<long long>(faces);
  std::printf(
      "    D2H growth %lld bytes over %lld more cells / %lld more faces\n"
      "    D2H calls %lld -> %lld; mean bytes/call %.2f -> %.2f\n",
      tallD2h - d2hBytes, deltaCells, deltaFaces, d2hCalls, tallD2hCalls,
      static_cast<double>(d2hBytes) / static_cast<double>(d2hCalls),
      static_cast<double>(tallD2h) / static_cast<double>(tallD2hCalls));

  const double meanBase = static_cast<double>(d2hBytes) / static_cast<double>(d2hCalls);
  const double meanTall = static_cast<double>(tallD2h) / static_cast<double>(tallD2hCalls);
  EXPECT_LT(meanBase, static_cast<double>(fullField));
  EXPECT_LT(meanTall, static_cast<double>(sizeof(Real) * static_cast<std::uint64_t>(tallN)));

  EXPECT_GT(d2hCalls, 0);
}

// =============================================================================
// MANDATORY GATE -- the wrong-flux production negative control.
// =============================================================================
//
// RESTATED AFTER THE FLUX FIX, because its original premise became false.
//
// It used to assert that the resident path consumes SIMPLE's carried corrected
// face flux. It does not, and deliberately must not: the CPU reference builds
// its own convection flux from the cell velocity
// (KEpsilonModel::correctOnHost:234), so the resident path now rebuilds the
// same quantity with calculateMassFluxDevice. Perturbing `inputs.massFlux`
// after that change proves nothing -- the buffer is no longer read by the
// turbulence convection, so the control would have passed vacuously.
//
// What the path DOES consume is the resident velocity, through that flux. So
// the perturbation moves there, and the gate is unchanged in spirit: the first
// transport assembly must notice, not the final convergence.
TEST(ResidentChannelProduction, WrongFluxIsDetectedByTheFirstTransportAssembly) {
  skipIfNoDevice();
  ChannelCase c;
  const Index n = c.mesh.numberOfCells();

  // One correction from the SAME turbulence state, twice, through the RESIDENT
  // entry point -- the one correctFromResidentDiscretization drives in
  // production. Its massFlux input is a device buffer the caller supplies, so
  // the substitution happens exactly where the production path reads it.
  cfd::gpu::DeviceMomentumConvectionPlan plan;
  ASSERT_TRUE(plan.build(c.mesh, c.velocityBoundaries)) << plan.unsupportedReason();
  cfd::gpu::DeviceFaceFluxPlan fluxPlan;
  ASSERT_TRUE(fluxPlan.build(c.mesh, c.velocityBoundaries)) << fluxPlan.unsupportedReason();

  VectorField velocity(n, cfd::Vector2{kBulkVelocity, 0.0});
  const auto hostFlux =
      cfd::physics::calculateMassFlux(c.mesh, velocity, c.fluid, c.velocityBoundaries);

  const auto correctOnce = [&](Real velocityScale) {
    KEpsilonModel seed(c.mesh, c.fluid, c.velocityBoundaries, c.kBoundaries, c.epsilonBoundaries,
                       turbulenceConfig(LinearSolverBackend::GPU));
    cfd::gpu::GpuKEpsilonCorrection correction;
    EXPECT_TRUE(correction.prepare(c.mesh, c.kBoundaries, c.epsilonBoundaries, seed.k(),
                                   seed.epsilon(), seed.turbulentViscosity()));
    // The resident VELOCITY, which is what the turbulence convection flux is
    // now built from.
    cfd::gpu::DeviceVelocity perturbedVelocity;
    std::vector<Real> px(static_cast<std::size_t>(n), kBulkVelocity * velocityScale);
    std::vector<Real> pz(static_cast<std::size_t>(n), 0.0);
    perturbedVelocity.x.uploadFrom(px.data(), n);
    perturbedVelocity.y.uploadFrom(pz.data(), n);
    perturbedVelocity.z.uploadFrom(pz.data(), n);
    cfd::gpu::DeviceBuffer<Real> carriedFlux;
    carriedFlux.uploadFrom(hostFlux.data(), c.mesh.numberOfFaces());

    cfd::gpu::ResidentTurbulenceInputs inputs;
    inputs.velocity = &perturbedVelocity;
    inputs.massFlux = &carriedFlux;
    inputs.convectionPlan = &plan;
    inputs.fluxPlan = &fluxPlan;
    inputs.cellCount = n;

    cfd::turbulence::WallTreatment treatment;
    cfd::gpu::GpuKEpsilonResult result;
    correction.correctResidentWithWallTreatment(
        inputs, c.mesh, c.fluid, turbulenceConfig(LinearSolverBackend::GPU),
        cfd::turbulence::collectWallFaces(c.mesh, c.velocityBoundaries), treatment, result);
    return result;
  };

  const auto correct = correctOnce(1.0);
  const auto wrong = correctOnce(1.05);  // 5% on the velocity the flux is built from

  Real maxKDelta = 0.0;
  Real maxEpsDelta = 0.0;
  for (Index i = 0; i < n; ++i) {
    maxKDelta = std::max(maxKDelta, std::abs(wrong.k[i] - correct.k[i]));
    maxEpsDelta = std::max(maxEpsDelta, std::abs(wrong.epsilon[i] - correct.epsilon[i]));
  }
  std::printf("\n  wrong-flux control: max|dk| %.3e   max|deps| %.3e\n", maxKDelta, maxEpsDelta);
  EXPECT_GT(maxKDelta, 0.0)
      << "perturbing the face flux changed nothing in k, so the transport assembly is not "
         "consuming the flux it was given";
  EXPECT_GT(maxEpsDelta, 0.0) << "epsilon did not respond to the flux either";
}

// =============================================================================
// Persistent turbulence workspace -- correction 1 may allocate, 2..N may not.
// =============================================================================
//
// Isolates the turbulence correction from momentum and pressure, so a non-zero
// number here is unambiguously the turbulence path's.
TEST(ResidentChannelProduction, RepeatedCorrectionsAllocateNothing) {
  skipIfNoDevice();
  ChannelCase c;
  const Index n = c.mesh.numberOfCells();

  cfd::gpu::DeviceMomentumConvectionPlan plan;
  ASSERT_TRUE(plan.build(c.mesh, c.velocityBoundaries)) << plan.unsupportedReason();
  cfd::gpu::DeviceFaceFluxPlan fluxPlan;
  ASSERT_TRUE(fluxPlan.build(c.mesh, c.velocityBoundaries)) << fluxPlan.unsupportedReason();
  VectorField velocity(n, cfd::Vector2{kBulkVelocity, 0.0});
  const auto hostFlux =
      cfd::physics::calculateMassFlux(c.mesh, velocity, c.fluid, c.velocityBoundaries);

  cfd::gpu::DeviceVelocity deviceVelocity;
  std::vector<Real> vx(static_cast<std::size_t>(n), kBulkVelocity);
  std::vector<Real> vy(static_cast<std::size_t>(n), 0.0);
  std::vector<Real> vz(static_cast<std::size_t>(n), 0.0);
  deviceVelocity.x.uploadFrom(vx.data(), n);
  deviceVelocity.y.uploadFrom(vy.data(), n);
  deviceVelocity.z.uploadFrom(vz.data(), n);
  cfd::gpu::DeviceBuffer<Real> deviceFlux;
  deviceFlux.uploadFrom(hostFlux.data(), c.mesh.numberOfFaces());

  KEpsilonModel seed(c.mesh, c.fluid, c.velocityBoundaries, c.kBoundaries, c.epsilonBoundaries,
                     turbulenceConfig(LinearSolverBackend::GPU));
  cfd::gpu::GpuKEpsilonCorrection correction;
  ASSERT_TRUE(correction.prepare(c.mesh, c.kBoundaries, c.epsilonBoundaries, seed.k(),
                                 seed.epsilon(), seed.turbulentViscosity()));

  cfd::gpu::ResidentTurbulenceInputs inputs;
  inputs.velocity = &deviceVelocity;
  inputs.massFlux = &deviceFlux;
  inputs.convectionPlan = &plan;
  inputs.fluxPlan = &fluxPlan;
  inputs.cellCount = n;
  const auto wallFaces = cfd::turbulence::collectWallFaces(c.mesh, c.velocityBoundaries);

  const auto correctOnce = [&]() {
    cfd::turbulence::WallTreatment treatment;
    cfd::gpu::GpuKEpsilonResult result;
    correction.correctResidentWithWallTreatment(inputs, c.mesh, c.fluid,
                                                turbulenceConfig(LinearSolverBackend::GPU),
                                                wallFaces, treatment, result);
  };

  // TWO warm-up corrections, not one, and the reason is the double buffering
  // rather than slack in the gate.
  //
  // The assembly rotates its scratch with the system by swapping. Call 1 sizes
  // the system and hands the scratch the system's old (empty) buffers; call 2
  // sizes the scratch and hands it call 1's sized buffers. Only from call 3 is
  // every buffer in the rotation already the right size. Allowing one warm-up
  // would charge the gate a cost that belongs to filling the second half of a
  // double buffer, which is a one-off, not a steady state.
  correctOnce();
  correctOnce();

  cfd::gpu::resetGpuExecutionStats();
  constexpr int kRepeats = 5;
  for (int i = 0; i < kRepeats; ++i) correctOnce();
  const auto stats = cfd::gpu::gpuExecutionStats();
  const std::uint64_t fullCellField = sizeof(Real) * static_cast<std::uint64_t>(n);
  const std::uint64_t fullFaceField =
      sizeof(Real) * static_cast<std::uint64_t>(c.mesh.numberOfFaces());
  const std::uint64_t d2hPerCorrection = stats.deviceToHostBytes / kRepeats;
  std::printf(
      "\n  %d further corrections: %llu allocations, %llu reallocations\n"
      "    D2H %llu bytes in %llu calls = %llu bytes per correction\n"
      "    one cell field %llu, one velocity %llu, one face-flux field %llu\n",
      kRepeats, static_cast<unsigned long long>(stats.allocations),
      static_cast<unsigned long long>(stats.reallocations),
      static_cast<unsigned long long>(stats.deviceToHostBytes),
      static_cast<unsigned long long>(stats.deviceToHostCalls),
      static_cast<unsigned long long>(d2hPerCorrection),
      static_cast<unsigned long long>(fullCellField),
      static_cast<unsigned long long>(2 * fullCellField),
      static_cast<unsigned long long>(fullFaceField));

  EXPECT_EQ(stats.allocations, 0u) << "the turbulence correction still allocates in steady state";
  EXPECT_EQ(stats.reallocations, 0u);

  // THE SENSITIVE RESIDENCY GATE for P16 Step 4's flux fix.
  //
  // The fix made the resident path rebuild its own convection flux with
  // calculateMassFluxDevice, to match the CPU reference, which recomputes from
  // the cell velocity. The residency question that raises is whether the
  // rebuild pulls the velocity or the face flux back to the host on the way.
  //
  // MEASURED AS THE LARGEST SINGLE TRANSFER, not as a total. Two earlier
  // versions of this gate bounded total D2H bytes -- once per outer iteration,
  // once per correction -- and both were wrong in the same way: a correction
  // moves ~12.8 kB in ~186 calls, i.e. ~69 bytes each, because two Krylov
  // solves fetch a handful of scalars per iteration. That total exceeds a
  // field while containing nothing remotely field-sized, and it grows with
  // mesh size because the ITERATION COUNT does. Bounding it charges solver
  // work against a field-sized allowance.
  //
  // The largest single transfer separates the two cleanly, and the threshold
  // is the smaller of the two things that could have leaked -- one cell field;
  // a velocity is two of those and a face field larger still -- so either
  // would breach it.
  const std::uint64_t largest = stats.largestDeviceToHostBytes;
  const std::uint64_t atLargest = stats.deviceToHostCallsAtLargestSize;
  std::printf("    largest single D2H %llu bytes, %llu copies of that size over %d corrections\n",
              static_cast<unsigned long long>(largest), static_cast<unsigned long long>(atLargest),
              kRepeats);

  // NOTHING LARGER THAN ONE CELL FIELD EVER MOVES. This is what rules out the
  // face flux, which is 612 faces x 8 = 4896 bytes on this mesh -- more than
  // twice a cell field, so a single such copy would raise the maximum.
  EXPECT_EQ(largest, fullCellField)
      << "the largest D2H was not one cell field; a face-flux download would be " << fullFaceField;
  EXPECT_LT(fullCellField, fullFaceField) << "the threshold cannot separate the two on this mesh";

  // AND EXACTLY THREE COPIES OF THAT SIZE PER CORRECTION -- k, epsilon and
  // mu_t, the host ScalarFields TurbulenceModel's interface publishes. That
  // cost is recorded and accepted at GpuKEpsilonCorrectionCuda.cpp's
  // runCorrection: the CSR, RHS and solution never leave the device, and making
  // the published fields resident too would mean exposing device handles
  // through TurbulenceModel, which P16 does not authorize.
  //
  // The COUNT is what rules out a velocity download, which size alone cannot:
  // one velocity component is also exactly one cell field. Two more components
  // would read 5 per correction, three would read 6.
  EXPECT_EQ(atLargest, 3u * kRepeats)
      << "a correction made " << (static_cast<double>(atLargest) / kRepeats)
      << " cell-field-sized downloads, not the three the TurbulenceModel interface publishes -- "
         "the resident path is downloading the velocity";

  // INSTRUMENT VALIDITY. The bound above is worth exactly what the counter's
  // ability to see a field-sized transfer is worth. So one is performed
  // deliberately, and the counter is required to register it at full size --
  // otherwise a counter that silently stopped working would report a perfect
  // residency contract.
  cfd::gpu::resetGpuExecutionStats();
  std::vector<Real> sink(static_cast<std::size_t>(n), 0.0);
  deviceVelocity.x.downloadTo(sink.data(), n);
  const auto controlStats = cfd::gpu::gpuExecutionStats();
  EXPECT_EQ(controlStats.largestDeviceToHostBytes, fullCellField)
      << "the high-water counter did not register a deliberate full-field download, so the bound "
         "above proves nothing";
  EXPECT_EQ(controlStats.deviceToHostCallsAtLargestSize, 1u)
      << "the count-at-largest-size did not register one deliberate full-field download, so the "
         "velocity check above proves nothing";
}

// =============================================================================
// THE IMPLEMENTATION-EQUIVALENCE GATE -- same mature state, one outer iteration.
// =============================================================================
//
// Two independently evolved 600-iteration trajectories cannot answer "does the
// GPU implement the same iteration?". The divergence-onset sweep showed why:
// at iteration 1 the turbulence fields agree to 2.1e-15, so nothing is
// injected -- what accumulates afterwards is nonlinear sensitivity to a
// rounding-level difference, which is a property of the equations, not of the
// port.
//
// This clones a MATURE CPU state into both arms and runs exactly one
// production outer iteration from it. No trajectory is accumulated between
// checkpoints: each one starts from the CPU reference state again.
TEST(ResidentChannelProduction, MatureSameStateOneStepMap) {
  skipIfNoDevice();
  std::printf(
      "\n  MATURE SAME-STATE ONE-STEP MAP (clone -> one production outer iteration)\n"
      "  from    U          p          flux       k          eps        mu_t       tau_w\n");

  for (const Index checkpoint : {Index{10}, Index{50}, Index{300}, Index{600}}) {
    // --- capture a mature CPU state ---------------------------------------
    ChannelCase ref;
    const Index n = ref.mesh.numberOfCells();
    KEpsilonModel refModel(ref.mesh, ref.fluid, ref.velocityBoundaries, ref.kBoundaries,
                           ref.epsilonBoundaries, turbulenceConfig(LinearSolverBackend::CPU));
    VectorField v0(n, cfd::Vector2{kBulkVelocity, 0.0});
    ScalarField p0(n, 0.0);
    const SIMPLE spin(settingsFor(LinearSolverBackend::CPU, checkpoint, /*strictOuter=*/true), 0,
                      &refModel);
    const auto captured =
        spin.solve(ref.mesh, ref.fluid, ref.velocityBoundaries, ref.pressureBoundaries, v0, p0);
    ASSERT_EQ(captured.iterations, checkpoint) << "the capture run stopped early";

    // --- one outer iteration from that exact state, on each backend --------
    const auto oneStep = [&](LinearSolverBackend backend) {
      ChannelCase c;
      KEpsilonModel model(c.mesh, c.fluid, c.velocityBoundaries, c.kBoundaries, c.epsilonBoundaries,
                          turbulenceConfig(backend));
      cfd::turbulence::testing::KEpsilonModelTestAccess::injectState(
          model, c.mesh, refModel.k(), refModel.epsilon(), refModel.turbulentViscosity());
      if (backend == LinearSolverBackend::GPU) {
        EXPECT_TRUE(cfd::turbulence::testing::KEpsilonModelTestAccess::gpuActive(model))
            << "the GPU path fell back after injection; this arm would be CPU";
      }
      // STRICT KRYLOV, for the instrument rather than for the bound.
      //
      // At production linear tolerances (pressure 1e-9 / 1e-7) the two arms
      // stop on adjacent Krylov iterations and the resulting pressure
      // difference sits at ~1e-6 -- the same order as the field bound, so the
      // instrument cannot resolve an implementation difference from a stopping
      // difference. Proven on the frozen checkpoint-50 system by
      // Checkpoint50UnderStrictKrylov: 1.748e-06 at production tolerances,
      // 5.903e-14 at strict ones, an eight-order drop from a seven-order
      // tightening.
      //
      // Production defaults are untouched and the 1e-6 field bound is
      // unchanged; only this diagnostic's linear stopping criterion moves.
      const SIMPLE step(settingsFor(backend, 1, /*strictOuter=*/true, /*strictKrylov=*/true), 0,
                        &model);
      Outcome out;
      out.result = step.solve(c.mesh, c.fluid, c.velocityBoundaries, c.pressureBoundaries,
                              captured.velocity, captured.pressure);
      out.k = model.k();
      out.epsilon = model.epsilon();
      out.muT = model.turbulentViscosity();
      out.yPlus = model.yPlus();
      out.wallShear = model.wallShearStress();
      if (model.wallViscosity() != nullptr) out.wallViscosity = *model.wallViscosity();
      return out;
    };

    // The cloned inputs are asserted identical BEFORE either arm runs: both
    // are handed the same captured.velocity / captured.pressure objects and
    // the same three injected fields, so the premise is by construction.
    const Outcome cpu = oneStep(LinearSolverBackend::CPU);
    const Outcome gpu = oneStep(LinearSolverBackend::GPU);

    Real uNum = 0.0, uDen = 0.0, fNum = 0.0, fDen = 0.0;
    for (Index i = 0; i < n; ++i) {
      const Real du = gpu.result.velocity[i].x - cpu.result.velocity[i].x;
      const Real dv = gpu.result.velocity[i].y - cpu.result.velocity[i].y;
      uNum += du * du + dv * dv;
      uDen += cpu.result.velocity[i].x * cpu.result.velocity[i].x +
              cpu.result.velocity[i].y * cpu.result.velocity[i].y;
    }
    for (Index f = 0; f < ref.mesh.numberOfFaces(); ++f) {
      const Real d = gpu.result.massFlux[f] - cpu.result.massFlux[f];
      fNum += d * d;
      fDen += cpu.result.massFlux[f] * cpu.result.massFlux[f];
    }
    const Real uErr = uDen > 0.0 ? std::sqrt(uNum) / std::sqrt(uDen) : std::sqrt(uNum);
    const Real fErr = fDen > 0.0 ? std::sqrt(fNum) / std::sqrt(fDen) : std::sqrt(fNum);
    const Real pErr = relativeL2(cpu.result.pressure, gpu.result.pressure);
    const Real kErr = relativeL2(cpu.k, gpu.k);
    const Real eErr = relativeL2(cpu.epsilon, gpu.epsilon);
    const Real mErr = relativeL2(cpu.muT, gpu.muT);
    const Real tErr = relativeL2(cpu.wallShear, gpu.wallShear);

    // Localisation before conclusion: a relative error is a ratio, and a small
    // denominator inflates it without anything having gone wrong. These are
    // the absolute quantities behind the ratio, plus the linear-solver work
    // each arm did, so a stopping-point difference is visible rather than
    // inferred.
    Real pNorm = 0.0;
    Real maxAbsDp = 0.0;
    for (Index i = 0; i < n; ++i) {
      pNorm += cpu.result.pressure[i] * cpu.result.pressure[i];
      maxAbsDp = std::max(maxAbsDp, std::abs(gpu.result.pressure[i] - cpu.result.pressure[i]));
    }
    pNorm = std::sqrt(pNorm);
    std::printf(
        "  %-7lld %.3e  %.3e  %.3e  %.3e  %.3e  %.3e  %.3e\n"
        "          ||p||=%.3e  max|dp|=%.3e   p_lin_it cpu %lld gpu %lld"
        "   mom_lin_it cpu %lld gpu %lld\n",
        static_cast<long long>(checkpoint), uErr, pErr, fErr, kErr, eErr, mErr, tErr, pNorm,
        maxAbsDp, static_cast<long long>(cpu.result.pressureLinearIterations),
        static_cast<long long>(gpu.result.pressureLinearIterations),
        static_cast<long long>(cpu.result.momentumLinearIterations),
        static_cast<long long>(gpu.result.momentumLinearIterations));

    constexpr Real kBound = 1e-6;
    EXPECT_LE(uErr, kBound) << "U, from iteration " << checkpoint;
    EXPECT_LE(pErr, kBound) << "pressure, from iteration " << checkpoint;
    EXPECT_LE(fErr, kBound) << "face flux, from iteration " << checkpoint;
    EXPECT_LE(kErr, kBound) << "k, from iteration " << checkpoint;
    EXPECT_LE(eErr, kBound) << "epsilon, from iteration " << checkpoint;
    EXPECT_LE(mErr, kBound) << "mu_t, from iteration " << checkpoint;
    EXPECT_LE(tErr, kBound) << "wall shear, from iteration " << checkpoint;
    EXPECT_LE(relativeL2(cpu.yPlus, gpu.yPlus), kBound) << "y+, from iteration " << checkpoint;
  }
}

// Is the one-step U/p/flux difference about turbulence at all? Same experiment,
// LAMINAR model: no turbulence path runs on either arm.
TEST(ResidentChannelProduction, LaminarSameStateOneStepMap) {
  skipIfNoDevice();
  ChannelCase ref;
  const Index n = ref.mesh.numberOfCells();
  cfd::turbulence::LaminarModel refLam(ref.mesh);
  VectorField v0(n, cfd::Vector2{kBulkVelocity, 0.0});
  ScalarField p0(n, 0.0);
  const SIMPLE spin(settingsFor(LinearSolverBackend::CPU, 300, /*strictOuter=*/true), 0, &refLam);
  const auto captured =
      spin.solve(ref.mesh, ref.fluid, ref.velocityBoundaries, ref.pressureBoundaries, v0, p0);
  ASSERT_EQ(captured.iterations, 300);

  const auto oneStep = [&](LinearSolverBackend backend) {
    ChannelCase c;
    cfd::turbulence::LaminarModel lam(c.mesh);
    const SIMPLE step(settingsFor(backend, 1, /*strictOuter=*/true), 0, &lam);
    return step.solve(c.mesh, c.fluid, c.velocityBoundaries, c.pressureBoundaries,
                      captured.velocity, captured.pressure);
  };
  const auto cpu = oneStep(LinearSolverBackend::CPU);
  const auto gpu = oneStep(LinearSolverBackend::GPU);

  Real uNum = 0.0, uDen = 0.0;
  for (Index i = 0; i < n; ++i) {
    const Real du = gpu.velocity[i].x - cpu.velocity[i].x;
    uNum += du * du;
    uDen += cpu.velocity[i].x * cpu.velocity[i].x;
  }
  std::printf(
      "\n  LAMINAR one-step from a mature state: U %.3e   p %.3e\n"
      "    cpu status %d iters %lld   gpu status %d iters %lld\n",
      uDen > 0.0 ? std::sqrt(uNum) / std::sqrt(uDen) : std::sqrt(uNum),
      relativeL2(cpu.pressure, gpu.pressure), static_cast<int>(cpu.status),
      static_cast<long long>(cpu.iterations), static_cast<int>(gpu.status),
      static_cast<long long>(gpu.iterations));
  SUCCEED();
}

// Is checkpoint 50's residual pressure difference a linear-solver STOPPING
// effect? Same frozen state, same everything, with the already-approved strict
// equivalence Krylov settings. Production defaults and the field bound are
// untouched.
TEST(ResidentChannelProduction, Checkpoint50UnderStrictKrylov) {
  skipIfNoDevice();
  constexpr Index kCheckpoint = 50;
  ChannelCase ref;
  const Index n = ref.mesh.numberOfCells();
  KEpsilonModel refModel(ref.mesh, ref.fluid, ref.velocityBoundaries, ref.kBoundaries,
                         ref.epsilonBoundaries, turbulenceConfig(LinearSolverBackend::CPU));
  VectorField v0(n, cfd::Vector2{kBulkVelocity, 0.0});
  ScalarField p0(n, 0.0);
  const SIMPLE spin(settingsFor(LinearSolverBackend::CPU, kCheckpoint, true), 0, &refModel);
  const auto captured =
      spin.solve(ref.mesh, ref.fluid, ref.velocityBoundaries, ref.pressureBoundaries, v0, p0);
  ASSERT_EQ(captured.iterations, kCheckpoint);

  const auto oneStep = [&](LinearSolverBackend backend, bool strictKrylov) {
    ChannelCase c;
    KEpsilonModel model(c.mesh, c.fluid, c.velocityBoundaries, c.kBoundaries, c.epsilonBoundaries,
                        turbulenceConfig(backend));
    cfd::turbulence::testing::KEpsilonModelTestAccess::injectState(
        model, c.mesh, refModel.k(), refModel.epsilon(), refModel.turbulentViscosity());
    const SIMPLE step(settingsFor(backend, 1, true, strictKrylov), 0, &model);
    return step.solve(c.mesh, c.fluid, c.velocityBoundaries, c.pressureBoundaries,
                      captured.velocity, captured.pressure);
  };

  const auto loosCpu = oneStep(LinearSolverBackend::CPU, false);
  const auto loosGpu = oneStep(LinearSolverBackend::GPU, false);
  const auto tightCpu = oneStep(LinearSolverBackend::CPU, true);
  const auto tightGpu = oneStep(LinearSolverBackend::GPU, true);

  const Real loose = relativeL2(loosCpu.pressure, loosGpu.pressure);
  const Real tight = relativeL2(tightCpu.pressure, tightGpu.pressure);
  std::printf(
      "\n  CHECKPOINT 50, pressure relL2\n"
      "    production Krylov (1e-9/1e-7):   %.3e   p_lin_it cpu %lld gpu %lld\n"
      "    strict Krylov     (1e-16/1e-14): %.3e   p_lin_it cpu %lld gpu %lld\n",
      loose, static_cast<long long>(loosCpu.pressureLinearIterations),
      static_cast<long long>(loosGpu.pressureLinearIterations), tight,
      static_cast<long long>(tightCpu.pressureLinearIterations),
      static_cast<long long>(tightGpu.pressureLinearIterations));

  // Non-vacuity: the looser setting must actually reproduce the failure, or
  // this comparison proves nothing about it.
  EXPECT_GT(loose, 1e-6) << "the production-Krylov run no longer reproduces the checkpoint-50 "
                            "failure, so this experiment has nothing to explain";
  EXPECT_LE(tight, 1e-6)
      << "tightening the linear solves did not bring checkpoint 50 inside the bound, so the "
         "difference is not a solver stopping effect and must be traced further";
}

// =============================================================================
// Cross-backend continuation -- does backend identity create its own branch?
// =============================================================================
//
// From ONE mature state, both backends continue for several outer iterations.
// The question is not whether two independently evolved 600-iteration
// trajectories agree -- they need not -- but whether starting from the same
// state and changing only the backend immediately produces a distinct
// trajectory. If it does, there is a backend-specific fixed point and the
// same-state map was measuring too short a horizon to see it.
//
// Each horizon is a fresh continuation from the SAME captured state, so the
// evolution inside a horizon is continuous while no error is carried between
// horizons. Strict equivalence Krylov settings, for the reason
// Checkpoint50UnderStrictKrylov establishes: at production linear tolerances
// the stopping noise alone reaches the field bound.
TEST(ResidentChannelProduction, CrossBackendContinuationFromMatureState) {
  skipIfNoDevice();

  for (const Index checkpoint : {Index{300}, Index{600}}) {
    ChannelCase ref;
    const Index n = ref.mesh.numberOfCells();
    KEpsilonModel refModel(ref.mesh, ref.fluid, ref.velocityBoundaries, ref.kBoundaries,
                           ref.epsilonBoundaries, turbulenceConfig(LinearSolverBackend::CPU));
    VectorField v0(n, cfd::Vector2{kBulkVelocity, 0.0});
    ScalarField p0(n, 0.0);
    const SIMPLE spin(settingsFor(LinearSolverBackend::CPU, checkpoint, true), 0, &refModel);
    const auto captured =
        spin.solve(ref.mesh, ref.fluid, ref.velocityBoundaries, ref.pressureBoundaries, v0, p0);
    ASSERT_EQ(captured.iterations, checkpoint);

    const auto continueFor = [&](LinearSolverBackend backend, Index horizon) {
      ChannelCase c;
      KEpsilonModel model(c.mesh, c.fluid, c.velocityBoundaries, c.kBoundaries, c.epsilonBoundaries,
                          turbulenceConfig(backend));
      cfd::turbulence::testing::KEpsilonModelTestAccess::injectState(
          model, c.mesh, refModel.k(), refModel.epsilon(), refModel.turbulentViscosity());
      const SIMPLE run(settingsFor(backend, horizon, true, true), 0, &model);
      Outcome out;
      out.result = run.solve(c.mesh, c.fluid, c.velocityBoundaries, c.pressureBoundaries,
                             captured.velocity, captured.pressure);
      out.k = model.k();
      out.epsilon = model.epsilon();
      out.muT = model.turbulentViscosity();
      out.yPlus = model.yPlus();
      out.wallShear = model.wallShearStress();
      if (model.wallViscosity() != nullptr) out.wallViscosity = *model.wallViscosity();
      return out;
    };

    std::printf(
        "\n  CROSS-BACKEND CONTINUATION from checkpoint %lld\n"
        "  horizon  U          p          grad(p)    flux       k          mu_t       tau_w\n",
        static_cast<long long>(checkpoint));
    for (const Index horizon : {Index{1}, Index{2}, Index{3}, Index{5}, Index{8}}) {
      const Outcome cpu = continueFor(LinearSolverBackend::CPU, horizon);
      const Outcome gpu = continueFor(LinearSolverBackend::GPU, horizon);
      ASSERT_EQ(cpu.result.iterations, horizon);
      ASSERT_EQ(gpu.result.iterations, horizon);

      Real uNum = 0.0, uDen = 0.0, fNum = 0.0, fDen = 0.0;
      for (Index i = 0; i < n; ++i) {
        const Real du = gpu.result.velocity[i].x - cpu.result.velocity[i].x;
        const Real dv = gpu.result.velocity[i].y - cpu.result.velocity[i].y;
        uNum += du * du + dv * dv;
        uDen += cpu.result.velocity[i].x * cpu.result.velocity[i].x +
                cpu.result.velocity[i].y * cpu.result.velocity[i].y;
      }
      for (Index f = 0; f < ref.mesh.numberOfFaces(); ++f) {
        const Real d = gpu.result.massFlux[f] - cpu.result.massFlux[f];
        fNum += d * d;
        fDen += cpu.result.massFlux[f] * cpu.result.massFlux[f];
      }
      const auto cg =
          cfd::discretization::gradient(ref.mesh, cpu.result.pressure, ref.pressureBoundaries,
                                        cfd::discretization::GradientScheme::GreenGauss);
      const auto gg =
          cfd::discretization::gradient(ref.mesh, gpu.result.pressure, ref.pressureBoundaries,
                                        cfd::discretization::GradientScheme::GreenGauss);
      Real gNum = 0.0, gDen = 0.0;
      for (Index i = 0; i < n; ++i) {
        const Real dx = gg[i].x - cg[i].x;
        const Real dy = gg[i].y - cg[i].y;
        gNum += dx * dx + dy * dy;
        gDen += cg[i].x * cg[i].x + cg[i].y * cg[i].y;
      }
      const Real uErr = uDen > 0.0 ? std::sqrt(uNum) / std::sqrt(uDen) : std::sqrt(uNum);
      const Real fErr = fDen > 0.0 ? std::sqrt(fNum) / std::sqrt(fDen) : std::sqrt(fNum);
      const Real gErr = gDen > 0.0 ? std::sqrt(gNum) / std::sqrt(gDen) : std::sqrt(gNum);
      const Real pErr = relativeL2(cpu.result.pressure, gpu.result.pressure);
      const Real kErr = relativeL2(cpu.k, gpu.k);
      const Real mErr = relativeL2(cpu.muT, gpu.muT);
      const Real tErr = relativeL2(cpu.wallShear, gpu.wallShear);

      std::printf("  %-8lld %.3e  %.3e  %.3e  %.3e  %.3e  %.3e  %.3e\n",
                  static_cast<long long>(horizon), uErr, pErr, gErr, fErr, kErr, mErr, tErr);

      constexpr Real kBound = 1e-6;
      EXPECT_LE(uErr, kBound) << "U after " << horizon << " from checkpoint " << checkpoint;
      EXPECT_LE(pErr, kBound) << "pressure after " << horizon;
      EXPECT_LE(gErr, kBound) << "pressure gradient after " << horizon;
      EXPECT_LE(fErr, kBound) << "face flux after " << horizon;
      EXPECT_LE(kErr, kBound) << "k after " << horizon;
      EXPECT_LE(relativeL2(cpu.epsilon, gpu.epsilon), kBound) << "epsilon after " << horizon;
      EXPECT_LE(mErr, kBound) << "mu_t after " << horizon;
      EXPECT_LE(tErr, kBound) << "wall shear after " << horizon;
      EXPECT_LE(relativeL2(cpu.yPlus, gpu.yPlus), kBound) << "y+ after " << horizon;
      EXPECT_LE(relativeL2(cpu.wallViscosity, gpu.wallViscosity), kBound)
          << "wall viscosity after " << horizon;
    }
  }
}

// Quantifies the root cause found by inspection: the two turbulence paths are
// handed DIFFERENT mass fluxes.
//
//   KEpsilonModel::correctOnHost:234   calculateMassFlux(mesh, velocity, ...)
//   resident path                      SIMPLE's carried CORRECTED face flux
//
// In SIMPLE those are not the same quantity -- the corrected face flux differs
// from one recomputed from the corrected cell velocities by the Rhie-Chow face
// correction. The isolated Step-3 harness supplied calculateMassFlux() as its
// "resident" flux, so both arms consumed the same thing and agreed to 1e-14;
// the coupled path is the first place they can differ.
TEST(ResidentChannelProduction, CarriedFluxDiffersFromRecomputedFlux) {
  ChannelCase c;
  const Index n = c.mesh.numberOfCells();
  KEpsilonModel model(c.mesh, c.fluid, c.velocityBoundaries, c.kBoundaries, c.epsilonBoundaries,
                      turbulenceConfig(LinearSolverBackend::CPU));
  VectorField v0(n, cfd::Vector2{kBulkVelocity, 0.0});
  ScalarField p0(n, 0.0);
  const SIMPLE spin(settingsFor(LinearSolverBackend::CPU, 300, true), 0, &model);
  const auto mature =
      spin.solve(c.mesh, c.fluid, c.velocityBoundaries, c.pressureBoundaries, v0, p0);
  ASSERT_EQ(mature.iterations, 300);

  // What the CPU turbulence correction builds its convection operator from.
  const auto recomputed =
      cfd::physics::calculateMassFlux(c.mesh, mature.velocity, c.fluid, c.velocityBoundaries);

  Real num = 0.0;
  Real den = 0.0;
  Real maxAbs = 0.0;
  for (Index f = 0; f < c.mesh.numberOfFaces(); ++f) {
    const Real d = mature.massFlux[f] - recomputed[f];
    num += d * d;
    den += recomputed[f] * recomputed[f];
    maxAbs = std::max(maxAbs, std::abs(d));
  }
  const Real rel = den > 0.0 ? std::sqrt(num) / std::sqrt(den) : std::sqrt(num);
  std::printf(
      "\n  CARRIED vs RECOMPUTED face flux at a mature state\n"
      "    relative L2 %.3e   max|difference| %.3e\n"
      "    (BOTH turbulence paths now consume the second; the gap below is what the\n"
      "     resident path used to inherit from SIMPLE and no longer does)\n",
      rel, maxAbs);

  // The point is that they are NOT the same quantity. If this ever became zero
  // the explanation above would be wrong and the continuation failure would
  // need another cause.
  EXPECT_GT(rel, 0.0) << "carried and recomputed flux are identical on this case, so they cannot "
                         "explain the coupled turbulence difference";
}
