#include <gtest/gtest.h>

#include "cfd/core/Constants.hpp"
#include "cfd/core/Exception.hpp"
#include "cfd/mesh/Mesh.hpp"
#include "cfd/mesh/MeshGeometry.hpp"

TEST(MeshGeometry, DistanceAndDisplacement) {
  const cfd::Vector2 a{0.0, 0.0};
  const cfd::Vector2 b{3.0, 4.0};

  EXPECT_DOUBLE_EQ(cfd::mesh::MeshGeometry::distance(a, b), 5.0);

  const cfd::Vector2 d = cfd::mesh::MeshGeometry::displacement(a, b);
  EXPECT_DOUBLE_EQ(d.x, 3.0);
  EXPECT_DOUBLE_EQ(d.y, 4.0);
}

TEST(MeshGeometry, UnitNormalHasUnitMagnitude) {
  const cfd::mesh::Mesh mesh = cfd::mesh::MeshGeometry::createCartesian2D(1, 1, 1.0, 1.0);
  for (const auto& face : mesh.faces()) {
    const cfd::Vector2 n = cfd::mesh::MeshGeometry::unitNormal(face);
    EXPECT_NEAR(cfd::magnitude(n), 1.0, cfd::constants::small);
  }
}

TEST(MeshGeometry, OwnerNeighborDistanceMatchesCartesianSpacing) {
  const cfd::mesh::Mesh mesh = cfd::mesh::MeshGeometry::createCartesian2D(2, 1, 1.0, 1.0);
  // nx=2 -> dx=0.5; the single internal (vertical) face separates cell
  // centers exactly dx apart.
  for (const auto& face : mesh.faces()) {
    if (!face.isBoundary()) {
      EXPECT_NEAR(cfd::mesh::MeshGeometry::ownerNeighborDistance(mesh, face), 0.5, 1e-12);
    }
  }
}

TEST(MeshGeometry, OwnerNeighborDistanceRejectsBoundaryFace) {
  const cfd::mesh::Mesh mesh = cfd::mesh::MeshGeometry::createCartesian2D(1, 1, 1.0, 1.0);
  for (const auto& face : mesh.faces()) {
    EXPECT_THROW((void)cfd::mesh::MeshGeometry::ownerNeighborDistance(mesh, face),
                 cfd::InvalidArgumentError);
  }
}
