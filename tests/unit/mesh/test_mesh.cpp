#include <gtest/gtest.h>

#include <vector>

#include "cfd/core/Exception.hpp"
#include "cfd/mesh/Mesh.hpp"
#include "cfd/mesh/MeshGeometry.hpp"

TEST(Mesh, ExposesCellsFacesAndPatches) {
  const cfd::mesh::Mesh mesh = cfd::mesh::MeshGeometry::createCartesian2D(2, 1, 1.0, 1.0);

  EXPECT_EQ(mesh.numberOfCells(), 2U);
  EXPECT_EQ(mesh.numberOfFaces(), 7U);
  EXPECT_EQ(mesh.boundaryPatches().size(), 4U);
}

TEST(Mesh, CellAndFaceAccessorsValidateId) {
  const cfd::mesh::Mesh mesh = cfd::mesh::MeshGeometry::createCartesian2D(1, 1, 1.0, 1.0);

  EXPECT_NO_THROW((void)mesh.cell(0));
  EXPECT_THROW((void)mesh.cell(1), cfd::InvalidArgumentError);
  EXPECT_THROW((void)mesh.face(100), cfd::InvalidArgumentError);
}

TEST(Mesh, BoundaryPatchLookupByName) {
  const cfd::mesh::Mesh mesh = cfd::mesh::MeshGeometry::createCartesian2D(1, 1, 1.0, 1.0);

  EXPECT_NO_THROW((void)mesh.boundaryPatch("left"));
  EXPECT_THROW((void)mesh.boundaryPatch("does-not-exist"), cfd::InvalidArgumentError);
}

TEST(Mesh, RejectsEmptyCellsOrFaces) {
  EXPECT_THROW(cfd::mesh::Mesh({}, {}, {}), cfd::InvalidArgumentError);
}
