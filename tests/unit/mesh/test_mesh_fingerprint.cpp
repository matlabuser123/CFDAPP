#include <gtest/gtest.h>

#include "cfd/mesh/MeshFingerprint.hpp"
#include "cfd/mesh/MeshGeometry.hpp"

using cfd::mesh::computeMeshFingerprint;
using cfd::mesh::Mesh;
using cfd::mesh::MeshGeometry;

TEST(MeshFingerprintTest, IsNonEmpty) {
  const Mesh mesh = MeshGeometry::createCartesian2D(4, 4, 1.0, 1.0);
  EXPECT_FALSE(computeMeshFingerprint(mesh).empty());
}

TEST(MeshFingerprintTest, TwoSeparatelyBuiltIdenticalMeshesProduceIdenticalFingerprints) {
  const Mesh meshA = MeshGeometry::createCartesian2D(4, 4, 1.0, 1.0);
  const Mesh meshB = MeshGeometry::createCartesian2D(4, 4, 1.0, 1.0);
  EXPECT_EQ(computeMeshFingerprint(meshA), computeMeshFingerprint(meshB));
}

TEST(MeshFingerprintTest, RepeatedCallOnSameMeshIsDeterministic) {
  const Mesh mesh = MeshGeometry::createCartesian2D(4, 4, 1.0, 1.0);
  EXPECT_EQ(computeMeshFingerprint(mesh), computeMeshFingerprint(mesh));
}

TEST(MeshFingerprintTest, DifferentDomainSizeWithSameCellAndFaceCountProducesDifferentFingerprint) {
  // Same nx*ny topology (same cellCount/faceCount) but a wider domain --
  // different cell volumes, face centroids, and area vectors. This is
  // exactly the case cellCount/faceCount alone cannot distinguish.
  const Mesh meshA = MeshGeometry::createCartesian2D(4, 4, 1.0, 1.0);
  const Mesh meshB = MeshGeometry::createCartesian2D(4, 4, 2.0, 1.0);
  ASSERT_EQ(meshA.numberOfCells(), meshB.numberOfCells());
  ASSERT_EQ(meshA.numberOfFaces(), meshB.numberOfFaces());

  EXPECT_NE(computeMeshFingerprint(meshA), computeMeshFingerprint(meshB));
}

TEST(MeshFingerprintTest, DifferentResolutionProducesDifferentFingerprint) {
  const Mesh coarse = MeshGeometry::createCartesian2D(4, 4, 1.0, 1.0);
  const Mesh fine = MeshGeometry::createCartesian2D(8, 8, 1.0, 1.0);
  EXPECT_NE(computeMeshFingerprint(coarse), computeMeshFingerprint(fine));
}

TEST(MeshFingerprintTest, DifferentAspectRatioSameCellCountProducesDifferentFingerprint) {
  // 4x4 and 2x8 both have 16 cells, but different topology (row length,
  // hence different boundary patch face counts and internal connectivity).
  const Mesh meshA = MeshGeometry::createCartesian2D(4, 4, 1.0, 1.0);
  const Mesh meshB = MeshGeometry::createCartesian2D(2, 8, 1.0, 1.0);
  ASSERT_EQ(meshA.numberOfCells(), meshB.numberOfCells());

  EXPECT_NE(computeMeshFingerprint(meshA), computeMeshFingerprint(meshB));
}
