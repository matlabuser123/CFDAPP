#include <gtest/gtest.h>

#include <cmath>
#include <cstdio>
#include <optional>
#include <utility>
#include <vector>

#include "DistortedMesh.hpp"
#include "cfd/mesh/Cell.hpp"
#include "cfd/mesh/Face.hpp"
#include "cfd/mesh/Mesh.hpp"
#include "cfd/mesh/MeshGeometry.hpp"
#include "cfd/mesh/MeshQuality.hpp"

TEST(MeshQuality, UniformSquareGridIsValidWithUnitAspectRatio) {
  const cfd::mesh::Mesh mesh = cfd::mesh::MeshGeometry::createCartesian2D(4, 4, 1.0, 1.0);
  const cfd::mesh::MeshQualityReport report = cfd::mesh::MeshQuality::evaluate(mesh);

  EXPECT_TRUE(report.valid);
  EXPECT_NEAR(report.minimumVolume, report.maximumVolume, 1e-12);
  EXPECT_GT(report.minimumFaceArea, 0.0);
  EXPECT_NEAR(report.maximumAspectRatio, 1.0, 1e-12);
}

TEST(MeshQuality, NonSquareCellsReportCorrectAspectRatio) {
  // dx = 1/2 = 0.5, dy = 1/4 = 0.25 -> aspect ratio = 0.5 / 0.25 = 2.0
  const cfd::mesh::Mesh mesh = cfd::mesh::MeshGeometry::createCartesian2D(2, 4, 1.0, 1.0);
  const cfd::mesh::MeshQualityReport report = cfd::mesh::MeshQuality::evaluate(mesh);

  EXPECT_TRUE(report.valid);
  EXPECT_NEAR(report.maximumAspectRatio, 2.0, 1e-12);
}

// P12-NUM-003: a Cartesian mesh is exactly orthogonal (every internal
// face's area vector is exactly parallel to its owner-neighbor line) and
// exactly unskewed (the owner-neighbor line crosses each face exactly at
// its own centroid) -- both metrics must be exactly zero, not merely
// small, since MeshGeometry::decomposeFaceArea/skewness are constructed
// so a Cartesian mesh reduces the "non-orthogonal part" to the zero
// vector algebraically (S_nonorth = Sf - (Sf.Sf/d.Sf)*d == Sf - Sf when
// d || Sf exactly).
TEST(MeshQuality, CartesianHasZeroNonOrthogonality) {
  const cfd::mesh::Mesh mesh = cfd::mesh::MeshGeometry::createCartesian2D(6, 6, 1.0, 1.0);
  const cfd::mesh::MeshQualityReport report = cfd::mesh::MeshQuality::evaluate(mesh);

  EXPECT_TRUE(report.valid);
  EXPECT_NEAR(report.maxNonOrthogonalityDegrees, 0.0, 1e-10);
  EXPECT_NEAR(report.meanNonOrthogonalityDegrees, 0.0, 1e-10);
}

TEST(MeshQuality, CartesianHasZeroSkewness) {
  const cfd::mesh::Mesh mesh = cfd::mesh::MeshGeometry::createCartesian2D(6, 6, 1.0, 1.0);
  const cfd::mesh::MeshQualityReport report = cfd::mesh::MeshQuality::evaluate(mesh);

  EXPECT_TRUE(report.valid);
  EXPECT_NEAR(report.maxSkewness, 0.0, 1e-10);
  EXPECT_NEAR(report.meanSkewness, 0.0, 1e-10);
}

// A geometrically distorted mesh must report genuinely nonzero, distinct
// non-orthogonality and skewness -- confirming the two metrics actually
// discriminate a non-Cartesian mesh from a Cartesian one (not just
// structurally present but numerically inert). Bounds are loose brackets
// around the actual measured values for this exact mesh/amplitude
// (createDistortedQuad2D(8, 8, 1.0, 1.0, 0.3*(1.0/8.0)): max angle
// ~13.57 deg, mean angle ~6.04 deg, max skew ~0.0465, mean skew ~0.0126),
// not a re-derivation of the formula.
TEST(MeshQuality, DistortedMeshReportsNonZeroMetrics) {
  constexpr cfd::Index nx = 8;
  constexpr cfd::Index ny = 8;
  constexpr cfd::Real amplitude = 0.3 * (1.0 / static_cast<cfd::Real>(nx));
  const cfd::mesh::Mesh mesh = cfd::test::createDistortedQuad2D(nx, ny, 1.0, 1.0, amplitude);
  const cfd::mesh::MeshQualityReport report = cfd::mesh::MeshQuality::evaluate(mesh);

  EXPECT_TRUE(report.valid);
  EXPECT_GT(report.maxNonOrthogonalityDegrees, 5.0);
  EXPECT_LT(report.maxNonOrthogonalityDegrees, 30.0);
  EXPECT_GT(report.meanNonOrthogonalityDegrees, 1.0);
  EXPECT_LT(report.meanNonOrthogonalityDegrees, report.maxNonOrthogonalityDegrees);

  EXPECT_GT(report.maxSkewness, 0.01);
  EXPECT_LT(report.maxSkewness, 0.5);
  EXPECT_GT(report.meanSkewness, 0.001);
  EXPECT_LT(report.meanSkewness, report.maxSkewness);
}

// Distortion levels used across the P12-NUM-003 verification (amplitude
// as a fraction of the cell spacing h = 1/16 on a 16x16 mesh): Cartesian
// (0), mild (0.10h), moderate (0.25h), strong-but-valid (0.45h). Each
// mesh must stay geometrically valid (positive cell volumes, finite
// geometry, valid connectivity, every internal face's decomposition
// well-posed), and both metrics must strictly increase with the
// distortion level -- i.e. they actually discriminate the levels. The
// measured table is printed as evidence (see results/p12-num-003/
// summary.md).
TEST(MeshQuality, MetricsDiscriminateDistortionLevels) {
  constexpr cfd::Index n = 16;
  constexpr cfd::Real h = 1.0 / static_cast<cfd::Real>(n);
  const struct {
    const char* label;
    cfd::Real fraction;
  } levels[] = {{"cartesian", 0.0}, {"mild", 0.10}, {"moderate", 0.25}, {"strong", 0.45}};

  std::printf("\n%-10s %-9s %-14s %-14s %-12s %-12s %-12s\n", "level", "amp/h", "maxNonOrth",
              "meanNonOrth", "maxSkew", "meanSkew", "minVolume");
  cfd::mesh::MeshQualityReport previous{};
  bool first = true;
  for (const auto& level : levels) {
    const cfd::mesh::Mesh mesh =
        cfd::test::createDistortedQuad2D(n, n, 1.0, 1.0, level.fraction * h);
    const cfd::mesh::MeshQualityReport report = cfd::mesh::MeshQuality::evaluate(mesh);
    std::printf("%-10s %-9.2f %-14.6g %-14.6g %-12.6g %-12.6g %-12.6g\n", level.label,
                level.fraction, report.maxNonOrthogonalityDegrees,
                report.meanNonOrthogonalityDegrees, report.maxSkewness, report.meanSkewness,
                report.minimumVolume);

    EXPECT_TRUE(report.valid) << level.label;
    EXPECT_GT(report.minimumVolume, 0.0) << level.label;
    EXPECT_TRUE(std::isfinite(report.maxNonOrthogonalityDegrees)) << level.label;
    EXPECT_TRUE(std::isfinite(report.maxSkewness)) << level.label;
    for (const auto& face : mesh.faces()) {
      if (!face.isBoundary()) {
        EXPECT_TRUE(cfd::mesh::MeshGeometry::decomposeFaceArea(mesh, face).valid) << level.label;
      }
    }
    if (!first) {
      EXPECT_GT(report.maxNonOrthogonalityDegrees, previous.maxNonOrthogonalityDegrees)
          << level.label;
      EXPECT_GT(report.meanNonOrthogonalityDegrees, previous.meanNonOrthogonalityDegrees)
          << level.label;
      EXPECT_GT(report.maxSkewness, previous.maxSkewness) << level.label;
      EXPECT_GT(report.meanSkewness, previous.meanSkewness) << level.label;
    }
    previous = report;
    first = false;
  }
  std::fflush(stdout);
}

// Purely deterministic: evaluating the same mesh twice (or two
// independently-constructed but geometrically identical meshes) must
// yield bit-identical metrics -- no RNG, no iteration-order dependence.
TEST(MeshQuality, NonOrthogonalityMetricsAreDeterministic) {
  const cfd::mesh::Mesh meshA = cfd::test::createDistortedQuad2D(5, 5, 1.0, 1.0, 0.05);
  const cfd::mesh::Mesh meshB = cfd::test::createDistortedQuad2D(5, 5, 1.0, 1.0, 0.05);

  const cfd::mesh::MeshQualityReport reportA = cfd::mesh::MeshQuality::evaluate(meshA);
  const cfd::mesh::MeshQualityReport reportB = cfd::mesh::MeshQuality::evaluate(meshB);

  EXPECT_EQ(reportA.maxNonOrthogonalityDegrees, reportB.maxNonOrthogonalityDegrees);
  EXPECT_EQ(reportA.meanNonOrthogonalityDegrees, reportB.meanNonOrthogonalityDegrees);
  EXPECT_EQ(reportA.maxSkewness, reportB.maxSkewness);
  EXPECT_EQ(reportA.meanSkewness, reportB.meanSkewness);

  const cfd::mesh::MeshQualityReport reportA2 = cfd::mesh::MeshQuality::evaluate(meshA);
  EXPECT_EQ(reportA.maxNonOrthogonalityDegrees, reportA2.maxNonOrthogonalityDegrees);
  EXPECT_EQ(reportA.maxSkewness, reportA2.maxSkewness);
}

// A mesh with a degenerate internal face (zero owner-neighbor distance,
// via two coincident cell centroids) must be reported invalid rather than
// producing NaN/Inf non-orthogonality/skewness metrics -- exercising
// MeshGeometry::decomposeFaceArea's own well-posedness guard through
// MeshQuality::evaluate.
TEST(MeshQuality, InvalidGeometryHandledSafely) {
  std::vector<cfd::mesh::Cell> cells;
  cells.emplace_back(0, cfd::Vector2{0.5, 0.5}, 1.0);
  // Coincident centroid -- owner-neighbor distance is exactly zero.
  cells.emplace_back(1, cfd::Vector2{0.5, 0.5}, 1.0);

  std::vector<cfd::mesh::Face> faces;
  faces.emplace_back(0, 0, 1, cfd::Vector2{0.5, 0.5}, cfd::Vector2{1.0, 0.0});

  const cfd::mesh::Mesh mesh(std::move(cells), std::move(faces), {});
  const cfd::mesh::MeshQualityReport report = cfd::mesh::MeshQuality::evaluate(mesh);

  EXPECT_FALSE(report.valid);
  EXPECT_TRUE(std::isfinite(report.maxNonOrthogonalityDegrees));
  EXPECT_TRUE(std::isfinite(report.maxSkewness));
}

TEST(MeshQuality, DetectsOutOfRangeOwner) {
  std::vector<cfd::mesh::Cell> cells;
  cells.emplace_back(0, cfd::Vector2{0.5, 0.5}, 1.0);

  std::vector<cfd::mesh::Face> faces;
  // Owner id 5 does not exist -- only cell 0 was created.
  faces.emplace_back(0, 5, std::nullopt, cfd::Vector2{0.0, 0.5}, cfd::Vector2{-1.0, 0.0});

  const cfd::mesh::Mesh mesh(std::move(cells), std::move(faces), {});
  const cfd::mesh::MeshQualityReport report = cfd::mesh::MeshQuality::evaluate(mesh);

  EXPECT_FALSE(report.valid);
}
