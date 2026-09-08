#include <gtest/gtest.h>

#include <limits>
#include <set>
#include <vector>

#include "cfd/core/Exception.hpp"
#include "cfd/core/Vector2.hpp"
#include "cfd/mesh/Mesh.hpp"
#include "cfd/mesh/MeshGeometry.hpp"

namespace {

using cfd::Index;
using cfd::Real;
using cfd::mesh::Mesh;
using cfd::mesh::MeshGeometry;

// Runs every topology/geometry invariant from the P0 -- Mesh spec against
// one generated grid. Shared across every grid-size test below so each
// only needs to state its expected inputs once.
void checkInvariants(const Mesh& mesh, Index nx, Index ny, Real lengthX, Real lengthY) {
  const Real dx = lengthX / static_cast<Real>(nx);
  const Real dy = lengthY / static_cast<Real>(ny);

  const Index expectedCells = nx * ny;
  const Index expectedFaces = ((nx + 1) * ny) + (nx * (ny + 1));
  const Index expectedBoundary = (2 * nx) + (2 * ny);
  const Index expectedInternal = expectedFaces - expectedBoundary;

  ASSERT_EQ(mesh.numberOfCells(), expectedCells);
  ASSERT_EQ(mesh.numberOfFaces(), expectedFaces);
  EXPECT_EQ(expectedFaces, expectedInternal + expectedBoundary);

  // --- Volumes -----------------------------------------------------------
  Real totalVolume = 0.0;
  for (const auto& cell : mesh.cells()) {
    EXPECT_NEAR(cell.volume(), dx * dy, 1e-9);
    totalVolume += cell.volume();
  }
  EXPECT_NEAR(totalVolume, lengthX * lengthY, 1e-9);

  // --- Face areas, owner/neighbor validity, internal-face orientation ----
  Index internalCount = 0;
  Index boundaryCount = 0;
  for (const auto& face : mesh.faces()) {
    EXPECT_GT(face.area(), 0.0);
    ASSERT_LT(face.owner(), mesh.numberOfCells());

    if (face.isBoundary()) {
      ++boundaryCount;
    } else {
      ++internalCount;
      ASSERT_LT(*face.neighbor(), mesh.numberOfCells());
      EXPECT_NE(*face.neighbor(), face.owner());

      const auto& ownerCentroid = mesh.cell(face.owner()).centroid();
      const auto& neighborCentroid = mesh.cell(*face.neighbor()).centroid();
      const cfd::Vector2 ownerToNeighbor = neighborCentroid - ownerCentroid;
      EXPECT_GT(cfd::dot(face.areaVector(), ownerToNeighbor), 0.0);
    }
  }
  EXPECT_EQ(internalCount, expectedInternal);
  EXPECT_EQ(boundaryCount, expectedBoundary);

  // --- Every internal face touched by exactly two cells; boundary by one -
  std::vector<int> touchCount(mesh.numberOfFaces(), 0);
  for (const auto& cell : mesh.cells()) {
    for (const Index faceId : cell.faceIds()) {
      ++touchCount[faceId];
    }
  }
  for (const auto& face : mesh.faces()) {
    EXPECT_EQ(touchCount[face.id()], face.isBoundary() ? 1 : 2);
  }

  // --- Closed-cell geometry: sum of outward area vectors per cell ~ 0 ----
  for (const auto& cell : mesh.cells()) {
    ASSERT_EQ(cell.faceIds().size(), 4U);
    cfd::Vector2 sum{0.0, 0.0};
    for (const Index faceId : cell.faceIds()) {
      const auto& face = mesh.face(faceId);
      const cfd::Vector2 outward =
          (face.owner() == cell.id()) ? face.areaVector() : (face.areaVector() * -1.0);
      sum += outward;
    }
    EXPECT_NEAR(sum.x, 0.0, 1e-9);
    EXPECT_NEAR(sum.y, 0.0, 1e-9);
  }

  // --- Boundary patches ----------------------------------------------------
  ASSERT_EQ(mesh.boundaryPatches().size(), 4U);
  EXPECT_EQ(mesh.boundaryPatch("left").size(), ny);
  EXPECT_EQ(mesh.boundaryPatch("right").size(), ny);
  EXPECT_EQ(mesh.boundaryPatch("bottom").size(), nx);
  EXPECT_EQ(mesh.boundaryPatch("top").size(), nx);

  std::set<Index> patchedBoundaryFaces;
  for (const auto& patch : mesh.boundaryPatches()) {
    for (const Index faceId : patch.faceIds()) {
      EXPECT_TRUE(patchedBoundaryFaces.insert(faceId).second)
          << "face " << faceId << " belongs to more than one patch";
    }
  }
  EXPECT_EQ(patchedBoundaryFaces.size(), expectedBoundary);
}

}  // namespace

TEST(CartesianMesh, OneByOne) {
  const Mesh mesh = MeshGeometry::createCartesian2D(1, 1, 1.0, 1.0);
  checkInvariants(mesh, 1, 1, 1.0, 1.0);

  EXPECT_EQ(mesh.numberOfCells(), 1U);
  EXPECT_EQ(mesh.numberOfFaces(), 4U);
  for (const auto& face : mesh.faces()) {
    EXPECT_TRUE(face.isBoundary());
  }
}

TEST(CartesianMesh, TwoByOne) {
  const Mesh mesh = MeshGeometry::createCartesian2D(2, 1, 1.0, 1.0);
  checkInvariants(mesh, 2, 1, 1.0, 1.0);

  EXPECT_EQ(mesh.numberOfCells(), 2U);
  EXPECT_EQ(mesh.numberOfFaces(), 7U);

  bool foundInternal = false;
  for (const auto& face : mesh.faces()) {
    if (!face.isBoundary()) {
      EXPECT_EQ(face.owner(), 0U);
      ASSERT_TRUE(face.neighbor().has_value());
      EXPECT_EQ(*face.neighbor(), 1U);
      foundInternal = true;
    }
  }
  EXPECT_TRUE(foundInternal);
}

TEST(CartesianMesh, OneByTwo) {
  const Mesh mesh = MeshGeometry::createCartesian2D(1, 2, 1.0, 1.0);
  checkInvariants(mesh, 1, 2, 1.0, 1.0);

  EXPECT_EQ(mesh.numberOfCells(), 2U);
  EXPECT_EQ(mesh.numberOfFaces(), 7U);
}

TEST(CartesianMesh, TwoByTwo) {
  const Mesh mesh = MeshGeometry::createCartesian2D(2, 2, 1.0, 1.0);
  checkInvariants(mesh, 2, 2, 1.0, 1.0);

  EXPECT_EQ(mesh.numberOfCells(), 4U);
  EXPECT_EQ(mesh.numberOfFaces(), 12U);

  // Documented cell centers/volumes for this exact case.
  struct Expected {
    Real x;
    Real y;
  };
  const Expected expectedCenters[4] = {{0.25, 0.25}, {0.75, 0.25}, {0.25, 0.75}, {0.75, 0.75}};
  for (Index id = 0; id < 4; ++id) {
    const auto& cell = mesh.cell(id);
    EXPECT_NEAR(cell.centroid().x, expectedCenters[id].x, 1e-12);
    EXPECT_NEAR(cell.centroid().y, expectedCenters[id].y, 1e-12);
    EXPECT_NEAR(cell.volume(), 0.25, 1e-12);
  }
}

TEST(CartesianMesh, FourByThree) {
  const Mesh mesh = MeshGeometry::createCartesian2D(4, 3, 2.0, 1.5);
  checkInvariants(mesh, 4, 3, 2.0, 1.5);
  EXPECT_EQ(mesh.numberOfCells(), 12U);
}

TEST(CartesianMesh, TwentyByTwenty) {
  const Mesh mesh = MeshGeometry::createCartesian2D(20, 20, 1.0, 1.0);
  checkInvariants(mesh, 20, 20, 1.0, 1.0);
  EXPECT_EQ(mesh.numberOfCells(), 400U);
}

TEST(CartesianMesh, DeterministicGeneration) {
  const Mesh a = MeshGeometry::createCartesian2D(5, 4, 3.0, 2.0);
  const Mesh b = MeshGeometry::createCartesian2D(5, 4, 3.0, 2.0);

  ASSERT_EQ(a.numberOfCells(), b.numberOfCells());
  ASSERT_EQ(a.numberOfFaces(), b.numberOfFaces());
  for (Index id = 0; id < a.numberOfCells(); ++id) {
    EXPECT_DOUBLE_EQ(a.cell(id).centroid().x, b.cell(id).centroid().x);
    EXPECT_DOUBLE_EQ(a.cell(id).centroid().y, b.cell(id).centroid().y);
  }
  for (Index id = 0; id < a.numberOfFaces(); ++id) {
    EXPECT_EQ(a.face(id).owner(), b.face(id).owner());
    EXPECT_EQ(a.face(id).neighbor(), b.face(id).neighbor());
  }
}

TEST(CartesianMesh, RejectsInvalidInputs) {
  EXPECT_THROW((void)MeshGeometry::createCartesian2D(0, 1, 1.0, 1.0), cfd::InvalidArgumentError);
  EXPECT_THROW((void)MeshGeometry::createCartesian2D(1, 0, 1.0, 1.0), cfd::InvalidArgumentError);
  EXPECT_THROW((void)MeshGeometry::createCartesian2D(1, 1, 0.0, 1.0), cfd::InvalidArgumentError);
  EXPECT_THROW((void)MeshGeometry::createCartesian2D(1, 1, 1.0, -1.0), cfd::InvalidArgumentError);

  const Real nan = std::numeric_limits<Real>::quiet_NaN();
  const Real inf = std::numeric_limits<Real>::infinity();
  EXPECT_THROW((void)MeshGeometry::createCartesian2D(1, 1, nan, 1.0), cfd::InvalidArgumentError);
  EXPECT_THROW((void)MeshGeometry::createCartesian2D(1, 1, 1.0, inf), cfd::InvalidArgumentError);
}
