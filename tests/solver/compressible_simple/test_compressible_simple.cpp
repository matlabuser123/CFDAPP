// P12-COMP-002: CompressibleSIMPLE -- a genuinely coupled compressible
// pressure-velocity solver, dedicated and separate from
// cfd::pressure_velocity::SIMPLE (which is used unchanged, for every
// incompressible/post-hoc-compressible case, exactly as before). The
// mandatory acceptance regression: with a very large gas constant (the
// physical near-incompressible-gas limit, not a test-only bypass),
// CompressibleSIMPLE reproduces plain SIMPLE's own converged result on
// an already-validated case (the lid-driven cavity) within tolerance.
#include <gtest/gtest.h>

#include <cmath>
#include <memory>

#include "cfd/boundary/FixedGradient.hpp"
#include "cfd/boundary/MovingWall.hpp"
#include "cfd/boundary/Wall.hpp"
#include "cfd/compressible/CompressibleSIMPLE.hpp"
#include "cfd/core/Exception.hpp"
#include "cfd/mesh/MeshGeometry.hpp"
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
using cfd::compressible::CompressibleSIMPLE;
using cfd::compressible::CompressibleSIMPLEResult;
using cfd::compressible::CompressibleSIMPLESettings;
using cfd::compressible::CompressibleSIMPLEStatus;
using cfd::compressible::ThermodynamicProperties;
using cfd::compressible::validateCompressibleSIMPLESettings;
using cfd::fields::ScalarField;
using cfd::fields::VectorField;
using cfd::mesh::Mesh;
using cfd::mesh::MeshGeometry;
using cfd::physics::FluidProperties;
using cfd::pressure_velocity::SIMPLE;
using cfd::pressure_velocity::SIMPLEResult;
using cfd::pressure_velocity::SIMPLESettings;
using cfd::pressure_velocity::SIMPLEStatus;

namespace {

BoundaryConditionSet makeCavityVelocityBoundaries(const Mesh& mesh, Vector2 lidVelocity) {
  BoundaryConditionSet boundaries;
  boundaries.set(mesh, "left", std::make_unique<Wall>());
  boundaries.set(mesh, "right", std::make_unique<Wall>());
  boundaries.set(mesh, "bottom", std::make_unique<Wall>());
  boundaries.set(mesh, "top", std::make_unique<MovingWall>(lidVelocity));
  return boundaries;
}

BoundaryConditionSet makeZeroGradientPressureBoundaries(const Mesh& mesh) {
  BoundaryConditionSet boundaries;
  for (const auto& patch : mesh.boundaryPatches()) {
    boundaries.set(mesh, patch.name(), std::make_unique<FixedGradient>(0.0));
  }
  return boundaries;
}

SIMPLESettings makeIncompressibleSettings(Index maxIterations, Real tolerance) {
  SIMPLESettings settings;
  settings.maxIterations = maxIterations;
  settings.velocityRelaxation = 0.7;
  settings.pressureRelaxation = 0.3;
  settings.velocityTolerance = tolerance;
  settings.pressureTolerance = tolerance;
  settings.continuityTolerance = tolerance;
  settings.momentumSolver.maxIterations = 500;
  settings.momentumSolver.absoluteTolerance = 1e-10;
  settings.momentumSolver.relativeTolerance = 1e-8;
  settings.pressureSolver.maxIterations = 2000;
  settings.pressureSolver.absoluteTolerance = 1e-10;
  settings.pressureSolver.relativeTolerance = 1e-8;
  return settings;
}

CompressibleSIMPLESettings makeCompressibleSettings(Index maxIterations, Real tolerance,
                                                    Real pseudoTimeStep) {
  CompressibleSIMPLESettings settings;
  settings.maxIterations = maxIterations;
  settings.velocityRelaxation = 0.7;
  settings.pressureRelaxation = 0.3;
  settings.pseudoTimeStep = pseudoTimeStep;
  settings.velocityTolerance = tolerance;
  settings.pressureTolerance = tolerance;
  settings.continuityTolerance = tolerance;
  settings.momentumSolver.maxIterations = 500;
  settings.momentumSolver.absoluteTolerance = 1e-10;
  settings.momentumSolver.relativeTolerance = 1e-8;
  settings.pressureSolver.maxIterations = 2000;
  settings.pressureSolver.absoluteTolerance = 1e-10;
  settings.pressureSolver.relativeTolerance = 1e-8;
  return settings;
}

}  // namespace

// --- The mandatory reduction-to-incompressible regression -----------------

TEST(CompressibleSimpleTest, ReducesToIncompressibleSimpleWithNegligibleCompressibility) {
  const Mesh mesh = MeshGeometry::createCartesian2D(4, 4, 1.0, 1.0);
  const auto velocityBoundaries = makeCavityVelocityBoundaries(mesh, Vector2{1.0, 0.0});
  const auto pressureBoundaries = makeZeroGradientPressureBoundaries(mesh);
  const Index n = mesh.numberOfCells();
  const Real rho = 1.0;
  const Real mu = 0.01;

  // Reference: plain incompressible SIMPLE (unmodified, exactly as every
  // other caller uses it).
  const FluidProperties fluid(rho, mu);
  const SIMPLE simple(makeIncompressibleSettings(1000, 1e-6), /*referenceCell=*/0);
  const SIMPLEResult reference =
      simple.solve(mesh, fluid, velocityBoundaries, pressureBoundaries,
                  VectorField(n, Vector2{0.0, 0.0}), ScalarField(n, 0.0));
  ASSERT_EQ(reference.status, SIMPLEStatus::Converged);

  // CompressibleSIMPLE at the physical near-incompressible-gas limit: a
  // very large gas constant makes dDensityDPressure = 1/(R*T) negligible
  // -- not a test-only bypass of the compressibility term, a genuine
  // physical parameter choice. referencePressure/temperature are chosen
  // so the initial (and, since the term is negligible, every
  // subsequent) density equals `rho` exactly: rho = P/(R*T).
  const Real gasConstant = 1.0e10;
  const Real temperature = 300.0;
  const Real referencePressure = rho * gasConstant * temperature;
  const ThermodynamicProperties thermodynamics(gasConstant, /*cp=*/2.0e10);
  const CompressibleSIMPLE compressibleSimple(
      makeCompressibleSettings(1000, 1e-6, /*pseudoTimeStep=*/1.0), thermodynamics,
      referencePressure, /*referenceCell=*/0);

  const ScalarField temperatureField(n, temperature);
  const ScalarField initialDensity(n, rho);
  const CompressibleSIMPLEResult result = compressibleSimple.solve(
      mesh, mu, velocityBoundaries, pressureBoundaries, temperatureField,
      /*temperatureBoundaries=*/nullptr, VectorField(n, Vector2{0.0, 0.0}), ScalarField(n, 0.0),
      initialDensity);

  ASSERT_EQ(result.status, CompressibleSIMPLEStatus::Converged)
      << "CompressibleSIMPLE did not converge";

  for (Index i = 0; i < n; ++i) {
    EXPECT_NEAR(result.velocity[i].x, reference.velocity[i].x, 1e-4) << "cell " << i;
    EXPECT_NEAR(result.velocity[i].y, reference.velocity[i].y, 1e-4) << "cell " << i;
    EXPECT_NEAR(result.pressure[i], reference.pressure[i], 1e-3) << "cell " << i;
    // Density should have stayed essentially at the initial rho (the
    // near-incompressible-gas limit's own defining property).
    EXPECT_NEAR(result.density[i], rho, 1e-6) << "cell " << i;
  }
}

// --- Basic convergence (own case, not just the reduction target) ----------

TEST(CompressibleSimpleTest, TinyCavityConvergesWithGenuineCompressibility) {
  const Mesh mesh = MeshGeometry::createCartesian2D(4, 4, 1.0, 1.0);
  const auto velocityBoundaries = makeCavityVelocityBoundaries(mesh, Vector2{1.0, 0.0});
  const auto pressureBoundaries = makeZeroGradientPressureBoundaries(mesh);
  const Index n = mesh.numberOfCells();

  const Real gasConstant = 287.05;
  const Real temperature = 300.0;
  const Real referencePressure = 101325.0;
  const ThermodynamicProperties thermodynamics(gasConstant, 1005.0);
  const Real rho0 = thermodynamics.density(referencePressure, temperature);

  const CompressibleSIMPLE compressibleSimple(
      makeCompressibleSettings(2000, 1e-6, /*pseudoTimeStep=*/1.0), thermodynamics,
      referencePressure, /*referenceCell=*/0);
  const ScalarField temperatureField(n, temperature);
  const ScalarField initialDensity(n, rho0);

  const CompressibleSIMPLEResult result = compressibleSimple.solve(
      mesh, /*dynamicViscosity=*/1.8e-5, velocityBoundaries, pressureBoundaries, temperatureField,
      nullptr, VectorField(n, Vector2{0.0, 0.0}), ScalarField(n, 0.0), initialDensity);

  ASSERT_EQ(result.status, CompressibleSIMPLEStatus::Converged);
  EXPECT_LE(result.finalUResidual, 1e-6);
  EXPECT_LE(result.finalVResidual, 1e-6);
  EXPECT_LE(result.finalPressureResidual, 1e-6);
  EXPECT_LE(result.finalContinuityResidual, 1e-6);
  EXPECT_LE(result.globalMassImbalance, 1e-6);
  for (Index i = 0; i < n; ++i) {
    EXPECT_GT(result.density[i], 0.0) << "cell " << i;
    EXPECT_TRUE(std::isfinite(result.density[i])) << "cell " << i;
  }

  // Every wall (including the moving lid) carries ~0 normal mass flux at
  // the converged solution -- same conservation check SIMPLE's own
  // cavity test uses.
  for (const auto& patch : mesh.boundaryPatches()) {
    for (const Index faceId : patch.faceIds()) {
      EXPECT_NEAR(result.massFlux[faceId], 0.0, 1e-6);
    }
  }
}

TEST(CompressibleSimpleTest, RepeatedRunIsDeterministic) {
  const Mesh mesh = MeshGeometry::createCartesian2D(3, 3, 1.0, 1.0);
  const auto velocityBoundaries = makeCavityVelocityBoundaries(mesh, Vector2{1.0, 0.0});
  const auto pressureBoundaries = makeZeroGradientPressureBoundaries(mesh);
  const Index n = mesh.numberOfCells();
  const ThermodynamicProperties thermodynamics(287.05, 1005.0);
  const Real referencePressure = 101325.0;
  const Real temperature = 300.0;
  const Real rho0 = thermodynamics.density(referencePressure, temperature);

  const CompressibleSIMPLE compressibleSimple(makeCompressibleSettings(500, 1e-6, 1.0),
                                              thermodynamics, referencePressure, 0);
  const ScalarField temperatureField(n, temperature);
  const ScalarField initialDensity(n, rho0);

  const CompressibleSIMPLEResult first = compressibleSimple.solve(
      mesh, 1.8e-5, velocityBoundaries, pressureBoundaries, temperatureField, nullptr,
      VectorField(n, Vector2{0.0, 0.0}), ScalarField(n, 0.0), initialDensity);
  const CompressibleSIMPLEResult second = compressibleSimple.solve(
      mesh, 1.8e-5, velocityBoundaries, pressureBoundaries, temperatureField, nullptr,
      VectorField(n, Vector2{0.0, 0.0}), ScalarField(n, 0.0), initialDensity);

  ASSERT_EQ(first.status, CompressibleSIMPLEStatus::Converged);
  EXPECT_EQ(first.iterations, second.iterations);
  for (Index i = 0; i < n; ++i) {
    EXPECT_EQ(first.velocity[i].x, second.velocity[i].x) << "cell " << i;
    EXPECT_EQ(first.velocity[i].y, second.velocity[i].y) << "cell " << i;
    EXPECT_EQ(first.pressure[i], second.pressure[i]) << "cell " << i;
    EXPECT_EQ(first.density[i], second.density[i]) << "cell " << i;
  }
}

// --- Configuration/validation ----------------------------------------------

TEST(CompressibleSimpleSettingsTest, RejectsZeroMaxIterations) {
  CompressibleSIMPLESettings settings;
  settings.maxIterations = 0;
  EXPECT_THROW(validateCompressibleSIMPLESettings(settings), InvalidArgumentError);
}

TEST(CompressibleSimpleSettingsTest, RejectsNonPositivePseudoTimeStep) {
  CompressibleSIMPLESettings settings;
  settings.pseudoTimeStep = 0.0;
  EXPECT_THROW(validateCompressibleSIMPLESettings(settings), InvalidArgumentError);
}

TEST(CompressibleSimpleSettingsTest, RejectsOutOfRangeRelaxation) {
  CompressibleSIMPLESettings settings;
  settings.velocityRelaxation = 1.5;
  EXPECT_THROW(validateCompressibleSIMPLESettings(settings), InvalidArgumentError);
}

TEST(CompressibleSimpleTest, InvalidSettingsReportInvalidConfiguration) {
  const Mesh mesh = MeshGeometry::createCartesian2D(2, 2, 1.0, 1.0);
  const auto velocityBoundaries = makeCavityVelocityBoundaries(mesh, Vector2{1.0, 0.0});
  const auto pressureBoundaries = makeZeroGradientPressureBoundaries(mesh);
  const Index n = mesh.numberOfCells();

  CompressibleSIMPLESettings settings;
  settings.maxIterations = 0;  // invalid.
  const ThermodynamicProperties thermodynamics(287.05, 1005.0);
  const CompressibleSIMPLE compressibleSimple(settings, thermodynamics, 101325.0, 0);
  const ScalarField temperatureField(n, 300.0);
  const ScalarField initialDensity(n, 1.2);

  const CompressibleSIMPLEResult result = compressibleSimple.solve(
      mesh, 1.8e-5, velocityBoundaries, pressureBoundaries, temperatureField, nullptr,
      VectorField(n, Vector2{0.0, 0.0}), ScalarField(n, 0.0), initialDensity);

  EXPECT_EQ(result.status, CompressibleSIMPLEStatus::InvalidConfiguration);
}

TEST(CompressibleSimpleTest, MismatchedInitialFieldSizeReportsInvalidConfiguration) {
  const Mesh mesh = MeshGeometry::createCartesian2D(2, 2, 1.0, 1.0);
  const auto velocityBoundaries = makeCavityVelocityBoundaries(mesh, Vector2{1.0, 0.0});
  const auto pressureBoundaries = makeZeroGradientPressureBoundaries(mesh);

  const ThermodynamicProperties thermodynamics(287.05, 1005.0);
  const CompressibleSIMPLE compressibleSimple(makeCompressibleSettings(100, 1e-6, 1.0),
                                              thermodynamics, 101325.0, 0);
  const ScalarField temperatureField(mesh.numberOfCells(), 300.0);
  const ScalarField wrongSizedDensity(mesh.numberOfCells() + 1, 1.2);  // deliberately wrong.

  const CompressibleSIMPLEResult result = compressibleSimple.solve(
      mesh, 1.8e-5, velocityBoundaries, pressureBoundaries, temperatureField, nullptr,
      VectorField(mesh.numberOfCells(), Vector2{0.0, 0.0}), ScalarField(mesh.numberOfCells(), 0.0),
      wrongSizedDensity);

  EXPECT_EQ(result.status, CompressibleSIMPLEStatus::InvalidConfiguration);
}
