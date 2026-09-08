#include <gtest/gtest.h>

#include <memory>

#include "cfd/boundary/Inlet.hpp"
#include "cfd/boundary/MovingWall.hpp"
#include "cfd/boundary/Outlet.hpp"
#include "cfd/boundary/Wall.hpp"
#include "cfd/core/Exception.hpp"
#include "cfd/mesh/MeshGeometry.hpp"
#include "cfd/physics/FluidProperties.hpp"
#include "cfd/physics/MassFlux.hpp"

using cfd::Index;
using cfd::InvalidArgumentError;
using cfd::Real;
using cfd::Vector2;
using cfd::boundary::BoundaryConditionSet;
using cfd::boundary::Inlet;
using cfd::boundary::MovingWall;
using cfd::boundary::Outlet;
using cfd::boundary::Wall;
using cfd::fields::SurfaceField;
using cfd::fields::VectorField;
using cfd::mesh::Mesh;
using cfd::mesh::MeshGeometry;
using cfd::physics::calculateMassFlux;
using cfd::physics::FluidProperties;

namespace {

// A 2x1 channel: left = inlet, right = outlet, top/bottom = walls.
// lengthX=2, lengthY=1 -> dx=dy=1, so the internal (vertical) face has
// area 1 and cell centers are exactly 1 apart -- matches TODO.md section
// 49's worked example.
Mesh makeChannelMesh() { return MeshGeometry::createCartesian2D(2, 1, 2.0, 1.0); }

BoundaryConditionSet makeChannelBoundaries(const Mesh& mesh, Vector2 inletVelocity) {
  BoundaryConditionSet boundaries;
  boundaries.set(mesh, "left", std::make_unique<Inlet>(inletVelocity));
  boundaries.set(mesh, "right", std::make_unique<Outlet>());
  boundaries.set(mesh, "bottom", std::make_unique<Wall>());
  boundaries.set(mesh, "top", std::make_unique<Wall>());
  return boundaries;
}

}  // namespace

TEST(MassFluxTest, InternalFaceValue) {
  // TODO.md section 49: rho=2, U_P=(1,0), U_N=(3,0) -> U_f=(2,0) (linear
  // interpolation on a uniform grid), internal face area=1, Sf=(1,0) ->
  // F = 2 * 2 * 1 = 4.
  const Mesh mesh = makeChannelMesh();
  const auto boundaries = makeChannelBoundaries(mesh, Vector2{1.0, 0.0});
  VectorField velocity(mesh.numberOfCells());
  velocity[0] = Vector2{1.0, 0.0};
  velocity[1] = Vector2{3.0, 0.0};

  const FluidProperties fluid(2.0, 1.0);
  const SurfaceField massFlux = calculateMassFlux(mesh, velocity, fluid, boundaries);

  // The internal face is the only face with both an owner and neighbor.
  bool found = false;
  for (const auto& face : mesh.faces()) {
    if (!face.isBoundary()) {
      EXPECT_NEAR(massFlux[face.id()], 4.0, 1e-12);
      found = true;
    }
  }
  EXPECT_TRUE(found);
}

TEST(MassFluxTest, TangentialVelocityAtVerticalFaceGivesZeroFlux) {
  // TODO.md section 50: face normal n=(1,0), velocity U=(0,5) -> U.Sf=0.
  const Mesh mesh = makeChannelMesh();
  BoundaryConditionSet boundaries = makeChannelBoundaries(mesh, Vector2{1.0, 0.0});
  boundaries.replace(mesh, "left", std::make_unique<MovingWall>(Vector2{0.0, 5.0}));
  const VectorField velocity(mesh.numberOfCells(), Vector2{1.0, 0.0});

  const FluidProperties fluid(1.0, 1.0);
  const SurfaceField massFlux = calculateMassFlux(mesh, velocity, fluid, boundaries);

  const auto& leftPatch = mesh.boundaryPatch("left");
  for (const Index faceId : leftPatch.faceIds()) {
    EXPECT_NEAR(massFlux[faceId], 0.0, 1e-12);
  }
}

TEST(MassFluxTest, StationaryWallGivesZeroFlux) {
  const Mesh mesh = makeChannelMesh();
  const auto boundaries = makeChannelBoundaries(mesh, Vector2{1.0, 0.0});
  const VectorField velocity(mesh.numberOfCells(), Vector2{1.0, 0.0});

  const FluidProperties fluid(1.0, 1.0);
  const SurfaceField massFlux = calculateMassFlux(mesh, velocity, fluid, boundaries);

  for (const auto* patchName : {"bottom", "top"}) {
    for (const Index faceId : mesh.boundaryPatch(patchName).faceIds()) {
      EXPECT_NEAR(massFlux[faceId], 0.0, 1e-12);
    }
  }
}

TEST(MassFluxTest, TangentialMovingWallGivesZeroFlux) {
  // TODO.md section 51: top boundary Sf=(0,A), Uwall=(1,0) -> F=0. This
  // is the mandatory check for a lid-driven cavity: a moving lid must
  // not inject mass through the top wall.
  const Mesh mesh = makeChannelMesh();
  BoundaryConditionSet boundaries = makeChannelBoundaries(mesh, Vector2{1.0, 0.0});
  boundaries.replace(mesh, "top", std::make_unique<MovingWall>(Vector2{1.0, 0.0}));
  const VectorField velocity(mesh.numberOfCells(), Vector2{1.0, 0.0});

  const FluidProperties fluid(1.0, 1.0);
  const SurfaceField massFlux = calculateMassFlux(mesh, velocity, fluid, boundaries);

  for (const Index faceId : mesh.boundaryPatch("top").faceIds()) {
    EXPECT_NEAR(massFlux[faceId], 0.0, 1e-12);
  }
}

TEST(MassFluxTest, InletFluxIsNegativeOutletFluxIsPositive) {
  // TODO.md section 17: left Sf=(-A,0), U=(1,0) -> F<0 (mass enters);
  // right Sf=(+A,0), U=(1,0) -> F>0 (mass leaves).
  const Mesh mesh = makeChannelMesh();
  const auto boundaries = makeChannelBoundaries(mesh, Vector2{1.0, 0.0});
  const VectorField velocity(mesh.numberOfCells(), Vector2{1.0, 0.0});

  const FluidProperties fluid(1.0, 1.0);
  const SurfaceField massFlux = calculateMassFlux(mesh, velocity, fluid, boundaries);

  for (const Index faceId : mesh.boundaryPatch("left").faceIds()) {
    EXPECT_LT(massFlux[faceId], 0.0);
  }
  for (const Index faceId : mesh.boundaryPatch("right").faceIds()) {
    EXPECT_GT(massFlux[faceId], 0.0);
  }
}

TEST(MassFluxTest, ChannelMassBalanceExample) {
  // TODO.md section 23: Ly=1, rho=2, u=3 -> |mdot| = rho*u*Ly = 6, with
  // left total flux = -6 and right total flux = +6.
  const Mesh mesh = MeshGeometry::createCartesian2D(2, 4, 2.0, 1.0);
  const auto boundaries = makeChannelBoundaries(mesh, Vector2{3.0, 0.0});
  const VectorField velocity(mesh.numberOfCells(), Vector2{3.0, 0.0});

  const FluidProperties fluid(2.0, 1.0);
  const SurfaceField massFlux = calculateMassFlux(mesh, velocity, fluid, boundaries);

  Real leftTotal = 0.0;
  Real rightTotal = 0.0;
  for (const Index faceId : mesh.boundaryPatch("left").faceIds()) leftTotal += massFlux[faceId];
  for (const Index faceId : mesh.boundaryPatch("right").faceIds()) rightTotal += massFlux[faceId];

  EXPECT_NEAR(leftTotal, -6.0, 1e-10);
  EXPECT_NEAR(rightTotal, 6.0, 1e-10);
}

TEST(MassFluxTest, MismatchedVelocitySizeThrows) {
  const Mesh mesh = makeChannelMesh();
  const auto boundaries = makeChannelBoundaries(mesh, Vector2{1.0, 0.0});
  const VectorField velocity(mesh.numberOfCells() + 1, Vector2{1.0, 0.0});
  const FluidProperties fluid(1.0, 1.0);

  EXPECT_THROW((void)calculateMassFlux(mesh, velocity, fluid, boundaries), InvalidArgumentError);
}
