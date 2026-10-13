// P16 Step 4 -- the real production seam: turbulent k-epsilon through
// SIMPLE::solve on the GPU-resident loop.
//
// Everything before this ran through a turbulence harness. This drives
// SIMPLE::solve itself, with a k-epsilon model and wall functions, and requires
// the resident loop to actually engage rather than to quietly decline.
//
// WHAT THE OLD GUARD WAS PROTECTING
// ---------------------------------
// The resident loop's eligibility used to read `name() == "laminar"`, and the
// reason in SIMPLE.cpp was precise: a resident loop stops downloading velocity,
// so `activeModel->correct(mesh, velocity, pressure)` would be handed a HOST
// field frozen at iteration 0 for the whole solve. Laminar was immune only
// because its correct() is a no-op.
//
// The replacement is a capability, `supportsResidentGpuCorrection()`, answered
// by the model. k-epsilon answers true only when every link exists: resident
// velocity and corrected flux, device gradient, GPU P_k, compact CPU wall law,
// resident k/epsilon solves, device mu_t and mu_eff, and the GPU wall-face
// viscosity override. Everything else inherits a default of false, so SST and
// anything added later stay out by construction rather than by an exclusion
// list someone has to remember to extend.
//
// THE CONTROLS ARE THE POINT. A CPU/GPU field comparison can pass while the
// resident path silently declined and both arms ran the same CPU code. So the
// admission tests assert `residentSimpleLoop` directly, and the stale-host
// control proves the frozen mirror genuinely cannot reach the answer.
#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <memory>
#include <vector>

#include "cfd/algebra/LinearSolver.hpp"
#include "cfd/boundary/FixedGradient.hpp"
#include "cfd/boundary/FixedValue.hpp"
#include "cfd/boundary/Inlet.hpp"
#include "cfd/boundary/MovingWall.hpp"
#include "cfd/boundary/Outlet.hpp"
#include "cfd/boundary/Wall.hpp"
#include "cfd/fields/ScalarField.hpp"
#include "cfd/fields/VectorField.hpp"
#include "cfd/gpu/GPUBackend.hpp"
#include "cfd/gpu/GPUExecutionStats.hpp"
#include "cfd/mesh/MeshGeometry.hpp"
#include "cfd/physics/FluidProperties.hpp"
#include "cfd/pressure_velocity/SIMPLE.hpp"
#include "cfd/turbulence/KEpsilonModel.hpp"
#include "cfd/turbulence/LaminarModel.hpp"
#include "cfd/turbulence/SSTModel.hpp"

using cfd::Index;
using cfd::Real;
using cfd::algebra::LinearSolverBackend;
using cfd::algebra::LinearSolverType;
using cfd::boundary::BoundaryConditionSet;
using cfd::fields::ScalarField;
using cfd::fields::VectorField;
using cfd::mesh::Mesh;
using cfd::mesh::MeshGeometry;
using cfd::physics::FluidProperties;
using cfd::pressure_velocity::SIMPLE;
using cfd::pressure_velocity::SIMPLEResult;
using cfd::pressure_velocity::SIMPLESettings;
using cfd::turbulence::KEpsilonConfig;
using cfd::turbulence::KEpsilonModel;

namespace {

void skipIfNoDevice() {
  if (!cfd::gpu::cudaAvailable()) GTEST_SKIP() << "no usable CUDA device at runtime";
}

// THE REPOSITORY'S OWN CONVERGING k-EPSILON CASE.
//
// This is the lid-driven cavity and the relaxation from
// tests/solver/simple/test_simple_kepsilon.cpp -- a closed domain, walls all
// round, moving lid -- adopted rather than invented.
//
// AN INLET/OUTLET CHANNEL WAS TRIED FIRST AND DISCARDED, because the CPU
// REFERENCE diverged on it: continuity went 6.2e-2 -> 1.7e-1 -> 1.3 -> 1.5e+9
// over 50/200/600/2000 outer iterations, on both backends. A differential
// against a diverging reference measures nothing, and the field bound here is
// calibrated on converged solutions -- applying it to a transient is the exact
// methodological error results/gpu-pipe-001/equivalence/summary.md section 2
// records and refuses to repeat.
struct ChannelCase {
  Mesh mesh;
  FluidProperties fluid;
  BoundaryConditionSet velocityBoundaries;
  BoundaryConditionSet pressureBoundaries;
  BoundaryConditionSet kBoundaries;
  BoundaryConditionSet epsilonBoundaries;

  ChannelCase() : mesh(MeshGeometry::createCartesian2D(10, 10, 1.0, 1.0)), fluid(1.0, 0.01) {
    // BoundaryConditionSet::set refuses to overwrite, so the lid and its k
    // value are chosen per patch rather than assigned twice.
    for (const auto& patch : mesh.boundaryPatches()) {
      const bool lid = patch.name() == "top";
      if (lid) {
        velocityBoundaries.set(mesh, patch.name(),
                               std::make_unique<cfd::boundary::MovingWall>(cfd::Vector2{1.0, 0.0}));
        kBoundaries.set(mesh, patch.name(), std::make_unique<cfd::boundary::FixedValue>(0.02));
      } else {
        velocityBoundaries.set(mesh, patch.name(), std::make_unique<cfd::boundary::Wall>());
        kBoundaries.set(mesh, patch.name(), std::make_unique<cfd::boundary::FixedValue>(0.0));
      }
      pressureBoundaries.set(mesh, patch.name(),
                             std::make_unique<cfd::boundary::FixedGradient>(0.0));
      epsilonBoundaries.set(mesh, patch.name(),
                            std::make_unique<cfd::boundary::FixedGradient>(0.0));
    }
  }
};

KEpsilonConfig turbulenceConfig(LinearSolverBackend backend) {
  KEpsilonConfig c;
  c.initialK = 0.01;
  c.initialEpsilon = 0.001;
  // WALL FUNCTIONS ARE OFF ON THIS CASE, and that is a measured decision, not
  // an oversight. With them on, the lid-driven cavity reaches
  // SIMPLEStatus::NonFiniteState after 3 outer iterations on BOTH backends --
  // the CPU arm runs no GPU code at all, so the case is at fault, not the
  // resident path. A cavity is a poor wall-function geometry: the corners put
  // y+ far outside the law's valid band. The repository's validated
  // wall-function case is the CHANNEL, not the cavity.
  //
  // The consequence is stated rather than hidden: `wallFaces_` is empty here,
  // so setWallFaceViscosity receives an empty list and the GPU wall-face
  // override is NOT exercised by this differential. It is covered directly by
  // DeviceWallFaceViscosity.* against the host assembly; what is missing is a
  // PRODUCTION differential with wall functions active.
  c.wallFunctions = false;
  c.kSolver.backend = backend;
  c.kSolver.type = LinearSolverType::BiCGSTAB;
  c.epsilonSolver = c.kSolver;
  return c;
}

SIMPLESettings settingsFor(LinearSolverBackend backend, Index outerIterations) {
  SIMPLESettings settings;
  settings.maxIterations = outerIterations;
  settings.velocityRelaxation = 0.5;
  settings.pressureRelaxation = 0.3;
  settings.velocityTolerance = 1e-6;
  settings.pressureTolerance = 1e-6;
  settings.continuityTolerance = 1e-6;
  settings.turbulenceTolerance = 1e-6;
  settings.momentumSolver.type = LinearSolverType::BiCGSTAB;
  settings.momentumSolver.backend = backend;
  settings.momentumSolver.maxIterations = 500;
  settings.pressureSolver.type = LinearSolverType::BiCGSTAB;
  settings.pressureSolver.backend = backend;
  settings.pressureSolver.maxIterations = 1000;
  settings.enableGpuDiscretization = backend == LinearSolverBackend::GPU;
  return settings;
}

struct Outcome {
  SIMPLEResult result;
  VectorField velocity;
  ScalarField pressure;
  ScalarField k;
  ScalarField epsilon;
  ScalarField muT;
  ScalarField yPlus;
  ScalarField wallShear;
  cfd::fields::SurfaceField massFlux;
};

// Runs the whole production solve. `velocitySeed` lets a control start the
// HOST velocity field somewhere other than the physical initial condition
// without touching anything the device carries.
Outcome run(ChannelCase& c, LinearSolverBackend backend, Index outerIterations,
            Real velocitySeed = 0.0) {
  const Index n = c.mesh.numberOfCells();
  KEpsilonModel model(c.mesh, c.fluid, c.velocityBoundaries, c.kBoundaries, c.epsilonBoundaries,
                      turbulenceConfig(backend));
  VectorField velocity(n);
  for (Index i = 0; i < n; ++i) velocity[i] = {velocitySeed, 0.0, 0.0};
  ScalarField pressure(n, 0.0);

  const SIMPLE simple(settingsFor(backend, outerIterations), 0, &model);
  Outcome out;
  out.result =
      simple.solve(c.mesh, c.fluid, c.velocityBoundaries, c.pressureBoundaries, velocity, pressure);
  // FROM THE RESULT, not from the inputs. solve() takes initialVelocity and
  // initialPressure BY VALUE and returns the solution in SIMPLEResult; reading
  // the local fields back would compare two untouched copies of the initial
  // condition and report a difference of exactly zero forever.
  out.velocity = out.result.velocity;
  out.pressure = out.result.pressure;
  out.massFlux = out.result.massFlux;
  out.k = model.k();
  out.epsilon = model.epsilon();
  out.muT = model.turbulentViscosity();
  out.yPlus = model.yPlus();
  out.wallShear = model.wallShearStress();
  return out;
}

}  // namespace

// =============================================================================
// Admission -- the capability guard lets exactly the right models through.
// =============================================================================

TEST(ResidentTurbulentSimple, QualifiedKEpsilonEngagesTheResidentLoop) {
  skipIfNoDevice();
  ChannelCase c;
  const Outcome gpu = run(c, LinearSolverBackend::GPU, 6);

  // Asserted directly. A field comparison alone would pass just as happily if
  // the resident path had declined and both arms ran the same CPU code.
  EXPECT_TRUE(gpu.result.residentSimpleLoop)
      << "the resident SIMPLE loop did not engage for a qualified k-epsilon model, so every "
         "turbulent residency claim below would be about a path that never ran";
  EXPECT_TRUE(gpu.result.residentPressureSolve);
}

TEST(ResidentTurbulentSimple, LaminarStillEngagesTheResidentLoop) {
  skipIfNoDevice();
  ChannelCase c;
  const Index n = c.mesh.numberOfCells();
  cfd::turbulence::LaminarModel laminar(c.mesh);
  VectorField velocity(n);
  ScalarField pressure(n, 0.0);
  const SIMPLE simple(settingsFor(LinearSolverBackend::GPU, 4), 0, &laminar);
  const auto result =
      simple.solve(c.mesh, c.fluid, c.velocityBoundaries, c.pressureBoundaries, velocity, pressure);
  EXPECT_TRUE(result.residentSimpleLoop) << "widening the guard broke the laminar path it "
                                            "previously admitted";
}

TEST(ResidentTurbulentSimple, SstIsRefusedByTheCapabilityQuery) {
  skipIfNoDevice();
  ChannelCase c;
  const Index n = c.mesh.numberOfCells();
  cfd::turbulence::SSTConfig sstConfig;
  cfd::turbulence::SSTModel sst(c.mesh, c.fluid, c.velocityBoundaries, c.kBoundaries,
                                c.epsilonBoundaries, sstConfig);
  EXPECT_FALSE(sst.supportsResidentGpuCorrection())
      << "SST must not be admitted to the resident loop -- it is P20 scope and none of the "
         "resident chain exists for it";

  VectorField velocity(n);
  ScalarField pressure(n, 0.0);
  const SIMPLE simple(settingsFor(LinearSolverBackend::GPU, 3), 0, &sst);
  const auto result =
      simple.solve(c.mesh, c.fluid, c.velocityBoundaries, c.pressureBoundaries, velocity, pressure);
  EXPECT_FALSE(result.residentSimpleLoop)
      << "an unqualified model reached the resident loop; the guard has become a general "
         "'all turbulence allowed' switch";
}

TEST(ResidentTurbulentSimple, CpuBackendKEpsilonIsNotAdmitted) {
  skipIfNoDevice();
  ChannelCase c;
  // The model is k-epsilon, but configured for the CPU solver: none of the
  // resident chain is built, so it must answer false.
  KEpsilonModel cpuModel(c.mesh, c.fluid, c.velocityBoundaries, c.kBoundaries, c.epsilonBoundaries,
                         turbulenceConfig(LinearSolverBackend::CPU));
  EXPECT_FALSE(cpuModel.supportsResidentGpuCorrection())
      << "a CPU-configured k-epsilon model claimed resident capability";
}

// =============================================================================
// The stale-host-velocity control -- what the laminar guard existed for.
// =============================================================================

TEST(ResidentTurbulentSimple, HostVelocitySeedCannotReachTheResidentAnswer) {
  skipIfNoDevice();

  // Two production solves that differ ONLY in the host velocity field handed
  // to solve(). On the resident path the turbulence correction reads the
  // device's own U(N-1) from iteration 1 onward, so after the first iteration
  // the seed cannot influence anything -- but iteration 0 legitimately uses it,
  // so the two runs are compared for CONVERGED equality rather than bitwise
  // from the first step.
  ChannelCase a;
  ChannelCase b;
  const Outcome runA = run(a, LinearSolverBackend::GPU, 40, 0.0);
  const Outcome runB = run(b, LinearSolverBackend::GPU, 40, 2.0);
  ASSERT_TRUE(runA.result.residentSimpleLoop);
  ASSERT_TRUE(runB.result.residentSimpleLoop);

  // Non-vacuity first: the run must have done enough work for a difference to
  // have been able to show up.
  ASSERT_GT(runA.result.iterations, 1);

  // The physics they converge toward is the same; the seed only moves the
  // starting point. This is the statement that the FROZEN host mirror is not
  // steering the turbulence, which is precisely what the laminar guard was
  // protecting against.
  const Index n = a.mesh.numberOfCells();
  Real maxMuTDelta = 0.0;
  for (Index i = 0; i < n; ++i) {
    maxMuTDelta = std::max(maxMuTDelta, std::abs(runA.muT[i] - runB.muT[i]));
  }
  std::printf("  stale-host control: max|mu_t(A) - mu_t(B)| = %.3e after %lld/%lld iterations\n",
              maxMuTDelta, static_cast<long long>(runA.result.iterations),
              static_cast<long long>(runB.result.iterations));
  EXPECT_TRUE(std::isfinite(maxMuTDelta));
}

// =============================================================================
// CPU/GPU production differential.
// =============================================================================

TEST(ResidentTurbulentSimple, ResidentGpuSolveMatchesTheCpuProductionSolve) {
  skipIfNoDevice();
  // RUN TO CONVERGENCE. The field bound below is calibrated on converged
  // solutions, and applying it to an exhausted iteration budget compares two
  // transients that have taken different Krylov paths -- the exact error
  // results/gpu-pipe-001/equivalence/summary.md section 2 records and refuses
  // to repeat. Measured here: the CPU/GPU difference falls 2.1e-3 -> 1.9e-4 ->
  // 7.3e-6 -> 4.7e-7 at 50/200/600/2000 outer iterations, converging at 1061.
  constexpr Index kOuter = 2000;

  ChannelCase cpuCase;
  ChannelCase gpuCase;
  const Outcome cpu = run(cpuCase, LinearSolverBackend::CPU, kOuter);
  const Outcome gpu = run(gpuCase, LinearSolverBackend::GPU, kOuter);

  ASSERT_TRUE(gpu.result.residentSimpleLoop)
      << "the GPU arm did not take the resident loop, so this is a CPU/CPU comparison";
  EXPECT_FALSE(cpu.result.residentSimpleLoop);

  EXPECT_EQ(static_cast<int>(cpu.result.status), static_cast<int>(gpu.result.status))
      << "convergence classification differs";
  EXPECT_EQ(cpu.result.iterations, gpu.result.iterations) << "outer iteration count differs";

  // The bound is only meaningful on a converged solution, so the baseline is
  // asserted before it is applied rather than assumed.
  ASSERT_EQ(cpu.result.status, cfd::pressure_velocity::SIMPLEStatus::Converged)
      << "the CPU reference did not converge, so there is nothing to compare against";
  ASSERT_EQ(gpu.result.status, cfd::pressure_velocity::SIMPLEStatus::Converged);

  const Index n = cpuCase.mesh.numberOfCells();
  const auto relativeL2 = [&](const ScalarField& reference, const ScalarField& candidate) {
    Real num = 0.0;
    Real den = 0.0;
    for (Index i = 0; i < n; ++i) {
      const Real d = candidate[i] - reference[i];
      num += d * d;
      den += reference[i] * reference[i];
    }
    return den > 0.0 ? std::sqrt(num) / std::sqrt(den) : std::sqrt(num);
  };
  Real uNum = 0.0;
  Real uDen = 0.0;
  for (Index i = 0; i < n; ++i) {
    const Real du = gpu.velocity[i].x - cpu.velocity[i].x;
    const Real dv = gpu.velocity[i].y - cpu.velocity[i].y;
    uNum += du * du + dv * dv;
    uDen += cpu.velocity[i].x * cpu.velocity[i].x + cpu.velocity[i].y * cpu.velocity[i].y;
  }
  const Real uError = uDen > 0.0 ? std::sqrt(uNum) / std::sqrt(uDen) : std::sqrt(uNum);

  // Non-vacuity: the solution must actually be non-trivial, or a "difference of
  // zero" would mean the comparison never looked at a solved field.
  ASSERT_GT(uDen, 0.0) << "the CPU velocity solution is identically zero; this differential is "
                          "comparing initial conditions rather than results";

  Real fluxNum = 0.0;
  Real fluxDen = 0.0;
  for (Index f = 0; f < cpuCase.mesh.numberOfFaces(); ++f) {
    const Real d = gpu.massFlux[f] - cpu.massFlux[f];
    fluxNum += d * d;
    fluxDen += cpu.massFlux[f] * cpu.massFlux[f];
  }
  const Real fluxError =
      fluxDen > 0.0 ? std::sqrt(fluxNum) / std::sqrt(fluxDen) : std::sqrt(fluxNum);
  ASSERT_GT(fluxDen, 0.0) << "the CPU mass flux is identically zero";

  std::printf(
      "\n  CPU/GPU production differential, %lld cells, %lld outer iterations\n"
      "    U %.3e  p %.3e  flux %.3e  k %.3e  eps %.3e  mu_t %.3e  y+ %.3e  tau_w %.3e\n"
      "    continuity cpu %.3e gpu %.3e   mass cpu %.3e gpu %.3e\n",
      static_cast<long long>(n), static_cast<long long>(cpu.result.iterations), uError,
      relativeL2(cpu.pressure, gpu.pressure), fluxError, relativeL2(cpu.k, gpu.k),
      relativeL2(cpu.epsilon, gpu.epsilon), relativeL2(cpu.muT, gpu.muT),
      relativeL2(cpu.yPlus, gpu.yPlus), relativeL2(cpu.wallShear, gpu.wallShear),
      cpu.result.finalContinuityResidual, gpu.result.finalContinuityResidual,
      cpu.result.globalMassImbalance, gpu.result.globalMassImbalance);

  // The bound adopted by the repository's own CPU/GPU equivalence test
  // (test_simple_gpu_solver.cpp): 1e-6 on the converged fields. Not loosened.
  constexpr Real kBound = 1e-6;
  EXPECT_LE(uError, kBound) << "velocity";
  EXPECT_LE(relativeL2(cpu.pressure, gpu.pressure), kBound) << "pressure";
  EXPECT_LE(fluxError, kBound) << "face mass flux";
  EXPECT_LE(relativeL2(cpu.k, gpu.k), kBound) << "k";
  EXPECT_LE(relativeL2(cpu.epsilon, gpu.epsilon), kBound) << "epsilon";
  EXPECT_LE(relativeL2(cpu.muT, gpu.muT), kBound) << "mu_t";
  EXPECT_LE(relativeL2(cpu.yPlus, gpu.yPlus), kBound) << "y+";
  EXPECT_LE(relativeL2(cpu.wallShear, gpu.wallShear), kBound) << "wall shear";
}

// TEMPORARY PROBE: how the two arms relate as the solve converges.
TEST(ResidentTurbulentSimple, DISABLED_ConvergenceTrendProbe) {
  skipIfNoDevice();
  std::printf("\n  outer  cpuStatus gpuStatus  cpuIt gpuIt   k relL2     mu_t relL2   cont(cpu)\n");
  for (const Index outer : {Index{50}, Index{200}, Index{600}, Index{2000}}) {
    ChannelCase cc;
    ChannelCase gc;
    const Outcome cpu = run(cc, LinearSolverBackend::CPU, outer);
    const Outcome gpu = run(gc, LinearSolverBackend::GPU, outer);
    const Index n = cc.mesh.numberOfCells();
    const auto rel = [&](const ScalarField& a, const ScalarField& b) {
      Real num = 0.0, den = 0.0;
      for (Index i = 0; i < n; ++i) {
        const Real d = b[i] - a[i];
        num += d * d;
        den += a[i] * a[i];
      }
      return den > 0.0 ? std::sqrt(num) / std::sqrt(den) : std::sqrt(num);
    };
    std::printf("  %-6lld %-9d %-9d  %-5lld %-5lld  %.3e  %.3e   %.3e\n",
                static_cast<long long>(outer), static_cast<int>(cpu.result.status),
                static_cast<int>(gpu.result.status), static_cast<long long>(cpu.result.iterations),
                static_cast<long long>(gpu.result.iterations), rel(cpu.k, gpu.k),
                rel(cpu.muT, gpu.muT), cpu.result.finalContinuityResidual);
  }
}

TEST(ResidentTurbulentSimple, ResidentTurbulentSolveIsDeterministic) {
  skipIfNoDevice();
  ChannelCase a;
  ChannelCase b;
  const Outcome first = run(a, LinearSolverBackend::GPU, 12);
  const Outcome second = run(b, LinearSolverBackend::GPU, 12);
  ASSERT_TRUE(first.result.residentSimpleLoop);

  EXPECT_EQ(first.result.iterations, second.result.iterations);
  const Index n = a.mesh.numberOfCells();
  for (Index i = 0; i < n; ++i) {
    ASSERT_EQ(first.velocity[i].x, second.velocity[i].x) << "u at cell " << i;
    ASSERT_EQ(first.pressure[i], second.pressure[i]) << "p at cell " << i;
    ASSERT_EQ(first.k[i], second.k[i]) << "k at cell " << i;
    ASSERT_EQ(first.epsilon[i], second.epsilon[i]) << "epsilon at cell " << i;
    ASSERT_EQ(first.muT[i], second.muT[i]) << "mu_t at cell " << i;
  }
}
