// P16-GPU-TURB Stage 6 -- one complete k-epsilon correction, CPU against GPU.
//
// THE CPU REFERENCE IS KEpsilonModel::correct ITSELF. Not a replica, not a
// re-composition of the same calls in a test: the shipped model, driven with
// the same initial state and the same velocity field. Every earlier stage
// compared one operator; this stage compares the WHOLE UPDATE, which is the
// only thing that can catch an ordering defect.
//
// WHAT ONE CORRECTION CANNOT CATCH
// --------------------------------
// The production order lags both equations against the SAME pre-update state:
// the epsilon equation reads k_OLD, not the k that was just solved, and mu_t is
// recomputed from the fresh fields only at the very end. Publishing k early is
// the obvious "optimisation" and it is a different model.
//
// A single correction cannot see that. With one correction there is no next
// iteration to be poisoned by a prematurely published field -- the comparison
// ends before the damage would appear. So MultipleCorrectionsStayInStep runs a
// chain where correction N's output is correction N+1's input, which is the
// only configuration in which a publication-order defect becomes visible.
//
// THE FROZEN GATE
//   6D  wall P_k replacement   the k/epsilon sources must consume the REPLACED
//                              production; a control bypasses the replacement
//                              and must be detected
//   6F  one correction         k, epsilon, mu_t vs the production model
//   6G  N corrections          the same, after each of 5 chained corrections
//   6I  provenance             5 of 8 operations Gpu; WallTreatment, KSolve and
//                              EpsilonSolve honestly Cpu, and allOnGpu FALSE
//
// Bounds are inherited, not invented: the transport-derived fields carry P14's
// frozen 2.1e-12 relative L2; mu_t is cell-local algebra over those fields and
// is compared on the same basis rather than a looser one.
#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <memory>
#include <string>
#include <vector>

#include "cfd/algebra/BiCGSTAB.hpp"
#include "cfd/boundary/BoundaryCondition.hpp"
#include "cfd/boundary/FixedGradient.hpp"
#include "cfd/boundary/FixedValue.hpp"
#include "cfd/boundary/Inlet.hpp"
#include "cfd/boundary/Outlet.hpp"
#include "cfd/boundary/Wall.hpp"
#include "cfd/discretization/VectorGradient.hpp"
#include "cfd/fields/ScalarField.hpp"
#include "cfd/fields/VectorField.hpp"
#include "cfd/gpu/DeviceKEpsilon.hpp"
#include "cfd/gpu/GPUBackend.hpp"
#include "cfd/mesh/Cell.hpp"
#include "cfd/mesh/Mesh.hpp"
#include "cfd/mesh/MeshGeometry.hpp"
#include "cfd/physics/FluidProperties.hpp"
#include "cfd/physics/MassFlux.hpp"
#include "cfd/turbulence/KEpsilonModel.hpp"
#include "cfd/turbulence/WallTreatment.hpp"

using cfd::Index;
using cfd::Real;
using cfd::boundary::BoundaryConditionSet;
using cfd::fields::ScalarField;
using cfd::fields::VectorField;
using cfd::mesh::Mesh;
using cfd::mesh::MeshGeometry;

namespace {

bool deviceUnavailable() { return !cfd::gpu::cudaAvailable(); }

constexpr Real kDensity = 1.225;
constexpr Real kViscosity = 1.8e-5;

struct Harness {
  Mesh mesh;
  cfd::physics::FluidProperties fluid;
  BoundaryConditionSet velocityBoundaries;
  BoundaryConditionSet kBoundaries;
  BoundaryConditionSet epsilonBoundaries;
  VectorField velocity;
  cfd::turbulence::KEpsilonConfig config;

  Harness(Index nx, Index ny, bool wallFunctions)
      : mesh(MeshGeometry::createCartesian2D(nx, ny, 1.0, 0.4)),
        fluid(kDensity, kViscosity),
        velocity(mesh.numberOfCells()) {
    velocityBoundaries.set(mesh, "left",
                           std::make_unique<cfd::boundary::Inlet>(cfd::Vector2{2.0, 0.0}));
    velocityBoundaries.set(mesh, "right", std::make_unique<cfd::boundary::Outlet>());
    velocityBoundaries.set(mesh, "bottom", std::make_unique<cfd::boundary::Wall>());
    velocityBoundaries.set(mesh, "top", std::make_unique<cfd::boundary::Wall>());

    kBoundaries.set(mesh, "left", std::make_unique<cfd::boundary::FixedValue>(0.06));
    kBoundaries.set(mesh, "right", std::make_unique<cfd::boundary::FixedGradient>(0.0));
    kBoundaries.set(mesh, "bottom", std::make_unique<cfd::boundary::FixedGradient>(0.0));
    kBoundaries.set(mesh, "top", std::make_unique<cfd::boundary::FixedGradient>(0.0));

    epsilonBoundaries.set(mesh, "left", std::make_unique<cfd::boundary::FixedValue>(0.9));
    epsilonBoundaries.set(mesh, "right", std::make_unique<cfd::boundary::FixedGradient>(0.0));
    epsilonBoundaries.set(mesh, "bottom", std::make_unique<cfd::boundary::FixedGradient>(0.0));
    epsilonBoundaries.set(mesh, "top", std::make_unique<cfd::boundary::FixedGradient>(0.0));

    // A sheared profile, so the strain rate is real and the wall cells see a
    // genuine tangential velocity.
    for (Index i = 0; i < mesh.numberOfCells(); ++i) {
      const Real y = mesh.cell(i).centroid().y;
      velocity[i] = {2.0 * (1.0 - ((y - 0.2) * (y - 0.2)) / 0.04) + 0.15, 0.03 * y, 0.0};
    }

    config.initialK = 0.05;
    config.initialEpsilon = 0.8;
    config.wallFunctions = wallFunctions;

    // DIAGNOSTIC EXPERIMENT (see the Stage 6 evidence). The default solver
    // tolerance is 1e-10 relative, and a chained comparison at that tolerance
    // measures WHERE THE SOLVER STOPPED, not what the discretization computed:
    // once the CPU and device matrices differ by 1-2 ulp, one arm can satisfy
    // the stopping test an iteration before the other, and the two solutions
    // then differ by the tolerance rather than by machine epsilon.
    config.kSolver.relativeTolerance = 1e-14;
    config.kSolver.absoluteTolerance = 1e-16;
    config.kSolver.maxIterations = 5000;
    config.epsilonSolver = config.kSolver;
  }
};

// Uploads the host wall treatment's outputs. The treatment itself stays on the
// host -- see DeviceKEpsilon.hpp for why, and the evidence for its cost.
void buildDeviceWall(const Harness& h, const ScalarField& k, cfd::gpu::DeviceWallTreatment& wall) {
  if (!h.config.wallFunctions) return;
  const auto wallFaces = cfd::turbulence::collectWallFaces(h.mesh, h.velocityBoundaries);
  const auto treatment = cfd::turbulence::computeWallTreatment(
      h.mesh, wallFaces, h.velocity, k, h.fluid.density(), h.fluid.dynamicViscosity(),
      h.config.wallFunctionCoefficients);
  const Index count = static_cast<Index>(treatment.wallCells.size());
  wall.wallCells.uploadFrom(treatment.wallCells.data(), count);
  wall.wallCellProduction.uploadFrom(treatment.wallCellProduction.data(), count);
  cfd::gpu::uploadImposedCellsDevice(treatment.wallCells, treatment.wallCellDissipation,
                                     h.mesh.numberOfCells(), wall.imposedEpsilon);
}

cfd::gpu::DeviceKEpsilonConfig deviceConfig(const Harness& h) {
  cfd::gpu::DeviceKEpsilonConfig c;
  c.density = h.fluid.density();
  c.dynamicViscosity = h.fluid.dynamicViscosity();
  c.cMu = h.config.coefficients.cMu;
  c.c1Epsilon = h.config.coefficients.c1Epsilon;
  c.c2Epsilon = h.config.coefficients.c2Epsilon;
  c.sigmaK = h.config.coefficients.sigmaK;
  c.sigmaEpsilon = h.config.coefficients.sigmaEpsilon;
  c.kFloor = h.config.kFloor;
  c.epsilonFloor = h.config.epsilonFloor;
  c.kRelaxation = h.config.kRelaxation;
  c.epsilonRelaxation = h.config.epsilonRelaxation;
  c.kSolver = h.config.kSolver;
  c.epsilonSolver = h.config.epsilonSolver;
  return c;
}

std::vector<Real> download(const cfd::gpu::DeviceBuffer<Real>& b) {
  std::vector<Real> host(static_cast<std::size_t>(b.size()), 0.0);
  if (!host.empty()) b.downloadTo(host.data(), b.size());
  return host;
}

Real relativeL2(const ScalarField& host, const std::vector<Real>& device) {
  Real numerator = 0.0;
  Real denominator = 0.0;
  for (Index i = 0; i < host.size(); ++i) {
    const Real diff = device[static_cast<std::size_t>(i)] - host[i];
    numerator += diff * diff;
    denominator += host[i] * host[i];
  }
  return std::sqrt(numerator) / std::sqrt(denominator);
}

// Runs one device correction against the state, returning the diagnostics.
cfd::gpu::DeviceKEpsilonCorrection runDeviceCorrection(const Harness& h,
                                                       cfd::gpu::DeviceKEpsilonState& state,
                                                       bool bypassWallProduction = false) {
  const Index n = h.mesh.numberOfCells();
  // ONE PLAN PAIR PER EQUATION. k and epsilon have different boundary sets and
  // the plans bake the encoded boundary values in, so sharing them silently
  // applies k's inlet value to epsilon.
  cfd::gpu::DeviceDiffusionPlan kDiffusion;
  cfd::gpu::DeviceConvectionPlan kConvection;
  cfd::gpu::DeviceDiffusionPlan epsilonDiffusion;
  cfd::gpu::DeviceConvectionPlan epsilonConvection;
  if (!kDiffusion.build(h.mesh, h.kBoundaries) || !kConvection.build(h.mesh, h.kBoundaries) ||
      !epsilonDiffusion.build(h.mesh, h.epsilonBoundaries) ||
      !epsilonConvection.build(h.mesh, h.epsilonBoundaries)) {
    throw std::runtime_error("device plans unusable: " + kDiffusion.unsupportedReason() +
                             epsilonDiffusion.unsupportedReason());
  }
  cfd::gpu::DeviceKEpsilonPlans plans;
  plans.kDiffusion = &kDiffusion;
  plans.kConvection = &kConvection;
  plans.epsilonDiffusion = &epsilonDiffusion;
  plans.epsilonConvection = &epsilonConvection;

  const auto massFlux =
      cfd::physics::calculateMassFlux(h.mesh, h.velocity, h.fluid, h.velocityBoundaries);
  const auto gradient =
      cfd::discretization::computeVelocityGradient(h.mesh, h.velocity, h.velocityBoundaries);

  cfd::gpu::DeviceVelocityGradient deviceGradient;
  std::vector<Real> ux(n), uy(n), vx(n), vy(n);
  for (Index i = 0; i < n; ++i) {
    ux[i] = gradient.gradU[i].x;
    uy[i] = gradient.gradU[i].y;
    vx[i] = gradient.gradV[i].x;
    vy[i] = gradient.gradV[i].y;
  }
  deviceGradient.gradUx.uploadFrom(ux.data(), n);
  deviceGradient.gradUy.uploadFrom(uy.data(), n);
  deviceGradient.gradVx.uploadFrom(vx.data(), n);
  deviceGradient.gradVy.uploadFrom(vy.data(), n);

  cfd::gpu::DeviceBuffer<Real> deviceFlux;
  deviceFlux.uploadFrom(massFlux.data(), h.mesh.numberOfFaces());
  std::vector<Real> volumes(static_cast<std::size_t>(n));
  for (Index i = 0; i < n; ++i) volumes[static_cast<std::size_t>(i)] = h.mesh.cell(i).volume();
  cfd::gpu::DeviceBuffer<Real> deviceVolumes;
  deviceVolumes.uploadFrom(volumes.data(), n);

  const std::vector<Real> kHost = download(state.k);
  ScalarField kField(n);
  for (Index i = 0; i < n; ++i) kField[i] = kHost[static_cast<std::size_t>(i)];
  cfd::gpu::DeviceWallTreatment wall;
  buildDeviceWall(h, kField, wall);
  if (bypassWallProduction) {
    // THE CONTROL: keep the imposed epsilon rows but drop the production
    // replacement, which is the defect a device path would have if it
    // recomputed mu_t*S^2 after the wall treatment.
    wall.wallCells.resize(0);
    wall.wallCellProduction.resize(0);
  }

  cfd::gpu::DeviceKEpsilonCorrection correction;
  cfd::gpu::correctKEpsilonDevice(plans, deviceVolumes, deviceGradient, deviceFlux, wall,
                                  deviceConfig(h), state, correction);
  return correction;
}

void seedDeviceState(const cfd::turbulence::KEpsilonModel& model, Index n,
                     cfd::gpu::DeviceKEpsilonState& state) {
  state.k.uploadFrom(model.k().data(), n);
  state.epsilon.uploadFrom(model.epsilon().data(), n);
  state.turbulentViscosity.uploadFrom(model.turbulentViscosity().data(), n);
}

}  // namespace

// =============================================================================
// 6F -- one complete correction.
// =============================================================================

TEST(DeviceKEpsilonCorrection, OneCorrectionMatchesTheProductionModel) {
  if (deviceUnavailable()) GTEST_SKIP() << "no usable CUDA device at runtime";

  for (const bool wallFunctions : {false, true}) {
    const Harness h(12, 8, wallFunctions);
    const Index n = h.mesh.numberOfCells();

    cfd::turbulence::KEpsilonModel model(h.mesh, h.fluid, h.velocityBoundaries, h.kBoundaries,
                                         h.epsilonBoundaries, h.config);
    cfd::gpu::DeviceKEpsilonState state;
    seedDeviceState(model, n, state);

    model.correct(h.mesh, h.velocity, ScalarField(n, 0.0));
    const auto correction = runDeviceCorrection(h, state);

    const std::string label = wallFunctions ? "wall functions ON" : "wall functions OFF";
    const Real kError = relativeL2(model.k(), download(state.k));
    const Real epsilonError = relativeL2(model.epsilon(), download(state.epsilon));
    const Real muTError =
        relativeL2(model.turbulentViscosity(), download(state.turbulentViscosity));

    EXPECT_LE(kError, 2.1e-12) << label << " k relative L2 " << kError;
    EXPECT_LE(epsilonError, 2.1e-12) << label << " epsilon relative L2 " << epsilonError;
    EXPECT_LE(muTError, 2.1e-12) << label << " mu_t relative L2 " << muTError;
    std::printf("  %-20s k %.3e  epsilon %.3e  mu_t %.3e\n", label.c_str(), kError, epsilonError,
                muTError);

    ASSERT_TRUE(model.convergenceResidual().has_value());
    EXPECT_NEAR(correction.convergenceResidual, *model.convergenceResidual(),
                1e-9 * std::max(1.0, *model.convergenceResidual()))
        << label << " convergence residual";
  }
}

// =============================================================================
// 6G -- the chain. This is what catches an ordering defect.
// =============================================================================

TEST(DeviceKEpsilonCorrection, MultipleCorrectionsStayInStep) {
  if (deviceUnavailable()) GTEST_SKIP() << "no usable CUDA device at runtime";

  const Harness h(12, 8, /*wallFunctions=*/true);
  const Index n = h.mesh.numberOfCells();

  cfd::turbulence::KEpsilonModel model(h.mesh, h.fluid, h.velocityBoundaries, h.kBoundaries,
                                       h.epsilonBoundaries, h.config);
  cfd::gpu::DeviceKEpsilonState state;
  seedDeviceState(model, n, state);

  // Five chained corrections. A stale mu_t, a prematurely published k, or a
  // forgotten re-assertion does not necessarily show up in correction 1 -- it
  // compounds. The per-correction assertion means the FIRST diverging
  // correction is named, not the last.
  for (int correctionIndex = 1; correctionIndex <= 5; ++correctionIndex) {
    model.correct(h.mesh, h.velocity, ScalarField(n, 0.0));
    runDeviceCorrection(h, state);

    const Real kError = relativeL2(model.k(), download(state.k));
    const Real epsilonError = relativeL2(model.epsilon(), download(state.epsilon));
    const Real muTError =
        relativeL2(model.turbulentViscosity(), download(state.turbulentViscosity));
    std::printf("  correction %d: k %.3e  epsilon %.3e  mu_t %.3e\n", correctionIndex, kError,
                epsilonError, muTError);

    EXPECT_LE(kError, 2.1e-12) << "correction " << correctionIndex << " k";
    EXPECT_LE(epsilonError, 2.1e-12) << "correction " << correctionIndex << " epsilon";
    EXPECT_LE(muTError, 2.1e-12) << "correction " << correctionIndex << " mu_t";
  }

  // The state must actually have MOVED over five corrections, otherwise the
  // chain compared a fixed point to itself and proves nothing about ordering.
  cfd::turbulence::KEpsilonModel fresh(h.mesh, h.fluid, h.velocityBoundaries, h.kBoundaries,
                                       h.epsilonBoundaries, h.config);
  Real maxMovement = 0.0;
  for (Index i = 0; i < n; ++i) {
    maxMovement = std::max(maxMovement, std::abs(model.k()[i] - fresh.k()[i]));
  }
  EXPECT_GT(maxMovement, 1e-6)
      << "k barely moved over five corrections, so the chain is a fixed point and cannot "
         "discriminate an ordering defect";
}

// =============================================================================
// 6D -- the wall production replacement must be load-bearing.
// =============================================================================

TEST(DeviceKEpsilonCorrection, BypassingTheWallProductionReplacementIsDetected) {
  if (deviceUnavailable()) GTEST_SKIP() << "no usable CUDA device at runtime";

  const Harness h(12, 8, /*wallFunctions=*/true);
  const Index n = h.mesh.numberOfCells();

  cfd::turbulence::KEpsilonModel model(h.mesh, h.fluid, h.velocityBoundaries, h.kBoundaries,
                                       h.epsilonBoundaries, h.config);
  cfd::gpu::DeviceKEpsilonState correctState;
  seedDeviceState(model, n, correctState);
  cfd::gpu::DeviceKEpsilonState bypassedState;
  seedDeviceState(model, n, bypassedState);

  model.correct(h.mesh, h.velocity, ScalarField(n, 0.0));
  runDeviceCorrection(h, correctState);
  runDeviceCorrection(h, bypassedState, /*bypassWallProduction=*/true);

  // The correct path agrees with production.
  EXPECT_LE(relativeL2(model.k(), download(correctState.k)), 2.1e-12);

  // The bypassed one must NOT. If it did, the wall treatment's production
  // replacement would be doing nothing, and every "wall functions are active"
  // claim in P16 would rest on an operation with no effect.
  const Real bypassedError = relativeL2(model.k(), download(bypassedState.k));
  EXPECT_GT(bypassedError, 2.1e-12)
      << "dropping the wall production replacement changed nothing, so the replacement is inert "
         "and the gate cannot see it";
  std::printf("  bypassed wall production: k relative L2 = %.3e (must exceed 2.1e-12)\n",
              bypassedError);
}

// =============================================================================
// 6I -- provenance. Five of eight, honestly.
// =============================================================================

TEST(DeviceKEpsilonCorrection, TraceReportsExactlyWhatRanOnTheDevice) {
  if (deviceUnavailable()) GTEST_SKIP() << "no usable CUDA device at runtime";

  const Harness h(12, 8, /*wallFunctions=*/true);
  const Index n = h.mesh.numberOfCells();
  cfd::turbulence::KEpsilonModel model(h.mesh, h.fluid, h.velocityBoundaries, h.kBoundaries,
                                       h.epsilonBoundaries, h.config);
  cfd::gpu::DeviceKEpsilonState state;
  seedDeviceState(model, n, state);

  cfd::gpu::clearForcedCpuFallbacksForTesting();
  auto& trace = cfd::gpu::turbulenceBackendTrace();
  trace.reset();
  runDeviceCorrection(h, state);

  using cfd::gpu::ExecutionBackend;
  using cfd::gpu::TurbulenceOperation;

  // What this composition genuinely executes on the device.
  for (const auto op : {TurbulenceOperation::StrainRate, TurbulenceOperation::Production,
                        TurbulenceOperation::TurbulentViscosity}) {
    EXPECT_EQ(trace.backendFor(op), ExecutionBackend::Gpu)
        << cfd::gpu::turbulenceOperationName(op) << " should have run on the device";
  }

  // The assembly and the solve now genuinely run on the device: the CSR is
  // adopted in place by solveBiCGSTABResident and never comes down.
  for (const auto op : {TurbulenceOperation::KAssembly, TurbulenceOperation::EpsilonAssembly,
                        TurbulenceOperation::KSolve, TurbulenceOperation::EpsilonSolve}) {
    EXPECT_EQ(trace.backendFor(op), ExecutionBackend::Gpu)
        << cfd::gpu::turbulenceOperationName(op) << " should have run on the device:\n"
        << trace.describe();
  }

  // The wall treatment does NOT, by an explicit P16 scope decision. Recording
  // it as Gpu would be the exact failure mode this facility exists to prevent.
  EXPECT_EQ(trace.backendFor(TurbulenceOperation::WallTreatment), ExecutionBackend::NotAttempted)
      << "the wall treatment is host-side in P16 and must not claim the device";

  // P16's requirement: the seven GPU-path operations, WallTreatment excepted.
  EXPECT_TRUE(cfd::gpu::turbulenceGpuPathQualified(trace))
      << "the P16 GPU turbulence path is not qualified:\n"
      << trace.describe();

  const TurbulenceOperation all[] = {
      TurbulenceOperation::StrainRate,         TurbulenceOperation::Production,
      TurbulenceOperation::KAssembly,          TurbulenceOperation::KSolve,
      TurbulenceOperation::EpsilonAssembly,    TurbulenceOperation::EpsilonSolve,
      TurbulenceOperation::TurbulentViscosity, TurbulenceOperation::WallTreatment};
  // allOnGpu() is a DIFFERENT question from turbulenceGpuPathQualified() and
  // still returns false, because the wall treatment is host-side. P16 does not
  // require it. A caller who wants "everything on the device" keeps asking
  // this one and keeps getting the honest answer.
  EXPECT_FALSE(trace.allOnGpu(all, 8))
      << "allOnGpu() passed while the wall treatment is host-side:\n"
      << trace.describe();
}

// =============================================================================
// 7C -- the plan-sharing control. Non-negotiable after the Stage 6 failure.
// =============================================================================

TEST(DeviceKEpsilonCorrection, SharingTheKPlanWithEpsilonIsDetected) {
  if (deviceUnavailable()) GTEST_SKIP() << "no usable CUDA device at runtime";

  // Stage 6 shipped a composition that used ONE plan pair for both equations.
  // k matched the production model to 0 ulp and epsilon was out by 23.5%,
  // because a DeviceDiffusionPlan bakes the encoded boundary values in at
  // build() time and k's inlet value was being applied to epsilon. Every
  // isolated Stage 4/5 gate had passed.
  //
  // This control reproduces that defect deliberately and requires the gate to
  // catch it. Without it, a later refactor could quietly reintroduce the
  // sharing and only the physics would know.
  const Harness h(12, 8, /*wallFunctions=*/true);
  const Index n = h.mesh.numberOfCells();

  cfd::turbulence::KEpsilonModel model(h.mesh, h.fluid, h.velocityBoundaries, h.kBoundaries,
                                       h.epsilonBoundaries, h.config);
  model.correct(h.mesh, h.velocity, ScalarField(n, 0.0));

  cfd::gpu::DeviceDiffusionPlan kDiffusion;
  cfd::gpu::DeviceConvectionPlan kConvection;
  ASSERT_TRUE(kDiffusion.build(h.mesh, h.kBoundaries));
  ASSERT_TRUE(kConvection.build(h.mesh, h.kBoundaries));

  // THE DEFECT: k's plans used for epsilon too.
  cfd::gpu::DeviceKEpsilonPlans shared;
  shared.kDiffusion = &kDiffusion;
  shared.kConvection = &kConvection;
  shared.epsilonDiffusion = &kDiffusion;
  shared.epsilonConvection = &kConvection;

  cfd::gpu::DeviceKEpsilonState state;
  cfd::turbulence::KEpsilonModel seed(h.mesh, h.fluid, h.velocityBoundaries, h.kBoundaries,
                                      h.epsilonBoundaries, h.config);
  seedDeviceState(seed, n, state);

  const auto massFlux =
      cfd::physics::calculateMassFlux(h.mesh, h.velocity, h.fluid, h.velocityBoundaries);
  const auto gradient =
      cfd::discretization::computeVelocityGradient(h.mesh, h.velocity, h.velocityBoundaries);
  cfd::gpu::DeviceVelocityGradient deviceGradient;
  std::vector<Real> ux(n), uy(n), vx(n), vy(n);
  for (Index i = 0; i < n; ++i) {
    ux[i] = gradient.gradU[i].x;
    uy[i] = gradient.gradU[i].y;
    vx[i] = gradient.gradV[i].x;
    vy[i] = gradient.gradV[i].y;
  }
  deviceGradient.gradUx.uploadFrom(ux.data(), n);
  deviceGradient.gradUy.uploadFrom(uy.data(), n);
  deviceGradient.gradVx.uploadFrom(vx.data(), n);
  deviceGradient.gradVy.uploadFrom(vy.data(), n);
  cfd::gpu::DeviceBuffer<Real> deviceFlux;
  deviceFlux.uploadFrom(massFlux.data(), h.mesh.numberOfFaces());
  std::vector<Real> volumes(static_cast<std::size_t>(n));
  for (Index i = 0; i < n; ++i) volumes[static_cast<std::size_t>(i)] = h.mesh.cell(i).volume();
  cfd::gpu::DeviceBuffer<Real> deviceVolumes;
  deviceVolumes.uploadFrom(volumes.data(), n);
  cfd::gpu::DeviceWallTreatment wall;
  buildDeviceWall(h, seed.k(), wall);

  cfd::gpu::DeviceKEpsilonCorrection correction;
  cfd::gpu::correctKEpsilonDevice(shared, deviceVolumes, deviceGradient, deviceFlux, wall,
                                  deviceConfig(h), state, correction);

  // k still matches -- it is using its own plans either way. That is precisely
  // what made the original defect so quiet.
  EXPECT_LE(relativeL2(model.k(), download(state.k)), 2.1e-12)
      << "k should be unaffected by epsilon's plans";

  // epsilon must NOT. If this passed, plan sharing would be undetectable.
  const Real epsilonError = relativeL2(model.epsilon(), download(state.epsilon));
  EXPECT_GT(epsilonError, 2.1e-12)
      << "sharing k's plan with epsilon changed nothing, so the two boundary sets are "
         "indistinguishable here and this control proves nothing";
  std::printf("  shared plans: epsilon relative L2 = %.3e (must exceed 2.1e-12)\n", epsilonError);

  // And a null plan is refused outright rather than crashing later.
  cfd::gpu::DeviceKEpsilonPlans incomplete;
  incomplete.kDiffusion = &kDiffusion;
  incomplete.kConvection = &kConvection;
  cfd::gpu::DeviceKEpsilonState other;
  seedDeviceState(seed, n, other);
  EXPECT_THROW(
      cfd::gpu::correctKEpsilonDevice(incomplete, deviceVolumes, deviceGradient, deviceFlux, wall,
                                      deviceConfig(h), other, correction),
      std::runtime_error);
}

// =============================================================================
// Determinism.
// =============================================================================

TEST(DeviceKEpsilonCorrection, RepeatedCorrectionsAreBitwiseIdentical) {
  if (deviceUnavailable()) GTEST_SKIP() << "no usable CUDA device at runtime";

  const Harness h(12, 8, /*wallFunctions=*/true);
  const Index n = h.mesh.numberOfCells();
  cfd::turbulence::KEpsilonModel model(h.mesh, h.fluid, h.velocityBoundaries, h.kBoundaries,
                                       h.epsilonBoundaries, h.config);

  std::vector<Real> firstK, firstEpsilon, firstMuT;
  for (int pass = 0; pass < 2; ++pass) {
    cfd::gpu::DeviceKEpsilonState state;
    seedDeviceState(model, n, state);
    runDeviceCorrection(h, state);
    if (pass == 0) {
      firstK = download(state.k);
      firstEpsilon = download(state.epsilon);
      firstMuT = download(state.turbulentViscosity);
    } else {
      EXPECT_EQ(firstK, download(state.k));
      EXPECT_EQ(firstEpsilon, download(state.epsilon));
      EXPECT_EQ(firstMuT, download(state.turbulentViscosity));
    }
  }
}
