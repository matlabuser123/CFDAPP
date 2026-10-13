// P16-GPU-TURB Stage 7I step 2 -- the RESIDENT k-epsilon entry point.
//
// WHAT THIS PROVES, AND WHY THE CONTROLS ARE THE POINT
// ----------------------------------------------------
// `correctResident()` takes no host velocity at all, so "it does not read the
// host mirror" is true by construction and asserting it directly would be
// vacuous. What is NOT automatic is that it reads the RIGHT device state, and
// that the resident flux it adopts is the corrected one rather than something
// recomputed.
//
// So the controls are differential:
//
//   host vs device disagree   -> correctResident follows the DEVICE
//                                correct() follows the HOST
//                                the two results differ
//   device velocity perturbed -> the result changes
//   flux substituted          -> the result changes
//
// A test that only checked "resident agrees with CPU when both are fed the
// same state" would pass for an implementation that silently recomputed
// everything from whatever it could reach.
//
// WHAT IS DELIBERATELY NOT HERE
// The compact wall-owner gather is step 3. This file passes the same
// `GpuKEpsilonWallInputs` the non-resident path takes, so the two entry points
// are compared below the wall boundary and step 2 is testable on its own.
#include <gtest/gtest.h>

#include <cmath>
#include <memory>
#include <vector>

#include "cfd/algebra/LinearSolver.hpp"
#include "cfd/boundary/BoundaryCondition.hpp"
#include "cfd/boundary/FixedGradient.hpp"
#include "cfd/boundary/FixedValue.hpp"
#include "cfd/boundary/Inlet.hpp"
#include "cfd/boundary/Outlet.hpp"
#include "cfd/boundary/Wall.hpp"
#include "cfd/fields/ScalarField.hpp"
#include "cfd/fields/VectorField.hpp"
#include "cfd/gpu/DeviceFaceFlux.hpp"
#include "cfd/gpu/GPUBackend.hpp"
#include "cfd/gpu/GpuKEpsilonCorrection.hpp"
#include "cfd/gpu/GpuSimpleResidentTurbulence.hpp"
#include "cfd/mesh/Cell.hpp"
#include "cfd/mesh/Mesh.hpp"
#include "cfd/mesh/MeshGeometry.hpp"
#include "cfd/physics/FluidProperties.hpp"
#include "cfd/physics/MassFlux.hpp"
#include "cfd/turbulence/KEpsilonModel.hpp"

using cfd::Index;
using cfd::Real;
using cfd::boundary::BoundaryConditionSet;
using cfd::fields::ScalarField;
using cfd::fields::VectorField;
using cfd::mesh::Mesh;
using cfd::mesh::MeshGeometry;
using cfd::turbulence::KEpsilonConfig;
using cfd::turbulence::KEpsilonModel;

namespace {

bool deviceUnavailable() { return !cfd::gpu::cudaAvailable(); }

struct Fixture {
  Mesh mesh;
  cfd::physics::FluidProperties fluid;
  BoundaryConditionSet velocityBoundaries;
  BoundaryConditionSet kBoundaries;
  BoundaryConditionSet epsilonBoundaries;
  VectorField velocity;
  cfd::gpu::DeviceMomentumConvectionPlan convectionPlan;

  Fixture()
      : mesh(MeshGeometry::createCartesian2D(12, 8, 1.0, 0.4)),
        fluid(1.225, 1.8e-5),
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
    for (Index i = 0; i < mesh.numberOfCells(); ++i) {
      const Real y = mesh.cell(i).centroid().y;
      velocity[i] = {2.0 * (1.0 - ((y - 0.2) * (y - 0.2)) / 0.04) + 0.15, 0.03 * y, 0.0};
    }
  }

  [[nodiscard]] bool buildPlan() { return convectionPlan.build(mesh, velocityBoundaries); }
  [[nodiscard]] Index cellCount() const { return mesh.numberOfCells(); }
};

KEpsilonConfig config() {
  KEpsilonConfig c;
  c.initialK = 0.05;
  c.initialEpsilon = 0.8;
  c.wallFunctions = false;  // step 3 owns the wall exchange
  c.kSolver.relativeTolerance = 1e-14;
  c.kSolver.absoluteTolerance = 1e-16;
  c.kSolver.maxIterations = 5000;
  c.epsilonSolver = c.kSolver;
  return c;
}

// A resident view built from buffers the test owns, so each input can be
// varied independently. Mirrors exactly what residentTurbulenceInputs()
// borrows from GpuSimpleDiscretization::Impl.
struct ResidentState {
  cfd::gpu::DeviceVelocity velocity;
  cfd::gpu::DeviceBuffer<Real> massFlux;
  // The turbulence convection flux is rebuilt from the resident velocity, so
  // the view has to carry the plan that does it. Built by the caller, because
  // this struct deliberately owns only buffers.
  cfd::gpu::DeviceFaceFluxPlan fluxPlan;

  void uploadVelocity(const VectorField& field, Index n) {
    std::vector<Real> x(static_cast<std::size_t>(n)), y(static_cast<std::size_t>(n)),
        z(static_cast<std::size_t>(n), 0.0);
    for (Index i = 0; i < n; ++i) {
      x[static_cast<std::size_t>(i)] = field[i].x;
      y[static_cast<std::size_t>(i)] = field[i].y;
    }
    velocity.x.uploadFrom(x.data(), n);
    velocity.y.uploadFrom(y.data(), n);
    velocity.z.uploadFrom(z.data(), n);
  }

  [[nodiscard]] cfd::gpu::ResidentTurbulenceInputs view(
      const cfd::gpu::DeviceMomentumConvectionPlan& plan, Index n) const {
    cfd::gpu::ResidentTurbulenceInputs inputs;
    inputs.velocity = &velocity;
    inputs.massFlux = &massFlux;
    inputs.convectionPlan = &plan;
    inputs.fluxPlan = &fluxPlan;
    inputs.cellCount = n;
    return inputs;
  }
};

Real relativeL2(const ScalarField& reference, const ScalarField& candidate) {
  Real numerator = 0.0;
  Real denominator = 0.0;
  for (Index i = 0; i < reference.size(); ++i) {
    const Real diff = candidate[i] - reference[i];
    numerator += diff * diff;
    denominator += reference[i] * reference[i];
  }
  return std::sqrt(numerator) / std::sqrt(denominator);
}

// Runs one resident correction from a freshly seeded model state.
void runResident(Fixture& f, const ResidentState& resident, const KEpsilonConfig& cfg,
                 cfd::gpu::GpuKEpsilonResult& out) {
  KEpsilonModel seed(f.mesh, f.fluid, f.velocityBoundaries, f.kBoundaries, f.epsilonBoundaries,
                     cfg);
  cfd::gpu::GpuKEpsilonCorrection correction;
  ASSERT_TRUE(correction.prepare(f.mesh, f.kBoundaries, f.epsilonBoundaries, seed.k(),
                                 seed.epsilon(), seed.turbulentViscosity()))
      << correction.unsupportedReason();
  const cfd::gpu::GpuKEpsilonWallInputs noWall;
  correction.correctResident(resident.view(f.convectionPlan, f.cellCount()), f.mesh, f.fluid, cfg,
                             noWall, out);
}

}  // namespace

// =============================================================================
// The resident entry point reproduces the CPU model.
// =============================================================================

TEST(ResidentKEpsilonEntry, ResidentEntryPointMatchesTheCpuModel) {
  if (deviceUnavailable()) GTEST_SKIP() << "no usable CUDA device at runtime";

  Fixture f;
  ASSERT_TRUE(f.buildPlan()) << f.convectionPlan.unsupportedReason();
  const Index n = f.cellCount();
  const KEpsilonConfig cfg = config();

  // The resident state IS the host state here, so the two entry points are
  // being fed identical physics and any difference is the implementation.
  ResidentState resident;
  resident.uploadVelocity(f.velocity, n);
  ASSERT_TRUE(resident.fluxPlan.build(f.mesh, f.velocityBoundaries))
      << resident.fluxPlan.unsupportedReason();
  const auto hostFlux =
      cfd::physics::calculateMassFlux(f.mesh, f.velocity, f.fluid, f.velocityBoundaries);
  resident.massFlux.uploadFrom(hostFlux.data(), f.mesh.numberOfFaces());

  KEpsilonModel cpu(f.mesh, f.fluid, f.velocityBoundaries, f.kBoundaries, f.epsilonBoundaries, cfg);
  cpu.correct(f.mesh, f.velocity, ScalarField(n, 0.0));

  cfd::gpu::GpuKEpsilonResult result;
  runResident(f, resident, cfg, result);

  const Real kError = relativeL2(cpu.k(), result.k);
  const Real epsilonError = relativeL2(cpu.epsilon(), result.epsilon);
  const Real muTError = relativeL2(cpu.turbulentViscosity(), result.turbulentViscosity);
  std::printf("  resident vs CPU: k %.3e  epsilon %.3e  mu_t %.3e\n", kError, epsilonError,
              muTError);
  EXPECT_LE(kError, 2.1e-12);
  EXPECT_LE(epsilonError, 2.1e-12);
  EXPECT_LE(muTError, 2.1e-12);
}

// =============================================================================
// 2E -- the data source is the DEVICE field, proven differentially.
// =============================================================================

TEST(ResidentKEpsilonEntry, TheResidentPathFollowsTheDeviceVelocityNotTheHost) {
  if (deviceUnavailable()) GTEST_SKIP() << "no usable CUDA device at runtime";

  Fixture f;
  ASSERT_TRUE(f.buildPlan());
  const Index n = f.cellCount();
  const KEpsilonConfig cfg = config();
  const auto flux =
      cfd::physics::calculateMassFlux(f.mesh, f.velocity, f.fluid, f.velocityBoundaries);

  // Device holds the TRUE velocity.
  ResidentState resident;
  resident.uploadVelocity(f.velocity, n);
  ASSERT_TRUE(resident.fluxPlan.build(f.mesh, f.velocityBoundaries))
      << resident.fluxPlan.unsupportedReason();
  resident.massFlux.uploadFrom(flux.data(), f.mesh.numberOfFaces());
  cfd::gpu::GpuKEpsilonResult fromDevice;
  runResident(f, resident, cfg, fromDevice);

  // Now corrupt the HOST field and run the resident path again with the SAME
  // device state. A resident implementation that secretly reached for a host
  // mirror would move; this one must not.
  Fixture corrupted;
  ASSERT_TRUE(corrupted.buildPlan());
  for (Index i = 0; i < n; ++i) corrupted.velocity[i] = {0.01, -0.02, 0.0};
  ResidentState sameDevice;
  ASSERT_TRUE(sameDevice.fluxPlan.build(f.mesh, f.velocityBoundaries))
      << sameDevice.fluxPlan.unsupportedReason();
  sameDevice.uploadVelocity(f.velocity, n);  // the TRUE velocity, unchanged
  sameDevice.massFlux.uploadFrom(flux.data(), f.mesh.numberOfFaces());
  cfd::gpu::GpuKEpsilonResult stillFromDevice;
  runResident(corrupted, sameDevice, cfg, stillFromDevice);

  for (Index i = 0; i < n; ++i) {
    ASSERT_EQ(stillFromDevice.k[i], fromDevice.k[i])
        << "corrupting the host velocity changed the resident result at cell " << i
        << " -- the resident path is reading a host field";
    ASSERT_EQ(stillFromDevice.epsilon[i], fromDevice.epsilon[i]) << "cell " << i;
  }

  // And the converse: perturb the DEVICE velocity, keep the host field intact.
  // The result MUST change, otherwise the previous assertion passed because
  // the velocity reaches nothing at all.
  VectorField perturbed = f.velocity;
  perturbed[n / 2 + 1] = {perturbed[n / 2 + 1].x * 1.5, perturbed[n / 2 + 1].y - 0.4, 0.0};
  ResidentState movedDevice;
  ASSERT_TRUE(movedDevice.fluxPlan.build(f.mesh, f.velocityBoundaries))
      << movedDevice.fluxPlan.unsupportedReason();
  movedDevice.uploadVelocity(perturbed, n);
  movedDevice.massFlux.uploadFrom(flux.data(), f.mesh.numberOfFaces());
  cfd::gpu::GpuKEpsilonResult afterPerturbation;
  runResident(f, movedDevice, cfg, afterPerturbation);

  bool changed = false;
  for (Index i = 0; i < n; ++i) {
    if (afterPerturbation.k[i] != fromDevice.k[i]) {
      changed = true;
      break;
    }
  }
  EXPECT_TRUE(changed)
      << "perturbing the RESIDENT velocity changed nothing, so the resident velocity is not "
         "reaching the turbulence correction at all and the host-corruption control above is "
         "vacuous";
}

// =============================================================================
// 2D -- the turbulence convection flux is BUILT from the resident velocity.
// =============================================================================
//
// RESTATED. This test used to assert that substituting a different `massFlux`
// changed the result, on the premise that the resident path adopts SIMPLE's
// carried corrected face flux.
//
// That premise was wrong, and the cross-backend continuation gate proved it.
// The CPU reference builds its own convection flux from the cell velocity
// (KEpsilonModel::correctOnHost:234, calculateMassFlux). SIMPLE's carried flux
// is the pressure-corrected Rhie-Chow one and differs from it by 3.231e-06
// relative on the validated channel -- which propagated to k and mu_t at
// 2-4e-06 and broke equivalence. The resident path now rebuilds the same
// quantity the CPU builds, with calculateMassFluxDevice.
//
// So the control is two-sided, and both halves are needed:
//
//   substituting a different carried massFlux  ->  NOTHING changes
//       (proves the turbulence is not reading it)
//   perturbing the resident velocity           ->  the result changes
//       (proves it IS reading the velocity, so the half above is not vacuous)
TEST(ResidentKEpsilonEntry, TurbulenceFluxFollowsVelocityNotTheCarriedFlux) {
  if (deviceUnavailable()) GTEST_SKIP() << "no usable CUDA device at runtime";

  Fixture f;
  ASSERT_TRUE(f.buildPlan());
  const Index n = f.cellCount();
  const KEpsilonConfig cfg = config();
  const auto correctedFlux =
      cfd::physics::calculateMassFlux(f.mesh, f.velocity, f.fluid, f.velocityBoundaries);

  ResidentState resident;
  resident.uploadVelocity(f.velocity, n);
  ASSERT_TRUE(resident.fluxPlan.build(f.mesh, f.velocityBoundaries))
      << resident.fluxPlan.unsupportedReason();
  resident.massFlux.uploadFrom(correctedFlux.data(), f.mesh.numberOfFaces());
  cfd::gpu::GpuKEpsilonResult baseline;
  runResident(f, resident, cfg, baseline);

  // HALF ONE: a different carried flux must change nothing.
  cfd::fields::SurfaceField otherCarried = correctedFlux;
  for (Index face = 0; face < f.mesh.numberOfFaces(); ++face) otherCarried[face] *= 1.05;
  ResidentState carriedChanged;
  carriedChanged.uploadVelocity(f.velocity, n);
  ASSERT_TRUE(carriedChanged.fluxPlan.build(f.mesh, f.velocityBoundaries))
      << carriedChanged.fluxPlan.unsupportedReason();
  carriedChanged.massFlux.uploadFrom(otherCarried.data(), f.mesh.numberOfFaces());
  cfd::gpu::GpuKEpsilonResult withOtherCarried;
  runResident(f, carriedChanged, cfg, withOtherCarried);

  for (Index i = 0; i < n; ++i) {
    ASSERT_EQ(withOtherCarried.k[i], baseline.k[i])
        << "cell " << i
        << ": changing the carried flux moved k, so the turbulence convection is "
           "still reading SIMPLE's corrected flux instead of building its own";
    ASSERT_EQ(withOtherCarried.epsilon[i], baseline.epsilon[i]) << "cell " << i;
  }

  // HALF TWO: the velocity must change it, or half one proves nothing.
  cfd::fields::VectorField movedVelocity = f.velocity;
  for (Index i = 0; i < n; ++i)
    movedVelocity[i] = {movedVelocity[i].x * 1.05, movedVelocity[i].y, 0.0};
  ResidentState velocityChanged;
  velocityChanged.uploadVelocity(movedVelocity, n);
  ASSERT_TRUE(velocityChanged.fluxPlan.build(f.mesh, f.velocityBoundaries))
      << velocityChanged.fluxPlan.unsupportedReason();
  velocityChanged.massFlux.uploadFrom(correctedFlux.data(), f.mesh.numberOfFaces());
  cfd::gpu::GpuKEpsilonResult withMovedVelocity;
  runResident(f, velocityChanged, cfg, withMovedVelocity);

  bool moved = false;
  for (Index i = 0; i < n && !moved; ++i) {
    moved = withMovedVelocity.k[i] != baseline.k[i] ||
            withMovedVelocity.epsilon[i] != baseline.epsilon[i];
  }
  EXPECT_TRUE(moved) << "perturbing the resident velocity changed neither k nor epsilon, so the "
                        "turbulence convection flux is not being built from it and the first half "
                        "of this control is vacuous";
}

// =============================================================================
// Refusals.
// =============================================================================

TEST(ResidentKEpsilonEntry, AnUnusableOrMismatchedViewIsRefused) {
  if (deviceUnavailable()) GTEST_SKIP() << "no usable CUDA device at runtime";

  Fixture f;
  ASSERT_TRUE(f.buildPlan());
  const Index n = f.cellCount();
  const KEpsilonConfig cfg = config();
  KEpsilonModel seed(f.mesh, f.fluid, f.velocityBoundaries, f.kBoundaries, f.epsilonBoundaries,
                     cfg);
  cfd::gpu::GpuKEpsilonCorrection correction;
  ASSERT_TRUE(correction.prepare(f.mesh, f.kBoundaries, f.epsilonBoundaries, seed.k(),
                                 seed.epsilon(), seed.turbulentViscosity()));

  const cfd::gpu::GpuKEpsilonWallInputs noWall;
  cfd::gpu::GpuKEpsilonResult result;

  // An empty view -- what residentTurbulenceInputs() returns before
  // uploadInitialState has run. Refused rather than treated as "no state".
  const cfd::gpu::ResidentTurbulenceInputs empty;
  EXPECT_THROW(correction.correctResident(empty, f.mesh, f.fluid, cfg, noWall, result),
               cfd::InvalidArgumentError);

  // A view sized for a different mesh.
  ResidentState resident;
  resident.uploadVelocity(f.velocity, n);
  ASSERT_TRUE(resident.fluxPlan.build(f.mesh, f.velocityBoundaries))
      << resident.fluxPlan.unsupportedReason();
  const auto flux =
      cfd::physics::calculateMassFlux(f.mesh, f.velocity, f.fluid, f.velocityBoundaries);
  resident.massFlux.uploadFrom(flux.data(), f.mesh.numberOfFaces());
  cfd::gpu::ResidentTurbulenceInputs wrongSize = resident.view(f.convectionPlan, n);
  wrongSize.cellCount = n + 1;
  EXPECT_THROW(correction.correctResident(wrongSize, f.mesh, f.fluid, cfg, noWall, result),
               cfd::InvalidArgumentError);
}

// =============================================================================
// Determinism.
// =============================================================================

TEST(ResidentKEpsilonEntry, RepeatedResidentCorrectionsAreBitwiseIdentical) {
  if (deviceUnavailable()) GTEST_SKIP() << "no usable CUDA device at runtime";

  Fixture f;
  ASSERT_TRUE(f.buildPlan());
  const Index n = f.cellCount();
  const KEpsilonConfig cfg = config();
  ResidentState resident;
  resident.uploadVelocity(f.velocity, n);
  ASSERT_TRUE(resident.fluxPlan.build(f.mesh, f.velocityBoundaries))
      << resident.fluxPlan.unsupportedReason();
  const auto flux =
      cfd::physics::calculateMassFlux(f.mesh, f.velocity, f.fluid, f.velocityBoundaries);
  resident.massFlux.uploadFrom(flux.data(), f.mesh.numberOfFaces());

  cfd::gpu::GpuKEpsilonResult first;
  cfd::gpu::GpuKEpsilonResult second;
  runResident(f, resident, cfg, first);
  runResident(f, resident, cfg, second);
  for (Index i = 0; i < n; ++i) {
    ASSERT_EQ(first.k[i], second.k[i]) << "cell " << i;
    ASSERT_EQ(first.epsilon[i], second.epsilon[i]) << "cell " << i;
    ASSERT_EQ(first.turbulentViscosity[i], second.turbulentViscosity[i]) << "cell " << i;
  }
}
