// P3-PHYS-001: proves SIMPLE actually consumes an optional Boussinesq
// buoyancy source through the canonical momentum-assembly path, and --
// the critical regression gate -- that every documented "zero buoyancy"
// case (no buoyancy supplied at all, beta=0, gravity=(0,0)) reproduces
// the pre-existing non-buoyant solve() behavior *bit-for-bit*, not just
// approximately.
#include <gtest/gtest.h>

#include <memory>

#include "cfd/boundary/FixedGradient.hpp"
#include "cfd/boundary/MovingWall.hpp"
#include "cfd/boundary/Wall.hpp"
#include "cfd/core/Exception.hpp"
#include "cfd/mesh/MeshGeometry.hpp"
#include "cfd/physics/BoussinesqBuoyancy.hpp"
#include "cfd/physics/FluidProperties.hpp"
#include "cfd/pressure_velocity/SIMPLE.hpp"

using cfd::Index;
using cfd::InvalidArgumentError;
using cfd::Real;
using cfd::Vector2;
using cfd::boundary::BoundaryConditionSet;
using cfd::boundary::FixedGradient;
using cfd::boundary::MovingWall;
using cfd::boundary::Wall;
using cfd::fields::ScalarField;
using cfd::fields::VectorField;
using cfd::mesh::Mesh;
using cfd::mesh::MeshGeometry;
using cfd::physics::BoussinesqBuoyancy;
using cfd::physics::FluidProperties;
using cfd::pressure_velocity::SIMPLE;
using cfd::pressure_velocity::SIMPLEResult;
using cfd::pressure_velocity::SIMPLESettings;
using cfd::pressure_velocity::SIMPLEStatus;

namespace {

BoundaryConditionSet makeCavityVelocityBoundaries(const Mesh& mesh) {
  BoundaryConditionSet boundaries;
  boundaries.set(mesh, "left", std::make_unique<Wall>());
  boundaries.set(mesh, "right", std::make_unique<Wall>());
  boundaries.set(mesh, "bottom", std::make_unique<Wall>());
  boundaries.set(mesh, "top", std::make_unique<MovingWall>(Vector2{1.0, 0.0}));
  return boundaries;
}

BoundaryConditionSet makeZeroGradientPressureBoundaries(const Mesh& mesh) {
  BoundaryConditionSet boundaries;
  for (const auto& patch : mesh.boundaryPatches()) {
    boundaries.set(mesh, patch.name(), std::make_unique<FixedGradient>(0.0));
  }
  return boundaries;
}

// Diagnosed empirically (temporary standalone compilation): the
// combined lid-driven + buoyancy-source cases below need ~600 outer
// iterations to reach the same tight tolerances the pure-lid-driven
// baseline (test_simple_turbulence.cpp's own 500-iteration budget)
// reaches comfortably faster -- a stronger combined momentum source
// naturally takes longer to relax to steady state under fixed
// under-relaxation, not a correctness issue (confirmed converged,
// finalU/V/P residuals ~1e-7 at 603 iterations).
SIMPLESettings makeSettings() {
  SIMPLESettings settings;
  settings.maxIterations = 1500;
  settings.velocityRelaxation = 0.7;
  settings.pressureRelaxation = 0.3;
  settings.velocityTolerance = 1e-6;
  settings.pressureTolerance = 1e-6;
  settings.continuityTolerance = 1e-6;
  return settings;
}

// A nonuniform, physically arbitrary temperature field -- deliberately
// not uniform (a uniform field would trivially give zero source
// regardless of whether the bug being tested for exists), so the "zero
// buoyancy" cases below only pass if beta=0/gravity=0 genuinely zero out
// a *nonzero* per-cell source, not merely a global no-op.
ScalarField makeNonuniformTemperature(const Mesh& mesh, Real tRef) {
  ScalarField temperature(mesh.numberOfCells());
  for (const auto& cell : mesh.cells()) {
    temperature[cell.id()] = tRef + 20.0 * cell.centroid().x - 10.0 * cell.centroid().y;
  }
  return temperature;
}

SIMPLEResult runCavity(const Mesh& mesh, const FluidProperties& fluid,
                       const ScalarField* temperature, const BoussinesqBuoyancy* buoyancy) {
  const auto velocityBoundaries = makeCavityVelocityBoundaries(mesh);
  const auto pressureBoundaries = makeZeroGradientPressureBoundaries(mesh);
  const SIMPLE simple(makeSettings(), 0, nullptr, temperature, buoyancy);
  const VectorField initialVelocity(mesh.numberOfCells(), Vector2{0.0, 0.0});
  const ScalarField initialPressure(mesh.numberOfCells(), 0.0);
  return simple.solve(mesh, fluid, velocityBoundaries, pressureBoundaries, initialVelocity,
                      initialPressure);
}

void expectBitIdentical(const SIMPLEResult& a, const SIMPLEResult& b) {
  ASSERT_EQ(a.status, SIMPLEStatus::Converged);
  ASSERT_EQ(a.status, b.status);
  EXPECT_EQ(a.iterations, b.iterations);
  EXPECT_EQ(a.finalUResidual, b.finalUResidual);
  EXPECT_EQ(a.finalVResidual, b.finalVResidual);
  EXPECT_EQ(a.finalPressureResidual, b.finalPressureResidual);
  EXPECT_EQ(a.finalContinuityResidual, b.finalContinuityResidual);
  EXPECT_EQ(a.globalMassImbalance, b.globalMassImbalance);
  ASSERT_EQ(a.velocity.size(), b.velocity.size());
  for (Index i = 0; i < a.velocity.size(); ++i) {
    EXPECT_EQ(a.velocity[i].x, b.velocity[i].x) << "cell " << i;
    EXPECT_EQ(a.velocity[i].y, b.velocity[i].y) << "cell " << i;
    EXPECT_EQ(a.pressure[i], b.pressure[i]) << "cell " << i;
  }
}

}  // namespace

// --- Phase 5: zero-buoyancy equivalence (the critical regression gate) ----

TEST(SIMPLEBuoyancyTest, CaseA_NoBuoyancySuppliedIsTheBaseline) {
  // Sanity: the no-buoyancy path (both null, the pre-existing default)
  // converges to a sensible lid-driven-cavity solution on its own --
  // establishes the baseline the other cases below compare against.
  const Mesh mesh = MeshGeometry::createCartesian2D(4, 4, 1.0, 1.0);
  const FluidProperties fluid(1.0, 0.01);
  const SIMPLEResult result = runCavity(mesh, fluid, nullptr, nullptr);
  ASSERT_EQ(result.status, SIMPLEStatus::Converged);
}

TEST(SIMPLEBuoyancyTest, CaseB_BetaZeroMatchesNoBuoyancyBitForBit) {
  // Case B (task Phase 5): Boussinesq enabled with beta=0, against a
  // real nonuniform temperature field -- must reproduce Case A exactly.
  const Mesh mesh = MeshGeometry::createCartesian2D(4, 4, 1.0, 1.0);
  const FluidProperties fluid(1.0, 0.01);
  const ScalarField temperature = makeNonuniformTemperature(mesh, 300.0);
  const BoussinesqBuoyancy zeroBeta(fluid.density(), 0.0, 300.0, Vector2{0.0, -9.81});

  const SIMPLEResult caseA = runCavity(mesh, fluid, nullptr, nullptr);
  const SIMPLEResult caseB = runCavity(mesh, fluid, &temperature, &zeroBeta);

  expectBitIdentical(caseA, caseB);
}

TEST(SIMPLEBuoyancyTest, CaseC_ZeroGravityMatchesNoBuoyancyBitForBit) {
  // Case C (task Phase 5): Boussinesq enabled with gravity=(0,0), a
  // nonzero beta, against the same nonuniform temperature field -- must
  // also reproduce Case A exactly.
  const Mesh mesh = MeshGeometry::createCartesian2D(4, 4, 1.0, 1.0);
  const FluidProperties fluid(1.0, 0.01);
  const ScalarField temperature = makeNonuniformTemperature(mesh, 300.0);
  const BoussinesqBuoyancy zeroGravity(fluid.density(), 0.0034, 300.0, Vector2{0.0, 0.0});

  const SIMPLEResult caseA = runCavity(mesh, fluid, nullptr, nullptr);
  const SIMPLEResult caseC = runCavity(mesh, fluid, &temperature, &zeroGravity);

  expectBitIdentical(caseA, caseC);
}

TEST(SIMPLEBuoyancyTest, UniformReferenceTemperatureMatchesNoBuoyancyBitForBit) {
  // A fourth, equally-valid "zero buoyancy" case not explicitly named in
  // Phase 5 but implied by the class's own documented invariant: a
  // uniform T == T_ref field with real (nonzero) beta/gravity must also
  // reproduce Case A exactly, since source() is then (0,0) everywhere.
  const Mesh mesh = MeshGeometry::createCartesian2D(4, 4, 1.0, 1.0);
  const FluidProperties fluid(1.0, 0.01);
  const ScalarField uniformReferenceTemperature(mesh.numberOfCells(), 300.0);
  const BoussinesqBuoyancy buoyancy(fluid.density(), 0.0034, 300.0, Vector2{0.0, -9.81});

  const SIMPLEResult caseA = runCavity(mesh, fluid, nullptr, nullptr);
  const SIMPLEResult uniformT = runCavity(mesh, fluid, &uniformReferenceTemperature, &buoyancy);

  expectBitIdentical(caseA, uniformT);
}

// --- Real buoyancy changes the converged solution --------------------------

TEST(SIMPLEBuoyancyTest, RealBuoyancySourceChangesConvergedVelocityField) {
  // The mirror-image proof of ElevatedEddyViscosityChangesConvergedVelocityField
  // (test_simple_turbulence.cpp): a real, nonzero buoyancy source must
  // not be silently ignored anywhere between BoussinesqBuoyancy and the
  // assembled momentum system.
  const Mesh mesh = MeshGeometry::createCartesian2D(4, 4, 1.0, 1.0);
  const FluidProperties fluid(1.0, 0.01);
  const ScalarField temperature = makeNonuniformTemperature(mesh, 300.0);
  const BoussinesqBuoyancy buoyancy(fluid.density(), 0.05, 300.0, Vector2{0.0, -9.81});

  const SIMPLEResult baseline = runCavity(mesh, fluid, nullptr, nullptr);
  const SIMPLEResult withBuoyancy = runCavity(mesh, fluid, &temperature, &buoyancy);
  ASSERT_EQ(baseline.status, SIMPLEStatus::Converged);
  ASSERT_EQ(withBuoyancy.status, SIMPLEStatus::Converged);

  bool anyDiffers = false;
  for (Index i = 0; i < baseline.velocity.size(); ++i) {
    if (baseline.velocity[i].x != withBuoyancy.velocity[i].x ||
        baseline.velocity[i].y != withBuoyancy.velocity[i].y) {
      anyDiffers = true;
      break;
    }
  }
  EXPECT_TRUE(anyDiffers);
}

// --- Configuration validation -----------------------------------------------

TEST(SIMPLEBuoyancyTest, RejectsTemperatureWithoutBuoyancy) {
  const Mesh mesh = MeshGeometry::createCartesian2D(2, 2, 1.0, 1.0);
  const FluidProperties fluid(1.0, 0.01);
  const ScalarField temperature = makeNonuniformTemperature(mesh, 300.0);
  const SIMPLEResult result = runCavity(mesh, fluid, &temperature, nullptr);
  EXPECT_EQ(result.status, SIMPLEStatus::InvalidConfiguration);
}

TEST(SIMPLEBuoyancyTest, RejectsBuoyancyWithoutTemperature) {
  const Mesh mesh = MeshGeometry::createCartesian2D(2, 2, 1.0, 1.0);
  const FluidProperties fluid(1.0, 0.01);
  const BoussinesqBuoyancy buoyancy(fluid.density(), 0.0034, 300.0, Vector2{0.0, -9.81});
  const SIMPLEResult result = runCavity(mesh, fluid, nullptr, &buoyancy);
  EXPECT_EQ(result.status, SIMPLEStatus::InvalidConfiguration);
}

TEST(SIMPLEBuoyancyTest, RejectsMismatchedTemperatureSize) {
  const Mesh mesh = MeshGeometry::createCartesian2D(2, 2, 1.0, 1.0);
  const FluidProperties fluid(1.0, 0.01);
  const ScalarField wrongSizeTemperature(mesh.numberOfCells() + 1, 300.0);
  const BoussinesqBuoyancy buoyancy(fluid.density(), 0.0034, 300.0, Vector2{0.0, -9.81});
  const SIMPLEResult result = runCavity(mesh, fluid, &wrongSizeTemperature, &buoyancy);
  EXPECT_EQ(result.status, SIMPLEStatus::InvalidConfiguration);
}

// --- Determinism -------------------------------------------------------------

TEST(SIMPLEBuoyancyTest, RepeatedBuoyantSolveIsBitIdentical) {
  const Mesh mesh = MeshGeometry::createCartesian2D(4, 4, 1.0, 1.0);
  const FluidProperties fluid(1.0, 0.01);
  const ScalarField temperature = makeNonuniformTemperature(mesh, 300.0);
  const BoussinesqBuoyancy buoyancy(fluid.density(), 0.05, 300.0, Vector2{0.0, -9.81});

  const SIMPLEResult a = runCavity(mesh, fluid, &temperature, &buoyancy);
  const SIMPLEResult b = runCavity(mesh, fluid, &temperature, &buoyancy);
  expectBitIdentical(a, b);
}
