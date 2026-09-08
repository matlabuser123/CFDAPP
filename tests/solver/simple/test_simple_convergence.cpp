#include <gtest/gtest.h>

#include <memory>

#include "cfd/boundary/FixedGradient.hpp"
#include "cfd/boundary/MovingWall.hpp"
#include "cfd/boundary/Wall.hpp"
#include "cfd/mesh/MeshGeometry.hpp"
#include "cfd/physics/FluidProperties.hpp"
#include "cfd/pressure_velocity/SIMPLE.hpp"

using cfd::Index;
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
using cfd::physics::FluidProperties;
using cfd::pressure_velocity::SIMPLE;
using cfd::pressure_velocity::SIMPLEResult;
using cfd::pressure_velocity::SIMPLESettings;
using cfd::pressure_velocity::SIMPLEStatus;

namespace {

// TODO.md section 64: left/right/bottom = Wall, top = MovingWall(1,0).
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

SIMPLESettings makeCavitySettings(Index maxIterations, Real tolerance) {
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

}  // namespace

TEST(SIMPLEConvergenceTest, TinyFourByFourCavityConverges) {
  // TODO.md section 61: not published accuracy -- finite iterations,
  // residual reduction, continuity reduction, no NaN, correct
  // boundaries.
  const Mesh mesh = MeshGeometry::createCartesian2D(4, 4, 1.0, 1.0);
  const auto velocityBoundaries = makeCavityVelocityBoundaries(mesh, Vector2{1.0, 0.0});
  const auto pressureBoundaries = makeZeroGradientPressureBoundaries(mesh);
  const FluidProperties fluid(1.0, 0.01);

  const SIMPLE simple(makeCavitySettings(1000, 1e-6), /*referenceCell=*/0);
  const VectorField initialVelocity(mesh.numberOfCells(), Vector2{0.0, 0.0});
  const ScalarField initialPressure(mesh.numberOfCells(), 0.0);

  const SIMPLEResult result = simple.solve(mesh, fluid, velocityBoundaries, pressureBoundaries,
                                           initialVelocity, initialPressure);

  ASSERT_EQ(result.status, SIMPLEStatus::Converged);
  EXPECT_TRUE(result.converged());
  EXPECT_LT(result.iterations, 1000u);
  EXPECT_LE(result.finalUResidual, 1e-6);
  EXPECT_LE(result.finalVResidual, 1e-6);
  EXPECT_LE(result.finalPressureResidual, 1e-6);
  EXPECT_LE(result.finalContinuityResidual, 1e-6);
  EXPECT_LE(result.globalMassImbalance, 1e-6);

  // TODO.md section 86: every cavity wall face -- including the moving
  // lid -- must carry ~0 normal mass flux at the converged solution.
  for (const auto& patch : mesh.boundaryPatches()) {
    for (const Index faceId : patch.faceIds()) {
      EXPECT_NEAR(result.massFlux[faceId], 0.0, 1e-9);
    }
  }

  // Residuals should have dropped substantially, not just crossed the
  // gate by luck (TODO.md section 71: fix the operator, don't loosen
  // tolerance to hide a bug -- applied here as "the run should actually
  // show real reduction").
  ASSERT_FALSE(result.uResidualHistory.empty());
  EXPECT_LT(result.uResidualHistory.back(), result.uResidualHistory.front() * 1e-2);
}

TEST(SIMPLEConvergenceTest, CavityRe100_20x20Converges) {
  // TODO.md section 64: the canonical target case. rho=1, mu=0.01,
  // U_lid=1, L=1 -> Re = rho*U*L/mu = 100. Slower per-iteration than the
  // 4x4 case (400 cells), so this uses a generous iteration budget --
  // empirically converges around iteration ~3000 with these settings
  // (verified in this session before writing this test).
  const Mesh mesh = MeshGeometry::createCartesian2D(20, 20, 1.0, 1.0);
  const auto velocityBoundaries = makeCavityVelocityBoundaries(mesh, Vector2{1.0, 0.0});
  const auto pressureBoundaries = makeZeroGradientPressureBoundaries(mesh);
  const FluidProperties fluid(1.0, 0.01);

  const SIMPLE simple(makeCavitySettings(6000, 1e-6), /*referenceCell=*/0);
  const VectorField initialVelocity(mesh.numberOfCells(), Vector2{0.0, 0.0});
  const ScalarField initialPressure(mesh.numberOfCells(), 0.0);

  const SIMPLEResult result = simple.solve(mesh, fluid, velocityBoundaries, pressureBoundaries,
                                           initialVelocity, initialPressure);

  ASSERT_EQ(result.status, SIMPLEStatus::Converged);
  for (const auto& patch : mesh.boundaryPatches()) {
    for (const Index faceId : patch.faceIds()) {
      EXPECT_NEAR(result.massFlux[faceId], 0.0, 1e-8);
    }
  }
}
