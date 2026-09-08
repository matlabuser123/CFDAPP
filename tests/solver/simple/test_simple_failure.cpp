#include <gtest/gtest.h>

#include <memory>

#include "cfd/boundary/FixedGradient.hpp"
#include "cfd/boundary/MovingWall.hpp"
#include "cfd/boundary/Wall.hpp"
#include "cfd/mesh/MeshGeometry.hpp"
#include "cfd/physics/FluidProperties.hpp"
#include "cfd/pressure_velocity/SIMPLE.hpp"

using cfd::Index;
using cfd::Vector2;
using cfd::boundary::BoundaryConditionSet;
using cfd::boundary::FixedGradient;
using cfd::boundary::MovingWall;
using cfd::boundary::Wall;
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

}  // namespace

TEST(SIMPLEFailureTest, MaxIterationsIsNotReportedAsConverged) {
  // TODO.md section 81: a case that cannot converge in one SIMPLE cycle,
  // deliberately capped at maxIterations=1.
  const Mesh mesh = MeshGeometry::createCartesian2D(8, 8, 1.0, 1.0);
  const auto velocityBoundaries = makeCavityVelocityBoundaries(mesh);
  const auto pressureBoundaries = makeZeroGradientPressureBoundaries(mesh);
  const FluidProperties fluid(1.0, 0.01);

  SIMPLESettings settings;
  settings.maxIterations = 1;
  settings.velocityTolerance = 1e-12;  // unreachable in one cycle
  settings.pressureTolerance = 1e-12;
  settings.continuityTolerance = 1e-12;

  const SIMPLE simple(settings, 0);
  const VectorField initialVelocity(mesh.numberOfCells(), Vector2{0.0, 0.0});
  const ScalarField initialPressure(mesh.numberOfCells(), 0.0);

  const SIMPLEResult result = simple.solve(mesh, fluid, velocityBoundaries, pressureBoundaries,
                                           initialVelocity, initialPressure);

  EXPECT_EQ(result.status, SIMPLEStatus::MaxIterations);
  EXPECT_FALSE(result.converged());
  EXPECT_EQ(result.iterations, 1u);
}

TEST(SIMPLEFailureTest, MomentumSolverTooFewIterationsReportsMomentumFailure) {
  // TODO.md section 83: an intentionally impossible inner momentum
  // solve must not let SIMPLE continue as though u*/v* were valid.
  const Mesh mesh = MeshGeometry::createCartesian2D(8, 8, 1.0, 1.0);
  const auto velocityBoundaries = makeCavityVelocityBoundaries(mesh);
  const auto pressureBoundaries = makeZeroGradientPressureBoundaries(mesh);
  const FluidProperties fluid(1.0, 0.01);

  SIMPLESettings settings;
  settings.maxIterations = 20;
  // Valid per LinearSolverSettings (>= 1), but not nearly enough
  // iterations to converge a 64-unknown system to this tight tolerance.
  settings.momentumSolver.maxIterations = 1;
  settings.momentumSolver.absoluteTolerance = 1e-14;
  settings.momentumSolver.relativeTolerance = 1e-14;

  const SIMPLE simple(settings, 0);
  const VectorField initialVelocity(mesh.numberOfCells(), Vector2{0.0, 0.0});
  const ScalarField initialPressure(mesh.numberOfCells(), 0.0);

  const SIMPLEResult result = simple.solve(mesh, fluid, velocityBoundaries, pressureBoundaries,
                                           initialVelocity, initialPressure);

  EXPECT_EQ(result.status, SIMPLEStatus::MomentumFailure);
  EXPECT_FALSE(result.converged());
}

TEST(SIMPLEFailureTest, PressureSolverTooFewIterationsReportsPressureCorrectionFailure) {
  // TODO.md section 26/83: same idea, for the pressure-correction solve.
  const Mesh mesh = MeshGeometry::createCartesian2D(8, 8, 1.0, 1.0);
  const auto velocityBoundaries = makeCavityVelocityBoundaries(mesh);
  const auto pressureBoundaries = makeZeroGradientPressureBoundaries(mesh);
  const FluidProperties fluid(1.0, 0.01);

  SIMPLESettings settings;
  settings.maxIterations = 20;
  settings.pressureSolver.maxIterations = 1;
  settings.pressureSolver.absoluteTolerance = 1e-14;
  settings.pressureSolver.relativeTolerance = 1e-14;

  const SIMPLE simple(settings, 0);
  const VectorField initialVelocity(mesh.numberOfCells(), Vector2{0.0, 0.0});
  const ScalarField initialPressure(mesh.numberOfCells(), 0.0);

  const SIMPLEResult result = simple.solve(mesh, fluid, velocityBoundaries, pressureBoundaries,
                                           initialVelocity, initialPressure);

  EXPECT_EQ(result.status, SIMPLEStatus::PressureCorrectionFailure);
  EXPECT_FALSE(result.converged());
}

TEST(SIMPLEFailureTest, InvalidSettingsReportInvalidConfiguration) {
  const Mesh mesh = MeshGeometry::createCartesian2D(2, 2, 1.0, 1.0);
  const auto velocityBoundaries = makeCavityVelocityBoundaries(mesh);
  const auto pressureBoundaries = makeZeroGradientPressureBoundaries(mesh);
  const FluidProperties fluid(1.0, 0.01);

  SIMPLESettings settings;
  settings.velocityRelaxation = 1.5;  // out of (0, 1]

  const SIMPLE simple(settings, 0);
  const VectorField initialVelocity(mesh.numberOfCells(), Vector2{0.0, 0.0});
  const ScalarField initialPressure(mesh.numberOfCells(), 0.0);

  const SIMPLEResult result = simple.solve(mesh, fluid, velocityBoundaries, pressureBoundaries,
                                           initialVelocity, initialPressure);

  EXPECT_EQ(result.status, SIMPLEStatus::InvalidConfiguration);
  EXPECT_FALSE(result.converged());
}

TEST(SIMPLEFailureTest, MismatchedInitialFieldSizeReportsInvalidConfiguration) {
  const Mesh mesh = MeshGeometry::createCartesian2D(2, 2, 1.0, 1.0);
  const auto velocityBoundaries = makeCavityVelocityBoundaries(mesh);
  const auto pressureBoundaries = makeZeroGradientPressureBoundaries(mesh);
  const FluidProperties fluid(1.0, 0.01);

  const SIMPLE simple(SIMPLESettings{}, 0);
  const VectorField initialVelocity(mesh.numberOfCells() + 1, Vector2{0.0, 0.0});
  const ScalarField initialPressure(mesh.numberOfCells(), 0.0);

  const SIMPLEResult result = simple.solve(mesh, fluid, velocityBoundaries, pressureBoundaries,
                                           initialVelocity, initialPressure);

  EXPECT_EQ(result.status, SIMPLEStatus::InvalidConfiguration);
}

TEST(SIMPLEFailureTest, InvalidInnerSolverSettingsReportInvalidConfiguration) {
  // LinearSolver's own constructor validates maxIterations >= 1 and
  // throws -- SIMPLE::solve must catch this at construction time
  // (before the main loop) and report it as InvalidConfiguration rather
  // than letting the exception escape solve().
  const Mesh mesh = MeshGeometry::createCartesian2D(2, 2, 1.0, 1.0);
  const auto velocityBoundaries = makeCavityVelocityBoundaries(mesh);
  const auto pressureBoundaries = makeZeroGradientPressureBoundaries(mesh);
  const FluidProperties fluid(1.0, 0.01);

  SIMPLESettings settings;
  settings.momentumSolver.maxIterations = 0;

  const SIMPLE simple(settings, 0);
  const VectorField initialVelocity(mesh.numberOfCells(), Vector2{0.0, 0.0});
  const ScalarField initialPressure(mesh.numberOfCells(), 0.0);

  const SIMPLEResult result = simple.solve(mesh, fluid, velocityBoundaries, pressureBoundaries,
                                           initialVelocity, initialPressure);

  EXPECT_EQ(result.status, SIMPLEStatus::InvalidConfiguration);
}

TEST(SIMPLEFailureTest, OutOfRangeReferenceCellReportsInvalidConfiguration) {
  const Mesh mesh = MeshGeometry::createCartesian2D(2, 2, 1.0, 1.0);
  const auto velocityBoundaries = makeCavityVelocityBoundaries(mesh);
  const auto pressureBoundaries = makeZeroGradientPressureBoundaries(mesh);
  const FluidProperties fluid(1.0, 0.01);

  const SIMPLE simple(SIMPLESettings{}, /*referenceCell=*/999);
  const VectorField initialVelocity(mesh.numberOfCells(), Vector2{0.0, 0.0});
  const ScalarField initialPressure(mesh.numberOfCells(), 0.0);

  const SIMPLEResult result = simple.solve(mesh, fluid, velocityBoundaries, pressureBoundaries,
                                           initialVelocity, initialPressure);

  EXPECT_EQ(result.status, SIMPLEStatus::InvalidConfiguration);
}
