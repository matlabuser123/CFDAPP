#include <gtest/gtest.h>

#include <limits>
#include <memory>

#include "cfd/boundary/FixedGradient.hpp"
#include "cfd/boundary/MovingWall.hpp"
#include "cfd/boundary/Wall.hpp"
#include "cfd/mesh/MeshGeometry.hpp"
#include "cfd/physics/FluidProperties.hpp"
#include "cfd/pressure_velocity/SIMPLE.hpp"

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

TEST(SIMPLENonFiniteTest, NaNInitialVelocityIsDetectedImmediately) {
  // TODO.md section 47: SIMPLE must not let a NaN entering the initial
  // state propagate through hundreds of iterations before failing.
  const Mesh mesh = MeshGeometry::createCartesian2D(4, 4, 1.0, 1.0);
  const auto velocityBoundaries = makeCavityVelocityBoundaries(mesh);
  const auto pressureBoundaries = makeZeroGradientPressureBoundaries(mesh);
  const FluidProperties fluid(1.0, 0.01);

  const SIMPLE simple(SIMPLESettings{}, 0);
  VectorField initialVelocity(mesh.numberOfCells(), Vector2{0.0, 0.0});
  initialVelocity[0] = Vector2{std::numeric_limits<Real>::quiet_NaN(), 0.0};
  const ScalarField initialPressure(mesh.numberOfCells(), 0.0);

  const SIMPLEResult result = simple.solve(mesh, fluid, velocityBoundaries, pressureBoundaries,
                                           initialVelocity, initialPressure);

  EXPECT_EQ(result.status, SIMPLEStatus::NonFiniteState);
  EXPECT_FALSE(result.converged());
  EXPECT_EQ(result.iterations, 0u);
}

TEST(SIMPLENonFiniteTest, InfiniteInitialPressureIsDetectedImmediately) {
  const Mesh mesh = MeshGeometry::createCartesian2D(4, 4, 1.0, 1.0);
  const auto velocityBoundaries = makeCavityVelocityBoundaries(mesh);
  const auto pressureBoundaries = makeZeroGradientPressureBoundaries(mesh);
  const FluidProperties fluid(1.0, 0.01);

  const SIMPLE simple(SIMPLESettings{}, 0);
  const VectorField initialVelocity(mesh.numberOfCells(), Vector2{0.0, 0.0});
  ScalarField initialPressure(mesh.numberOfCells(), 0.0);
  initialPressure[3] = std::numeric_limits<Real>::infinity();

  const SIMPLEResult result = simple.solve(mesh, fluid, velocityBoundaries, pressureBoundaries,
                                           initialVelocity, initialPressure);

  EXPECT_EQ(result.status, SIMPLEStatus::NonFiniteState);
  EXPECT_FALSE(result.converged());
}
