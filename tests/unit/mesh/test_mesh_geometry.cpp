#include <gtest/gtest.h>

#include <cmath>
#include <utility>
#include <vector>

#include "DistortedMesh.hpp"
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

// ===========================================================================
// P12-NUM-003: non-orthogonal face decomposition, non-orthogonality angle,
// skewness, owner-neighbor crossing -- verification against hand-derived
// geometry (not a re-derivation of the implementation's own formula).
// ===========================================================================

namespace {

// Two cells joined by one internal face with the given centroid/area
// vector -- enough geometry for every per-face function here, with no
// structured-mesh assumptions at all.
cfd::mesh::Mesh twoCellMesh(cfd::Vector2 ownerCentroid, cfd::Vector2 neighborCentroid,
                            cfd::Vector2 faceCentroid, cfd::Vector2 areaVector) {
  std::vector<cfd::mesh::Cell> cells;
  cells.emplace_back(0, ownerCentroid, 1.0);
  cells.emplace_back(1, neighborCentroid, 1.0);
  std::vector<cfd::mesh::Face> faces;
  faces.emplace_back(0, 0, 1, faceCentroid, areaVector);
  cells[0].addFace(0);
  cells[1].addFace(0);
  return cfd::mesh::Mesh(std::move(cells), std::move(faces), {});
}

}  // namespace

// Cartesian: S_orth == Sf and S_nonorth == {0,0} BIT-FOR-BIT on every
// internal face (the documented exact-parallel guarantee, see
// decomposeFaceArea), angle exactly 0, skewness zero.
TEST(MeshGeometryNonOrthogonal, CartesianFacesDecomposeExactly) {
  const cfd::mesh::Mesh mesh = cfd::mesh::MeshGeometry::createCartesian2D(5, 3, 1.0, 0.7);
  cfd::Index internal = 0;
  for (const auto& face : mesh.faces()) {
    if (face.isBoundary()) {
      continue;
    }
    ++internal;
    const auto decomposition = cfd::mesh::MeshGeometry::decomposeFaceArea(mesh, face);
    ASSERT_TRUE(decomposition.valid);
    EXPECT_EQ(decomposition.orthogonal.x, face.areaVector().x);
    EXPECT_EQ(decomposition.orthogonal.y, face.areaVector().y);
    EXPECT_EQ(decomposition.nonOrthogonal.x, 0.0);
    EXPECT_EQ(decomposition.nonOrthogonal.y, 0.0);
    EXPECT_EQ(cfd::mesh::MeshGeometry::nonOrthogonalityAngleDegrees(mesh, face), 0.0);
    const auto skew = cfd::mesh::MeshGeometry::skewness(mesh, face);
    ASSERT_TRUE(skew.has_value());
    EXPECT_NEAR(*skew, 0.0, 1e-14);
  }
  EXPECT_GT(internal, 0u);
}

// Non-orthogonal but UNSKEWED: d = (1,0), Sf at 30 degrees to d, face
// centroid exactly on the owner-neighbor line. By hand: angle = 30 deg,
// |S_orth| = |Sf|/cos(30) (over-relaxed), S_orth || d, S_orth + S_nonorth
// = Sf, crossing point = face centroid (t = 0.5), skewness = 0.
TEST(MeshGeometryNonOrthogonal, NonOrthogonalButUnskewedFace) {
  const cfd::Real theta = cfd::constants::pi / 6.0;
  const cfd::Vector2 sf{std::cos(theta), std::sin(theta)};
  const auto mesh = twoCellMesh({0.0, 0.0}, {1.0, 0.0}, {0.5, 0.0}, sf);
  const auto& face = mesh.face(0);

  EXPECT_NEAR(cfd::mesh::MeshGeometry::nonOrthogonalityAngleDegrees(mesh, face), 30.0, 1e-12);

  const auto decomposition = cfd::mesh::MeshGeometry::decomposeFaceArea(mesh, face);
  ASSERT_TRUE(decomposition.valid);
  EXPECT_NEAR(decomposition.orthogonal.x, 1.0 / std::cos(theta), 1e-14);
  EXPECT_NEAR(decomposition.orthogonal.y, 0.0, 1e-14);
  EXPECT_NEAR(decomposition.orthogonal.x + decomposition.nonOrthogonal.x, sf.x, 1e-14);
  EXPECT_NEAR(decomposition.orthogonal.y + decomposition.nonOrthogonal.y, sf.y, 1e-14);

  const auto crossing = cfd::mesh::MeshGeometry::ownerNeighborCrossing(mesh, face);
  ASSERT_TRUE(crossing.has_value());
  EXPECT_NEAR(crossing->t, 0.5, 1e-14);
  EXPECT_NEAR(cfd::magnitude(crossing->skewVector), 0.0, 1e-14);
  EXPECT_NEAR(*cfd::mesh::MeshGeometry::skewness(mesh, face), 0.0, 1e-14);
}

// Orthogonal but SKEWED: Sf || d exactly, but the face centroid sits 0.3
// off the owner-neighbor line. By hand: angle = 0, S_nonorth = 0 exactly,
// crossing at (0.5, 0), skew vector (0, 0.3), skewness = 0.3 / |d| = 0.3.
// Together with the test above this shows the two metrics measure
// DISTINCT geometric effects -- each can be nonzero while the other is
// zero.
TEST(MeshGeometryNonOrthogonal, OrthogonalButSkewedFace) {
  const auto mesh = twoCellMesh({0.0, 0.0}, {1.0, 0.0}, {0.5, 0.3}, {1.0, 0.0});
  const auto& face = mesh.face(0);

  EXPECT_EQ(cfd::mesh::MeshGeometry::nonOrthogonalityAngleDegrees(mesh, face), 0.0);
  const auto decomposition = cfd::mesh::MeshGeometry::decomposeFaceArea(mesh, face);
  ASSERT_TRUE(decomposition.valid);
  EXPECT_EQ(decomposition.nonOrthogonal.x, 0.0);
  EXPECT_EQ(decomposition.nonOrthogonal.y, 0.0);

  const auto crossing = cfd::mesh::MeshGeometry::ownerNeighborCrossing(mesh, face);
  ASSERT_TRUE(crossing.has_value());
  EXPECT_NEAR(crossing->crossingPoint.x, 0.5, 1e-14);
  EXPECT_NEAR(crossing->crossingPoint.y, 0.0, 1e-14);
  EXPECT_NEAR(crossing->skewVector.x, 0.0, 1e-14);
  EXPECT_NEAR(crossing->skewVector.y, 0.3, 1e-14);
  EXPECT_NEAR(*cfd::mesh::MeshGeometry::skewness(mesh, face), 0.3, 1e-14);
}

// Over-relaxed property on a genuinely distorted mesh: S_orth || d,
// |S_orth| >= |Sf| (never a weaker implicit coefficient than the plain
// orthogonal one), exact reconstruction S_orth + S_nonorth = Sf.
TEST(MeshGeometryNonOrthogonal, DistortedDecompositionIsConsistent) {
  const cfd::mesh::Mesh mesh = cfd::test::createDistortedQuad2D(8, 8, 1.0, 1.0, 0.3 / 8.0);
  cfd::Index nonOrthogonalFaces = 0;
  for (const auto& face : mesh.faces()) {
    if (face.isBoundary()) {
      continue;
    }
    const auto decomposition = cfd::mesh::MeshGeometry::decomposeFaceArea(mesh, face);
    ASSERT_TRUE(decomposition.valid) << "face " << face.id();
    const cfd::Vector2 d =
        mesh.cell(*face.neighbor()).centroid() - mesh.cell(face.owner()).centroid();
    const cfd::Real scale = face.area();
    EXPECT_NEAR(decomposition.orthogonal.x + decomposition.nonOrthogonal.x, face.areaVector().x,
                1e-14 * scale);
    EXPECT_NEAR(decomposition.orthogonal.y + decomposition.nonOrthogonal.y, face.areaVector().y,
                1e-14 * scale);
    const cfd::Real cross = (decomposition.orthogonal.x * d.y) - (decomposition.orthogonal.y * d.x);
    EXPECT_NEAR(cross, 0.0, 1e-14 * scale * cfd::magnitude(d));
    EXPECT_GE(cfd::magnitude(decomposition.orthogonal), face.area() * (1.0 - 1e-14));
    if (cfd::magnitude(decomposition.nonOrthogonal) > 1e-6 * scale) {
      ++nonOrthogonalFaces;
    }
  }
  EXPECT_GT(nonOrthogonalFaces, 0u);
}

// Degenerate geometry is detected, never turned into NaN/Inf:
//   - Sf exactly perpendicular to d (90 degrees): decomposition invalid,
//     crossing/skewness undefined (nullopt); the angle itself is still a
//     well-defined 90 degrees.
//   - coincident owner/neighbor centroids (|d| = 0): decomposition
//     invalid, skewness nullopt, angle throws NumericalError.
//   - very small (but positive) face area: still valid and finite.
//   - boundary face: every per-face function throws InvalidArgumentError.
TEST(MeshGeometryNonOrthogonal, DegenerateGeometryIsDetected) {
  {
    const auto mesh = twoCellMesh({0.0, 0.0}, {1.0, 0.0}, {0.5, 0.0}, {0.0, 1.0});
    const auto& face = mesh.face(0);
    EXPECT_FALSE(cfd::mesh::MeshGeometry::decomposeFaceArea(mesh, face).valid);
    EXPECT_FALSE(cfd::mesh::MeshGeometry::ownerNeighborCrossing(mesh, face).has_value());
    EXPECT_FALSE(cfd::mesh::MeshGeometry::skewness(mesh, face).has_value());
    EXPECT_NEAR(cfd::mesh::MeshGeometry::nonOrthogonalityAngleDegrees(mesh, face), 90.0, 1e-12);
  }
  {
    const auto mesh = twoCellMesh({0.5, 0.5}, {0.5, 0.5}, {0.5, 0.5}, {1.0, 0.0});
    const auto& face = mesh.face(0);
    EXPECT_FALSE(cfd::mesh::MeshGeometry::decomposeFaceArea(mesh, face).valid);
    EXPECT_FALSE(cfd::mesh::MeshGeometry::skewness(mesh, face).has_value());
    EXPECT_THROW((void)cfd::mesh::MeshGeometry::nonOrthogonalityAngleDegrees(mesh, face),
                 cfd::NumericalError);
  }
  {
    const auto mesh = twoCellMesh({0.0, 0.0}, {1.0, 0.1}, {0.5, 0.05}, {1e-12, 0.0});
    const auto& face = mesh.face(0);
    const auto decomposition = cfd::mesh::MeshGeometry::decomposeFaceArea(mesh, face);
    ASSERT_TRUE(decomposition.valid);
    EXPECT_TRUE(std::isfinite(decomposition.orthogonal.x));
    EXPECT_TRUE(std::isfinite(decomposition.nonOrthogonal.y));
    EXPECT_TRUE(std::isfinite(*cfd::mesh::MeshGeometry::skewness(mesh, face)));
  }
  {
    const cfd::mesh::Mesh mesh = cfd::mesh::MeshGeometry::createCartesian2D(2, 2, 1.0, 1.0);
    for (const auto& face : mesh.faces()) {
      if (!face.isBoundary()) {
        continue;
      }
      EXPECT_THROW((void)cfd::mesh::MeshGeometry::decomposeFaceArea(mesh, face),
                   cfd::InvalidArgumentError);
      EXPECT_THROW((void)cfd::mesh::MeshGeometry::nonOrthogonalityAngleDegrees(mesh, face),
                   cfd::InvalidArgumentError);
      EXPECT_THROW((void)cfd::mesh::MeshGeometry::skewness(mesh, face), cfd::InvalidArgumentError);
      EXPECT_THROW((void)cfd::mesh::MeshGeometry::ownerNeighborCrossing(mesh, face),
                   cfd::InvalidArgumentError);
    }
  }
}
