#include <gtest/gtest.h>

#include <cmath>
#include <memory>

#include "cfd/boundary/FixedGradient.hpp"
#include "cfd/boundary/Inlet.hpp"
#include "cfd/boundary/MovingWall.hpp"
#include "cfd/boundary/Outlet.hpp"
#include "cfd/boundary/Wall.hpp"
#include "cfd/mesh/MeshGeometry.hpp"
#include "cfd/physics/FluidProperties.hpp"
#include "cfd/physics/MassFlux.hpp"
#include "cfd/physics/MomentumEquation.hpp"

using cfd::Index;
using cfd::Real;
using cfd::Vector2;
using cfd::boundary::BoundaryConditionSet;
using cfd::boundary::FixedGradient;
using cfd::boundary::Inlet;
using cfd::boundary::MovingWall;
using cfd::boundary::Outlet;
using cfd::boundary::Wall;
using cfd::fields::ScalarField;
using cfd::fields::VectorField;
using cfd::mesh::Mesh;
using cfd::mesh::MeshGeometry;
using cfd::physics::assembleMomentum;
using cfd::physics::calculateMassFlux;
using cfd::physics::FluidProperties;
using cfd::physics::MomentumSystems;

namespace {

BoundaryConditionSet makeZeroGradientPressureBoundaries(const Mesh& mesh) {
  BoundaryConditionSet boundaries;
  for (const auto& patch : mesh.boundaryPatches()) {
    boundaries.set(mesh, patch.name(), std::make_unique<FixedGradient>(0.0));
  }
  return boundaries;
}

}  // namespace

TEST(MomentumAssemblyTest, CavityAssemblyIsFiniteWithLidForcingOnUOnly) {
  // TODO.md section 34: 4x4 cavity, U=0, p=0, top = MovingWall(1,0),
  // other sides = Wall. No SIMPLE, no converged solution required --
  // just that assembly makes physical sense.
  const Mesh mesh = MeshGeometry::createCartesian2D(4, 4, 1.0, 1.0);
  BoundaryConditionSet velocityBoundaries;
  velocityBoundaries.set(mesh, "left", std::make_unique<Wall>());
  velocityBoundaries.set(mesh, "right", std::make_unique<Wall>());
  velocityBoundaries.set(mesh, "bottom", std::make_unique<Wall>());
  velocityBoundaries.set(mesh, "top", std::make_unique<MovingWall>(Vector2{1.0, 0.0}));
  const auto pressureBoundaries = makeZeroGradientPressureBoundaries(mesh);

  const VectorField velocity(mesh.numberOfCells(), Vector2{0.0, 0.0});
  const ScalarField pressure(mesh.numberOfCells(), 0.0);
  const FluidProperties fluid(1.0, 0.01);
  const auto massFlux = calculateMassFlux(mesh, velocity, fluid, velocityBoundaries);

  const MomentumSystems systems = assembleMomentum(mesh, velocity, pressure, massFlux, fluid,
                                                   velocityBoundaries, pressureBoundaries);

  EXPECT_TRUE(systems.u.system.matrix().allFinite());
  EXPECT_TRUE(systems.v.system.matrix().allFinite());
  EXPECT_TRUE(systems.u.system.rhs().allFinite());
  EXPECT_TRUE(systems.v.system.rhs().allFinite());
  for (Index i = 0; i < mesh.numberOfCells(); ++i) {
    EXPECT_GT(systems.u.diagonal[i], 0.0);
    EXPECT_GT(systems.v.diagonal[i], 0.0);
  }

  // The moving lid only drives U=0 walls; with zero initial velocity
  // everywhere the diffusion boundary contribution is the only nonzero
  // source, and it only touches u (lid velocity is (1,0)) -- v's RHS
  // should be exactly zero at every cell (Wall/MovingWall(1,0) both give
  // v_wall=0, and zero-gradient pressure means no pressure source).
  for (Index i = 0; i < mesh.numberOfCells(); ++i) {
    EXPECT_NEAR(systems.v.system.rhs()[i], 0.0, 1e-12);
  }
  // At least one top-row cell's u-equation must receive nonzero forcing
  // from the moving lid.
  bool anyLidForcing = false;
  for (Index i = 0; i < mesh.numberOfCells(); ++i) {
    if (std::abs(systems.u.system.rhs()[i]) > 1e-12) anyLidForcing = true;
  }
  EXPECT_TRUE(anyLidForcing);
}

TEST(MomentumAssemblyTest, ChannelAssemblyHasCorrectDimensionsAndFiniteCoefficients) {
  // TODO.md section 35: left=Inlet(1,0), right=Outlet, top/bottom=Wall.
  const Mesh mesh = MeshGeometry::createCartesian2D(8, 4, 2.0, 1.0);
  BoundaryConditionSet velocityBoundaries;
  velocityBoundaries.set(mesh, "left", std::make_unique<Inlet>(Vector2{1.0, 0.0}));
  velocityBoundaries.set(mesh, "right", std::make_unique<Outlet>());
  velocityBoundaries.set(mesh, "top", std::make_unique<Wall>());
  velocityBoundaries.set(mesh, "bottom", std::make_unique<Wall>());
  const auto pressureBoundaries = makeZeroGradientPressureBoundaries(mesh);

  const VectorField velocity(mesh.numberOfCells(), Vector2{1.0, 0.0});
  const ScalarField pressure(mesh.numberOfCells(), 0.0);
  const FluidProperties fluid(1.0, 0.01);
  const auto massFlux = calculateMassFlux(mesh, velocity, fluid, velocityBoundaries);

  const MomentumSystems systems = assembleMomentum(mesh, velocity, pressure, massFlux, fluid,
                                                   velocityBoundaries, pressureBoundaries);

  const Index n = mesh.numberOfCells();
  EXPECT_EQ(systems.u.system.matrix().rows(), n);
  EXPECT_EQ(systems.u.system.matrix().columns(), n);
  EXPECT_EQ(systems.u.system.rhs().size(), n);
  EXPECT_EQ(systems.v.system.matrix().rows(), n);
  EXPECT_EQ(systems.v.system.rhs().size(), n);

  EXPECT_TRUE(systems.u.system.matrix().allFinite());
  EXPECT_TRUE(systems.v.system.matrix().allFinite());
  EXPECT_TRUE(systems.u.system.rhs().allFinite());
  EXPECT_TRUE(systems.v.system.rhs().allFinite());

  // Wall mass fluxes must be exactly zero (TODO.md section 16).
  for (const auto* patchName : {"top", "bottom"}) {
    for (const Index faceId : mesh.boundaryPatch(patchName).faceIds()) {
      EXPECT_NEAR(massFlux[faceId], 0.0, 1e-12);
    }
  }
  // Inlet/outlet mass flows must balance globally (TODO.md section 22-23).
  Real leftTotal = 0.0, rightTotal = 0.0;
  for (const Index faceId : mesh.boundaryPatch("left").faceIds()) leftTotal += massFlux[faceId];
  for (const Index faceId : mesh.boundaryPatch("right").faceIds()) rightTotal += massFlux[faceId];
  EXPECT_NEAR(leftTotal + rightTotal, 0.0, 1e-10);
}

TEST(MomentumAssemblyTest, RepeatedAssemblyIsDeterministic) {
  // TODO.md section 57: assembling the same equation twice must produce
  // bit-identical CSR values, column indices, row offsets, RHS, and
  // diagonal.
  const Mesh mesh = MeshGeometry::createCartesian2D(5, 5, 1.0, 1.0);
  BoundaryConditionSet velocityBoundaries;
  velocityBoundaries.set(mesh, "left", std::make_unique<Inlet>(Vector2{1.0, 0.0}));
  velocityBoundaries.set(mesh, "right", std::make_unique<Outlet>());
  velocityBoundaries.set(mesh, "top", std::make_unique<Wall>());
  velocityBoundaries.set(mesh, "bottom", std::make_unique<Wall>());
  const auto pressureBoundaries = makeZeroGradientPressureBoundaries(mesh);

  const VectorField velocity(mesh.numberOfCells(), Vector2{0.5, 0.0});
  const ScalarField pressure(mesh.numberOfCells(), 0.0);
  const FluidProperties fluid(1.0, 0.02);
  const auto massFlux = calculateMassFlux(mesh, velocity, fluid, velocityBoundaries);

  const MomentumSystems a = assembleMomentum(mesh, velocity, pressure, massFlux, fluid,
                                             velocityBoundaries, pressureBoundaries);
  const MomentumSystems b = assembleMomentum(mesh, velocity, pressure, massFlux, fluid,
                                             velocityBoundaries, pressureBoundaries);

  for (Index i = 0; i < mesh.numberOfCells(); ++i) {
    EXPECT_EQ(a.u.system.rhs()[i], b.u.system.rhs()[i]);
    EXPECT_EQ(a.v.system.rhs()[i], b.v.system.rhs()[i]);
    EXPECT_EQ(a.u.diagonal[i], b.u.diagonal[i]);
    EXPECT_EQ(a.v.diagonal[i], b.v.diagonal[i]);
  }
}

TEST(MomentumAssemblyTest, UAndVSystemsAreIndependentScalarSystems) {
  // TODO.md section 36: N x N per component, not a coupled 2N x 2N block.
  const Mesh mesh = MeshGeometry::createCartesian2D(3, 3, 1.0, 1.0);
  BoundaryConditionSet velocityBoundaries;
  for (const auto& patch : mesh.boundaryPatches()) {
    velocityBoundaries.set(mesh, patch.name(), std::make_unique<Wall>());
  }
  const auto pressureBoundaries = makeZeroGradientPressureBoundaries(mesh);

  const VectorField velocity(mesh.numberOfCells(), Vector2{0.0, 0.0});
  const ScalarField pressure(mesh.numberOfCells(), 0.0);
  const FluidProperties fluid(1.0, 0.01);
  const auto massFlux = calculateMassFlux(mesh, velocity, fluid, velocityBoundaries);

  const MomentumSystems systems = assembleMomentum(mesh, velocity, pressure, massFlux, fluid,
                                                   velocityBoundaries, pressureBoundaries);
  const Index n = mesh.numberOfCells();
  EXPECT_EQ(systems.u.system.matrix().rows(), n);
  EXPECT_EQ(systems.v.system.matrix().rows(), n);
}
