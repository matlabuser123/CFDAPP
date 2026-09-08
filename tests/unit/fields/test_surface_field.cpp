#include <gtest/gtest.h>

#include "cfd/fields/SurfaceField.hpp"
#include "cfd/mesh/MeshGeometry.hpp"

using cfd::fields::SurfaceField;

TEST(SurfaceFieldTest, SizedConstructionAndFill) {
  SurfaceField phi(5, 0.0);
  EXPECT_EQ(phi.size(), 5U);
  phi.fill(1.5);
  for (SurfaceField::size_type i = 0; i < phi.size(); ++i) {
    EXPECT_DOUBLE_EQ(phi[i], 1.5);
  }
}

TEST(SurfaceFieldTest, SizeMatchesOneByOneMeshFaceCount) {
  const cfd::mesh::Mesh mesh = cfd::mesh::MeshGeometry::createCartesian2D(1, 1, 1.0, 1.0);
  const SurfaceField phi(mesh.numberOfFaces(), 0.0);
  EXPECT_EQ(phi.size(), 4U);
}

TEST(SurfaceFieldTest, SizeMatchesTwoByTwoMeshFaceCount) {
  const cfd::mesh::Mesh mesh = cfd::mesh::MeshGeometry::createCartesian2D(2, 2, 1.0, 1.0);
  const SurfaceField phi(mesh.numberOfFaces(), 0.0);
  EXPECT_EQ(phi.size(), 12U);
}
