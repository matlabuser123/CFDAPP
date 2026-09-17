// P2-TURB-006 sections 9-10, 41: cfd::turbulence::computeWallDistance --
// the mandatory hand-derived wall-distance tests, plus proof that inlet/
// outlet/symmetry patches are never mistaken for walls.
#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>
#include <vector>

#include "cfd/boundary/Inlet.hpp"
#include "cfd/boundary/Outlet.hpp"
#include "cfd/boundary/Symmetry.hpp"
#include "cfd/boundary/Wall.hpp"
#include "cfd/core/Exception.hpp"
#include "cfd/mesh/MeshGeometry.hpp"
#include "cfd/turbulence/WallDistance.hpp"

using cfd::Index;
using cfd::InvalidArgumentError;
using cfd::Vector2;
using cfd::boundary::BoundaryConditionSet;
using cfd::boundary::Inlet;
using cfd::boundary::Outlet;
using cfd::boundary::Symmetry;
using cfd::boundary::Wall;
using cfd::mesh::Mesh;
using cfd::mesh::MeshGeometry;
using cfd::turbulence::computeWallDistance;

TEST(WallDistanceTest, HandDerivedDistanceNearBottomWall) {
  // P2-TURB-006 section 10 (mandatory): domain 0<=x<=1, 0<=y<=1, top and
  // bottom walls; a single column (nx=1) of 4 rows (ny=1..4) so the cell
  // centered at (0.5, 0.125) exists exactly. left/right are non-wall
  // (inlet/outlet), proving they are not mistaken for walls.
  const Mesh mesh = MeshGeometry::createCartesian2D(1, 4, 1.0, 1.0);
  BoundaryConditionSet velocityBoundaries;
  velocityBoundaries.set(mesh, "left", std::make_unique<Inlet>(Vector2{1.0, 0.0}));
  velocityBoundaries.set(mesh, "right", std::make_unique<Outlet>());
  velocityBoundaries.set(mesh, "bottom", std::make_unique<Wall>());
  velocityBoundaries.set(mesh, "top", std::make_unique<Wall>());

  const auto distance = computeWallDistance(mesh, velocityBoundaries);
  ASSERT_EQ(distance.size(), mesh.numberOfCells());

  // Cell 0's centroid: column 0 (x=0.5), row 0 (y=0.125, since each of
  // the 4 rows is 0.25 tall).
  ASSERT_DOUBLE_EQ(mesh.cell(0).centroid().x, 0.5);
  ASSERT_DOUBLE_EQ(mesh.cell(0).centroid().y, 0.125);
  EXPECT_DOUBLE_EQ(distance[0], 0.125);
}

TEST(WallDistanceTest, HandDerivedDistanceAtDomainCenter) {
  // Single cell (nx=1, ny=1): centroid exactly at (0.5, 0.5), top/bottom
  // walls equidistant -> expected distance 0.5.
  const Mesh mesh = MeshGeometry::createCartesian2D(1, 1, 1.0, 1.0);
  BoundaryConditionSet velocityBoundaries;
  velocityBoundaries.set(mesh, "left", std::make_unique<Inlet>(Vector2{1.0, 0.0}));
  velocityBoundaries.set(mesh, "right", std::make_unique<Outlet>());
  velocityBoundaries.set(mesh, "bottom", std::make_unique<Wall>());
  velocityBoundaries.set(mesh, "top", std::make_unique<Wall>());

  const auto distance = computeWallDistance(mesh, velocityBoundaries);
  ASSERT_DOUBLE_EQ(mesh.cell(0).centroid().x, 0.5);
  ASSERT_DOUBLE_EQ(mesh.cell(0).centroid().y, 0.5);
  EXPECT_DOUBLE_EQ(distance[0], 0.5);
}

TEST(WallDistanceTest, InletOutletSymmetryAreNeverMistakenForWalls) {
  // Only "bottom" is a wall; left/right/top are inlet/outlet/symmetry.
  // Every cell's distance must be measured against "bottom" alone --
  // verified by checking the top row's own distance is NOT small (it
  // would be, wrongly, if "top" were mistakenly treated as a wall too).
  const Mesh mesh = MeshGeometry::createCartesian2D(1, 4, 1.0, 1.0);
  BoundaryConditionSet velocityBoundaries;
  velocityBoundaries.set(mesh, "left", std::make_unique<Inlet>(Vector2{1.0, 0.0}));
  velocityBoundaries.set(mesh, "right", std::make_unique<Outlet>());
  velocityBoundaries.set(mesh, "bottom", std::make_unique<Wall>());
  velocityBoundaries.set(mesh, "top", std::make_unique<Symmetry>());

  const auto distance = computeWallDistance(mesh, velocityBoundaries);
  // Row 3 (topmost, y=0.875) is far from the only wall (y=0) -- if "top"
  // were wrongly treated as a wall, this would instead be close to
  // 0.125 (distance to the top boundary).
  EXPECT_NEAR(distance[3], 0.875, 1e-12);
  EXPECT_GT(distance[3], 0.5);
}

TEST(WallDistanceTest, EveryCellDistanceIsFiniteAndPositive) {
  const Mesh mesh = MeshGeometry::createCartesian2D(4, 4, 1.0, 1.0);
  BoundaryConditionSet velocityBoundaries;
  velocityBoundaries.set(mesh, "left", std::make_unique<Wall>());
  velocityBoundaries.set(mesh, "right", std::make_unique<Wall>());
  velocityBoundaries.set(mesh, "bottom", std::make_unique<Wall>());
  velocityBoundaries.set(mesh, "top", std::make_unique<Wall>());

  const auto distance = computeWallDistance(mesh, velocityBoundaries);
  for (Index i = 0; i < distance.size(); ++i) {
    EXPECT_TRUE(std::isfinite(distance[i])) << "cell " << i;
    EXPECT_GT(distance[i], 0.0) << "cell " << i;
  }
}

TEST(WallDistanceTest, RejectsNoWallPatches) {
  const Mesh mesh = MeshGeometry::createCartesian2D(2, 2, 1.0, 1.0);
  BoundaryConditionSet velocityBoundaries;
  velocityBoundaries.set(mesh, "left", std::make_unique<Inlet>(Vector2{1.0, 0.0}));
  velocityBoundaries.set(mesh, "right", std::make_unique<Outlet>());
  velocityBoundaries.set(mesh, "bottom", std::make_unique<Symmetry>());
  velocityBoundaries.set(mesh, "top", std::make_unique<Symmetry>());

  EXPECT_THROW((void)computeWallDistance(mesh, velocityBoundaries), InvalidArgumentError);
}

// P12-MESH-001: on a structured_quad channel whose grid lines meet both
// walls obliquely (vertical lines tilted by up to ~30 degrees; y rows kept
// straight), the distance is the exact perpendicular wall distance
// min(y, H - y) of every centroid. The pre-P12-MESH-001 face-centroid
// distance overestimated it in the wall-adjacent cells (checked below, so
// this test proves the mesh actually exercises the difference).
TEST(WallDistanceTest, ExactPerpendicularDistanceOnTiltedStructuredQuadMesh) {
  const Index nx = 24;
  const Index ny = 6;
  const cfd::Real length = 4.0;
  const cfd::Real height = 1.0;
  const cfd::Real pi = std::acos(-1.0);
  std::vector<Vector2> vertices;
  for (Index j = 0; j <= ny; ++j) {
    for (Index i = 0; i <= nx; ++i) {
      const cfd::Real xi = length * static_cast<cfd::Real>(i) / static_cast<cfd::Real>(nx);
      const cfd::Real eta = height * static_cast<cfd::Real>(j) / static_cast<cfd::Real>(ny);
      cfd::Real x = xi + (0.08 * std::sin(pi * xi / length) * std::sin(2.0 * pi * eta / height));
      if (i == 0) x = 0.0;
      if (i == nx) x = length;
      vertices.push_back(Vector2{x, eta});
    }
  }
  const Mesh mesh = MeshGeometry::createStructuredQuad2D(nx, ny, vertices);
  BoundaryConditionSet velocityBoundaries;
  velocityBoundaries.set(mesh, "left", std::make_unique<Inlet>(Vector2{1.0, 0.0}));
  velocityBoundaries.set(mesh, "right", std::make_unique<Outlet>());
  velocityBoundaries.set(mesh, "bottom", std::make_unique<Wall>());
  velocityBoundaries.set(mesh, "top", std::make_unique<Wall>());

  const auto distance = computeWallDistance(mesh, velocityBoundaries);
  cfd::Real worstFaceCentroidOverestimate = 0.0;
  for (const auto& cell : mesh.cells()) {
    const cfd::Real exact = std::min(cell.centroid().y, height - cell.centroid().y);
    EXPECT_NEAR(distance[cell.id()], exact, 1e-12 * height) << "cell " << cell.id();
    cfd::Real faceCentroidDistance = std::numeric_limits<cfd::Real>::infinity();
    for (const char* wall : {"bottom", "top"}) {
      for (const Index f : mesh.boundaryPatch(wall).faceIds()) {
        faceCentroidDistance = std::min(
            faceCentroidDistance, MeshGeometry::distance(cell.centroid(), mesh.face(f).centroid()));
      }
    }
    worstFaceCentroidOverestimate =
        std::max(worstFaceCentroidOverestimate, faceCentroidDistance / exact - 1.0);
  }
  EXPECT_GT(worstFaceCentroidOverestimate, 0.05);
}

// P12-MESH-001 backward compatibility: on a Cartesian mesh the segment
// distance equals the pre-P12-MESH-001 face-centroid distance bit for bit
// (the nearest face's projection parameter is exactly zero).
TEST(WallDistanceTest, CartesianDistanceIsBitIdenticalToFaceCentroidDistance) {
  const Mesh mesh = MeshGeometry::createCartesian2D(7, 5, 2.0, 0.9);
  BoundaryConditionSet velocityBoundaries;
  for (const char* patch : {"left", "right", "bottom", "top"}) {
    velocityBoundaries.set(mesh, patch, std::make_unique<Wall>());
  }
  const auto distance = computeWallDistance(mesh, velocityBoundaries);
  for (const auto& cell : mesh.cells()) {
    cfd::Real faceCentroidDistance = std::numeric_limits<cfd::Real>::infinity();
    for (const auto& face : mesh.faces()) {
      if (face.isBoundary()) {
        faceCentroidDistance = std::min(faceCentroidDistance,
                                        MeshGeometry::distance(cell.centroid(), face.centroid()));
      }
    }
    EXPECT_EQ(distance[cell.id()], faceCentroidDistance) << "cell " << cell.id();
  }
}
