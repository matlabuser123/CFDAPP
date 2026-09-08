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

SIMPLEResult runCavity(const Mesh& mesh, const FluidProperties& fluid) {
  const auto velocityBoundaries = makeCavityVelocityBoundaries(mesh);
  const auto pressureBoundaries = makeZeroGradientPressureBoundaries(mesh);

  SIMPLESettings settings;
  settings.maxIterations = 500;
  settings.velocityRelaxation = 0.7;
  settings.pressureRelaxation = 0.3;
  settings.velocityTolerance = 1e-6;
  settings.pressureTolerance = 1e-6;
  settings.continuityTolerance = 1e-6;

  const SIMPLE simple(settings, 0);
  const VectorField initialVelocity(mesh.numberOfCells(), Vector2{0.0, 0.0});
  const ScalarField initialPressure(mesh.numberOfCells(), 0.0);
  return simple.solve(mesh, fluid, velocityBoundaries, pressureBoundaries, initialVelocity,
                      initialPressure);
}

}  // namespace

TEST(SIMPLEDeterminismTest, RepeatedSolveIsBitIdentical) {
  // TODO.md section 69: for serial P0, aim for bit-identical results
  // across repeated runs of the same problem.
  const Mesh mesh = MeshGeometry::createCartesian2D(4, 4, 1.0, 1.0);
  const FluidProperties fluid(1.0, 0.01);

  const SIMPLEResult a = runCavity(mesh, fluid);
  const SIMPLEResult b = runCavity(mesh, fluid);

  ASSERT_EQ(a.status, SIMPLEStatus::Converged);
  ASSERT_EQ(a.status, b.status);
  EXPECT_EQ(a.iterations, b.iterations);
  EXPECT_EQ(a.finalUResidual, b.finalUResidual);
  EXPECT_EQ(a.finalVResidual, b.finalVResidual);
  EXPECT_EQ(a.finalPressureResidual, b.finalPressureResidual);
  EXPECT_EQ(a.finalContinuityResidual, b.finalContinuityResidual);
  EXPECT_EQ(a.globalMassImbalance, b.globalMassImbalance);

  ASSERT_EQ(a.uResidualHistory.size(), b.uResidualHistory.size());
  for (Index i = 0; i < a.uResidualHistory.size(); ++i) {
    EXPECT_EQ(a.uResidualHistory[i], b.uResidualHistory[i]);
    EXPECT_EQ(a.vResidualHistory[i], b.vResidualHistory[i]);
    EXPECT_EQ(a.pressureResidualHistory[i], b.pressureResidualHistory[i]);
    EXPECT_EQ(a.continuityHistory[i], b.continuityHistory[i]);
  }

  ASSERT_EQ(a.velocity.size(), b.velocity.size());
  for (Index i = 0; i < a.velocity.size(); ++i) {
    EXPECT_EQ(a.velocity[i].x, b.velocity[i].x);
    EXPECT_EQ(a.velocity[i].y, b.velocity[i].y);
    EXPECT_EQ(a.pressure[i], b.pressure[i]);
  }
  ASSERT_EQ(a.massFlux.size(), b.massFlux.size());
  for (Index i = 0; i < a.massFlux.size(); ++i) {
    EXPECT_EQ(a.massFlux[i], b.massFlux[i]);
  }
}

TEST(SIMPLEDeterminismTest, RepeatedSolveIsBitIdenticalThirdRun) {
  // A third independent run, to guard against a subtle two-run
  // coincidence (e.g. an uninitialized-but-happens-to-match value).
  const Mesh mesh = MeshGeometry::createCartesian2D(4, 4, 1.0, 1.0);
  const FluidProperties fluid(1.0, 0.01);

  const SIMPLEResult a = runCavity(mesh, fluid);
  const SIMPLEResult c = runCavity(mesh, fluid);

  ASSERT_EQ(a.iterations, c.iterations);
  EXPECT_EQ(a.finalUResidual, c.finalUResidual);
  for (Index i = 0; i < a.velocity.size(); ++i) {
    EXPECT_EQ(a.velocity[i].x, c.velocity[i].x);
    EXPECT_EQ(a.velocity[i].y, c.velocity[i].y);
  }
}
