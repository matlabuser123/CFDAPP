// P12-MESH-004 -- the production mesh-quality report (MeshQuality.hpp):
// metric definitions on meshes with known exact values, worst-entity
// identification, the valid / valid_with_warnings / invalid classification,
// and the fatal conditions on hand-built meshes (plus the Cell / Face
// constructor checks that stop the rest before a Mesh exists).

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "cfd/core/Exception.hpp"
#include "cfd/mesh/BoundaryPatch.hpp"
#include "cfd/mesh/Cell.hpp"
#include "cfd/mesh/Face.hpp"
#include "cfd/mesh/Mesh.hpp"
#include "cfd/mesh/MeshGeometry.hpp"
#include "cfd/mesh/MeshGrading.hpp"
#include "cfd/mesh/MeshQuality.hpp"
#include "cfd/mesh/MultiBlockSpec.hpp"

namespace {

using cfd::Index;
using cfd::Real;
using cfd::Vector2;
using cfd::mesh::Cell;
using cfd::mesh::Face;
using cfd::mesh::Mesh;
using cfd::mesh::MeshGeometry;
using cfd::mesh::MeshQuality;
using cfd::mesh::MeshQualityIssue;
using cfd::mesh::MeshQualityReport;
using cfd::mesh::MeshQualitySeverity;
using cfd::mesh::MeshQualityStatus;

constexpr Real kPi = 3.14159265358979323846;

// (nx + 1) x (ny + 1) vertices of the unit-spaced grid mapped by `map`.
template <typename Map>
std::vector<Vector2> vertexGrid(Index nx, Index ny, Real lx, Real ly, Map map) {
  std::vector<Vector2> vertices;
  for (Index j = 0; j <= ny; ++j) {
    for (Index i = 0; i <= nx; ++i) {
      vertices.push_back(map(Vector2{lx * static_cast<Real>(i) / static_cast<Real>(nx),
                                     ly * static_cast<Real>(j) / static_cast<Real>(ny)}));
    }
  }
  return vertices;
}

// The editable parts of a mesh, to build invalid meshes from a valid one.
struct Parts {
  std::vector<Cell> cells;
  std::vector<Face> faces;
  std::vector<cfd::mesh::BoundaryPatch> patches;

  explicit Parts(const Mesh& mesh)
      : cells(mesh.cells()), faces(mesh.faces()), patches(mesh.boundaryPatches()) {}

  void setFace(Index id, Index owner, std::optional<Index> neighbor, Vector2 centroid,
               Vector2 areaVector) {
    faces[id] = Face(id, owner, neighbor, centroid, areaVector);
  }
  [[nodiscard]] Mesh build() const { return Mesh(cells, faces, patches); }
};

Index countIssues(const MeshQualityReport& r, MeshQualitySeverity severity,
                  const std::string& metric) {
  return static_cast<Index>(std::count_if(
      r.issues.begin(), r.issues.end(),
      [&](const MeshQualityIssue& i) { return i.severity == severity && i.metric == metric; }));
}

const MeshQualityIssue* findIssue(const MeshQualityReport& r, MeshQualitySeverity severity,
                                  const std::string& metric) {
  for (const auto& issue : r.issues) {
    if (issue.severity == severity && issue.metric == metric) return &issue;
  }
  return nullptr;
}

Index firstInternalFace(const Mesh& mesh) {
  for (const Face& face : mesh.faces()) {
    if (!face.isBoundary()) return face.id();
  }
  return 0;
}

Index firstBoundaryFace(const Mesh& mesh) {
  for (const Face& face : mesh.faces()) {
    if (face.isBoundary()) return face.id();
  }
  return 0;
}

// --- Metric definitions ------------------------------------------------------

TEST(MeshQualityReport, UniformSquareMeshHasTheIdealValueOfEveryMetric) {
  const Mesh mesh = MeshGeometry::createCartesian2D(4, 4, 1.0, 1.0);
  const MeshQualityReport r = MeshQuality::evaluate(mesh);
  EXPECT_EQ(r.status, MeshQualityStatus::Valid);
  EXPECT_TRUE(r.valid);
  EXPECT_TRUE(r.issues.empty());
  EXPECT_EQ(r.cellCount, 16u);
  EXPECT_EQ(r.faceCount, 40u);
  EXPECT_EQ(r.internalFaceCount, 24u);
  EXPECT_EQ(r.boundaryFaceCount, 16u);
  EXPECT_EQ(r.cellArea.count, 16u);
  EXPECT_DOUBLE_EQ(r.cellArea.minimum, 1.0 / 16.0);
  EXPECT_DOUBLE_EQ(r.cellArea.maximum, 1.0 / 16.0);
  EXPECT_DOUBLE_EQ(r.faceLength.minimum, 0.25);
  EXPECT_DOUBLE_EQ(r.faceLength.maximum, 0.25);
  EXPECT_DOUBLE_EQ(r.aspectRatio.maximum, 1.0);
  EXPECT_DOUBLE_EQ(r.aspectRatio.rms, 1.0);
  EXPECT_EQ(r.nonOrthogonality.count, 24u);
  EXPECT_NEAR(r.nonOrthogonality.maximum, 0.0, 1e-12);
  EXPECT_NEAR(r.skewness.maximum, 0.0, 1e-12);
  EXPECT_EQ(r.expansionRatio.count, 24u);
  EXPECT_DOUBLE_EQ(r.expansionRatio.maximum, 1.0);
  EXPECT_EQ(r.degenerateCells, 0u);
  EXPECT_EQ(r.invalidFaces, 0u);
  EXPECT_EQ(r.connectedComponents, 1u);
  // Every aggregated value agrees with the pre-existing summary fields.
  EXPECT_EQ(r.maximumAspectRatio, r.aspectRatio.maximum);
  EXPECT_EQ(r.maxNonOrthogonalityDegrees, r.nonOrthogonality.maximum);
  EXPECT_EQ(r.maxSkewness, r.skewness.maximum);
  EXPECT_NE(r.summaryLine().find("valid: 16 cells"), std::string::npos) << r.summaryLine();
}

TEST(MeshQualityReport, AspectRatioOfARectangleIsItsSideRatio) {
  // 2 x 8 cells on the unit square: 0.5 x 0.125 -> 4.
  const MeshQualityReport r =
      MeshQuality::evaluate(MeshGeometry::createCartesian2D(2, 8, 1.0, 1.0));
  EXPECT_NEAR(r.aspectRatio.minimum, 4.0, 1e-12);
  EXPECT_NEAR(r.aspectRatio.maximum, 4.0, 1e-12);
  EXPECT_EQ(r.aspectRatio.worstId, 0u);  // ties -> the lowest id
}

// Regression for the pre-MESH-004 defect (results/p12-mesh-004/logs/01): the
// old aspect ratio sorted faces by axis alignment, so a valid grid rotated by
// exactly 45 degrees was declared INVALID. A rigid rotation changes no
// quality metric.
TEST(MeshQualityReport, RotatingAValidMeshChangesNoMetric) {
  for (const Real degrees : {0.0, 10.0, 30.0, 44.0, 45.0, 60.0, 90.0}) {
    const Real c = std::cos(degrees * kPi / 180.0);
    const Real s = std::sin(degrees * kPi / 180.0);
    const Mesh mesh = MeshGeometry::createStructuredQuad2D(
        4, 4, vertexGrid(4, 4, 3.0, 1.0, [&](Vector2 p) {
          return Vector2{(c * p.x) - (s * p.y), (s * p.x) + (c * p.y)};
        }));
    const MeshQualityReport r = MeshQuality::evaluate(mesh);
    EXPECT_EQ(r.status, MeshQualityStatus::Valid) << degrees << " deg: " << r.summaryLine();
    EXPECT_NEAR(r.aspectRatio.maximum, 3.0, 1e-12) << degrees;  // 0.75 x 0.25 cells
    EXPECT_NEAR(r.nonOrthogonality.maximum, 0.0, 1e-5) << degrees;
    EXPECT_NEAR(r.skewness.maximum, 0.0, 1e-9) << degrees;
    EXPECT_NEAR(r.expansionRatio.maximum, 1.0, 1e-12) << degrees;
  }
}

TEST(MeshQualityReport, ExpansionRatioOfAGeometricGradingIsItsRatio) {
  const cfd::mesh::AxisGrading uniform;
  const cfd::mesh::AxisGrading graded{cfd::mesh::GradingType::Geometric, 1.5,
                                      cfd::mesh::GradingCluster::Start};
  const MeshQualityReport r =
      MeshQuality::evaluate(MeshGeometry::createGraded2D(4, 6, 1.0, 1.0, uniform, graded));
  EXPECT_NEAR(r.expansionRatio.minimum, 1.0, 1e-12);  // faces between columns
  EXPECT_NEAR(r.expansionRatio.maximum, 1.5, 1e-12);  // faces between rows
  EXPECT_EQ(r.expansionRatio.aboveWarning, 0u);
  EXPECT_EQ(r.status, MeshQualityStatus::Valid);
}

// A uniformly sheared grid (x = X + s Y): every internal face is exactly
// atan(s) non-orthogonal and unskewed (parallelogram cells).
TEST(MeshQualityReport, ShearedMeshHasTheShearAngleOnEveryFace) {
  for (const Real angle : {26.565051177077994, 45.0, 60.0}) {
    const Real shear = std::tan(angle * kPi / 180.0);
    const Mesh mesh = MeshGeometry::createStructuredQuad2D(
        5, 5,
        vertexGrid(5, 5, 1.0, 1.0, [&](Vector2 p) { return Vector2{p.x + (shear * p.y), p.y}; }));
    const MeshQualityReport r = MeshQuality::evaluate(mesh);
    EXPECT_NEAR(r.nonOrthogonality.minimum, angle, 1e-9) << angle;
    EXPECT_NEAR(r.nonOrthogonality.maximum, angle, 1e-9) << angle;
    EXPECT_NEAR(r.skewness.maximum, 0.0, 1e-9) << angle;
    EXPECT_EQ(r.status, MeshQualityStatus::Valid) << angle;
  }
}

// Worst-entity identification: the report's maxima and worst ids are those
// of MeshGeometry's own per-face definitions (no second definition here --
// the per-face values are read from MeshGeometry, then compared).
TEST(MeshQualityReport, WorstEntityIsTheArgmaxOfTheMeshGeometryDefinition) {
  const Mesh mesh = MeshGeometry::createStructuredQuad2D(
      6, 6, vertexGrid(6, 6, 1.0, 1.0, [](Vector2 p) {
        const bool interior = p.x > 1e-12 && p.x < 1.0 - 1e-12 && p.y > 1e-12 && p.y < 1.0 - 1e-12;
        return interior ? Vector2{p.x + (0.04 * std::sin(7.0 * p.y + 3.0 * p.x)),
                                  p.y + (0.03 * std::cos(5.0 * p.x))}
                        : p;
      }));
  const MeshQualityReport r = MeshQuality::evaluate(mesh);
  Real maxAngle = -1.0;
  Real maxSkew = -1.0;
  Index angleFace = 0;
  Index skewFace = 0;
  for (const Face& face : mesh.faces()) {
    if (face.isBoundary()) continue;
    const Real angle = MeshGeometry::nonOrthogonalityAngleDegrees(mesh, face);
    if (angle > maxAngle) {
      maxAngle = angle;
      angleFace = face.id();
    }
    const Real skew = MeshGeometry::skewness(mesh, face).value();
    if (skew > maxSkew) {
      maxSkew = skew;
      skewFace = face.id();
    }
  }
  EXPECT_GT(maxAngle, 1.0);
  EXPECT_EQ(r.nonOrthogonality.maximum, maxAngle);
  EXPECT_EQ(r.nonOrthogonality.worstId, angleFace);
  EXPECT_EQ(r.nonOrthogonality.worstLocation.x, mesh.face(angleFace).centroid().x);
  EXPECT_EQ(r.nonOrthogonality.worstLocation.y, mesh.face(angleFace).centroid().y);
  EXPECT_EQ(r.skewness.maximum, maxSkew);
  EXPECT_EQ(r.skewness.worstId, skewFace);
  // The smallest cell is the cell-area worst entity.
  Index smallest = 0;
  for (const Cell& cell : mesh.cells()) {
    if (cell.volume() < mesh.cell(smallest).volume()) smallest = cell.id();
  }
  EXPECT_EQ(r.cellArea.worstId, smallest);
  EXPECT_EQ(r.cellArea.minimum, mesh.cell(smallest).volume());
}

// Two conformal blocks of different resolution: the size jump across the
// interface is the mesh's largest expansion ratio (2, AT the warning
// threshold -- a warning needs a value above it).
TEST(MeshQualityReport, MultiBlockInterfaceExpansionIsMeasured) {
  cfd::mesh::MultiBlockSpec spec;
  spec.blocks.push_back({"fine", 4, 4, vertexGrid(4, 4, 0.5, 1.0, [](Vector2 p) { return p; })});
  spec.blocks.push_back({"coarse", 2, 4, vertexGrid(2, 4, 0.5, 1.0, [](Vector2 p) {
                           return Vector2{p.x + 0.5, p.y};
                         })});
  using cfd::mesh::BlockSide;
  spec.interfaces.push_back({{0, BlockSide::Right}, {1, BlockSide::Left}, false});
  spec.patches.push_back({"walls",
                          {{0, BlockSide::Left},
                           {0, BlockSide::Bottom},
                           {0, BlockSide::Top},
                           {1, BlockSide::Right},
                           {1, BlockSide::Bottom},
                           {1, BlockSide::Top}}});
  const Mesh mesh = MeshGeometry::createMultiBlock2D(spec);
  const MeshQualityReport r = MeshQuality::evaluate(mesh);
  EXPECT_EQ(r.status, MeshQualityStatus::Valid) << r.summaryLine();
  EXPECT_NEAR(r.expansionRatio.maximum, 2.0, 1e-12);
  EXPECT_EQ(r.expansionRatio.aboveWarning, 0u);
  EXPECT_NEAR(r.nonOrthogonality.maximum, 0.0, 1e-9);
  EXPECT_NEAR(r.aspectRatio.maximum, 2.0, 1e-12);  // fine cells 0.125 x 0.25
  EXPECT_NEAR(r.aspectRatio.minimum, 1.0, 1e-12);  // coarse cells 0.25 x 0.25
  // The worst expansion face is an interface face: owner in "fine" (cells
  // 0..15), neighbour in "coarse" (cells 16..23).
  const Face& worst = mesh.face(r.expansionRatio.worstId);
  ASSERT_TRUE(worst.neighbor().has_value());
  EXPECT_LT(worst.owner(), 16u);
  EXPECT_GE(*worst.neighbor(), 16u);
  EXPECT_NEAR(worst.centroid().x, 0.5, 1e-12);
}

// --- Classification ------------------------------------------------------------

TEST(MeshQualityReport, WarningsAreAggregatedPerMetricWithTheWorstEntity) {
  cfd::mesh::MeshQualityThresholds thresholds;
  thresholds.aspectRatioWarning = 3.0;
  const Mesh mesh = MeshGeometry::createCartesian2D(2, 8, 1.0, 1.0);  // AR 4 everywhere
  const MeshQualityReport r = MeshQuality::evaluate(mesh, thresholds);
  EXPECT_EQ(r.status, MeshQualityStatus::ValidWithWarnings);
  EXPECT_TRUE(r.valid);
  EXPECT_TRUE(r.problems.empty());
  EXPECT_EQ(countIssues(r, MeshQualitySeverity::Warning, "aspect_ratio"), 1u);  // one entry
  const MeshQualityIssue* w = findIssue(r, MeshQualitySeverity::Warning, "aspect_ratio");
  ASSERT_NE(w, nullptr);
  EXPECT_EQ(w->entity, "cell");
  EXPECT_EQ(w->count, 16u);
  EXPECT_EQ(r.aspectRatio.aboveWarning, 16u);
  ASSERT_TRUE(w->id.has_value());
  EXPECT_EQ(*w->id, 0u);
  ASSERT_TRUE(w->value.has_value());
  EXPECT_NEAR(*w->value, 4.0, 1e-12);
  ASSERT_TRUE(w->threshold.has_value());
  EXPECT_EQ(*w->threshold, 3.0);
  const std::string line = cfd::mesh::formatMeshQualityIssue(*w);
  EXPECT_EQ(
      line.rfind("warning aspect_ratio: aspect_ratio 4 > 3 at cell 0 (0.25, 0.0625) (16 cells "
                 "above the warning threshold)",
                 0),
      0u)
      << line;
  EXPECT_NE(line.find("[value 4; threshold 3; at (0.25, 0.0625)]"), std::string::npos) << line;
  EXPECT_NE(r.summaryLine().find("valid_with_warnings: 16 cells"), std::string::npos);
  EXPECT_NE(r.summaryLine().find("1 warning(s)"), std::string::npos);
  // The same mesh under the default thresholds (aspect ratio 100) is valid.
  EXPECT_EQ(MeshQuality::evaluate(mesh).status, MeshQualityStatus::Valid);
}

TEST(MeshQualityReport, NonOrthogonalityBeyondSeventyDegreesIsAWarningNotAnError) {
  const Real shear = std::tan(75.0 * kPi / 180.0);
  const Mesh mesh = MeshGeometry::createStructuredQuad2D(
      4, 4,
      vertexGrid(4, 4, 1.0, 1.0, [&](Vector2 p) { return Vector2{p.x + (shear * p.y), p.y}; }));
  const MeshQualityReport r = MeshQuality::evaluate(mesh);
  EXPECT_EQ(r.status, MeshQualityStatus::ValidWithWarnings);
  const MeshQualityIssue* w = findIssue(r, MeshQualitySeverity::Warning, "non_orthogonality");
  ASSERT_NE(w, nullptr);
  EXPECT_EQ(w->count, r.internalFaceCount);
  EXPECT_NEAR(*w->value, 75.0, 1e-9);
  EXPECT_EQ(*w->threshold, 70.0);
}

TEST(MeshQualityReport, SkewnessAndExpansionWarningsUseTheirThresholds) {
  cfd::mesh::MeshQualityThresholds thresholds;
  thresholds.expansionRatioWarning = 1.4;
  const cfd::mesh::AxisGrading uniform;
  const cfd::mesh::AxisGrading graded{cfd::mesh::GradingType::Geometric, 1.5,
                                      cfd::mesh::GradingCluster::Start};
  const Mesh mesh = MeshGeometry::createGraded2D(3, 4, 1.0, 1.0, uniform, graded);
  const MeshQualityReport r = MeshQuality::evaluate(mesh, thresholds);
  EXPECT_EQ(r.status, MeshQualityStatus::ValidWithWarnings);
  const MeshQualityIssue* w = findIssue(r, MeshQualitySeverity::Warning, "expansion_ratio");
  ASSERT_NE(w, nullptr);
  EXPECT_EQ(w->count, 9u);  // 3 columns x 3 row-to-row faces
  EXPECT_EQ(countIssues(r, MeshQualitySeverity::Warning, "skewness"), 0u);

  thresholds = {};
  thresholds.skewnessWarning = 0.01;
  const Mesh skewed = MeshGeometry::createStructuredQuad2D(
      4, 4, vertexGrid(4, 4, 1.0, 1.0, [](Vector2 p) {
        return (std::abs(p.x - 0.5) < 1e-12 && std::abs(p.y - 0.5) < 1e-12) ? Vector2{0.6, 0.55}
                                                                            : p;
      }));
  const MeshQualityReport s = MeshQuality::evaluate(skewed, thresholds);
  EXPECT_GT(s.skewness.maximum, 0.01);
  EXPECT_EQ(countIssues(s, MeshQualitySeverity::Warning, "skewness"), 1u);
  EXPECT_EQ(s.status, MeshQualityStatus::ValidWithWarnings);
}

TEST(MeshQualityReport, NonOrthogonalMeshGetsAnInformationalCorrectionAdvice) {
  const Mesh sheared = MeshGeometry::createStructuredQuad2D(
      4, 4, vertexGrid(4, 4, 1.0, 1.0, [](Vector2 p) { return Vector2{p.x + (0.5 * p.y), p.y}; }));
  const MeshQualityReport r = MeshQuality::evaluate(sheared);
  EXPECT_EQ(r.status, MeshQualityStatus::Valid);  // information never changes the status
  const MeshQualityIssue* info = findIssue(r, MeshQualitySeverity::Info, "non_orthogonality");
  ASSERT_NE(info, nullptr);
  EXPECT_NE(info->message.find("non_orthogonal_corrections >= 1"), std::string::npos);
  EXPECT_EQ(findIssue(MeshQuality::evaluate(MeshGeometry::createCartesian2D(4, 4, 1.0, 1.0)),
                      MeshQualitySeverity::Info, "non_orthogonality"),
            nullptr);
}

// --- Fatal conditions (hand-built meshes) ---------------------------------------

void expectFatal(const Mesh& mesh, const std::string& metric, const std::string& text) {
  const MeshQualityReport r = MeshQuality::evaluate(mesh);
  EXPECT_EQ(r.status, MeshQualityStatus::Invalid) << metric;
  EXPECT_FALSE(r.valid) << metric;
  bool found = false;
  for (const auto& issue : r.issues) {
    found = found || (issue.severity == MeshQualitySeverity::Fatal && issue.metric == metric &&
                      issue.message.find(text) != std::string::npos);
  }
  EXPECT_TRUE(found) << metric << " / " << text << ": " << r.summaryLine();
  EXPECT_FALSE(r.problems.empty());
  EXPECT_EQ(r.summaryLine().rfind("invalid: ", 0), 0u) << r.summaryLine();
}

// The first line of defence: a Cell / Face cannot even be constructed with a
// non-positive or non-finite area, a non-finite centroid, a non-positive or
// non-finite face length or owner == neighbour (Cell.cpp, Face.cpp), so
// MeshQuality's matching fatal checks (cell_area, cell_centroid,
// face_length, owner == neighbour) are a second, defensive line that no
// constructible Mesh reaches. Both lines are pinned here.
TEST(MeshQualityReport, DegenerateCellsAndFacesCannotBeConstructed) {
  const Real nan = std::numeric_limits<Real>::quiet_NaN();
  const Real inf = std::numeric_limits<Real>::infinity();
  EXPECT_THROW(Cell(0, Vector2{0.5, 0.5}, 0.0), cfd::InvalidArgumentError);
  EXPECT_THROW(Cell(0, Vector2{0.5, 0.5}, -0.1), cfd::InvalidArgumentError);
  EXPECT_THROW(Cell(0, Vector2{0.5, 0.5}, nan), cfd::InvalidArgumentError);
  EXPECT_THROW(Cell(0, Vector2{0.5, 0.5}, inf), cfd::InvalidArgumentError);
  EXPECT_THROW(Cell(0, Vector2{nan, 0.5}, 1.0), cfd::InvalidArgumentError);
  EXPECT_THROW(Face(0, 0, 1, Vector2{0.5, 0.5}, Vector2{0.0, 0.0}), cfd::InvalidArgumentError);
  EXPECT_THROW(Face(0, 0, 1, Vector2{0.5, 0.5}, Vector2{nan, 1.0}), cfd::InvalidArgumentError);
  EXPECT_THROW(Face(0, 0, 1, Vector2{inf, 0.5}, Vector2{1.0, 0.0}), cfd::InvalidArgumentError);
  EXPECT_THROW(Face(0, 1, 1, Vector2{0.5, 0.5}, Vector2{1.0, 0.0}), cfd::InvalidArgumentError);
}

TEST(MeshQualityReport, BrokenConnectivityIsFatal) {
  const Mesh base = MeshGeometry::createCartesian2D(3, 3, 1.0, 1.0);
  const Index f = firstInternalFace(base);
  const Face& face = base.face(f);
  {
    Parts parts(base);  // neighbour id beyond the cell count
    parts.setFace(f, face.owner(), Index{99}, face.centroid(), face.areaVector());
    expectFatal(parts.build(), "connectivity",
                "face " + std::to_string(f) + " has an invalid owner/neighbor cell id");
  }
  {
    Parts parts(base);  // a cell listing a face that does not exist
    Cell broken(0, base.cell(0).centroid(), base.cell(0).volume());
    for (const Index id : base.cell(0).faceIds()) broken.addFace(id);
    broken.addFace(999);
    parts.cells[0] = broken;
    expectFatal(parts.build(), "connectivity", "cell 0 lists a non-existent face 999");
  }
  {
    Parts parts(base);  // fewer than 3 faces
    parts.cells[0] = Cell(0, base.cell(0).centroid(), base.cell(0).volume());
    expectFatal(parts.build(), "connectivity", "cell 0 has fewer than 3 faces");
  }
}

TEST(MeshQualityReport, ReversedFaceOrientationIsFatal) {
  const Mesh base = MeshGeometry::createCartesian2D(3, 3, 1.0, 1.0);
  {
    const Index f = firstInternalFace(base);
    const Face& face = base.face(f);
    Parts parts(base);
    parts.setFace(f, face.owner(), face.neighbor(), face.centroid(), face.areaVector() * -1.0);
    const Mesh mesh = parts.build();
    expectFatal(mesh, "orientation", "area vector does not point from owner to neighbor");
    // The flipped face also opens its two cells.
    EXPECT_EQ(countIssues(MeshQuality::evaluate(mesh), MeshQualitySeverity::Fatal, "closure"), 2u);
  }
  {
    const Index f = firstBoundaryFace(base);
    const Face& face = base.face(f);
    Parts parts(base);
    parts.setFace(f, face.owner(), std::nullopt, face.centroid(), face.areaVector() * -1.0);
    expectFatal(parts.build(), "orientation", "(boundary) area vector does not point out");
  }
}

TEST(MeshQualityReport, BoundaryFaceMustBeInExactlyOnePatch) {
  const Mesh base = MeshGeometry::createCartesian2D(3, 3, 1.0, 1.0);
  const Index f = firstBoundaryFace(base);
  {
    Parts parts(base);  // removed from its patch
    for (auto& patch : parts.patches) {
      std::vector<Index> ids;
      for (const Index id : patch.faceIds()) {
        if (id != f) ids.push_back(id);
      }
      patch = cfd::mesh::BoundaryPatch(patch.name(), ids);
    }
    expectFatal(
        parts.build(), "patch",
        "face " + std::to_string(f) + " (boundary) is in 0 boundary patches, expected exactly 1");
  }
  {
    Parts parts(base);  // listed by a second patch
    parts.patches.emplace_back("extra", std::vector<Index>{f});
    expectFatal(parts.build(), "patch", "is in 2 boundary patches");
  }
}

TEST(MeshQualityReport, DisconnectedMeshIsFatal) {
  // Two 2 x 2 blocks side by side with no interface: two regions.
  cfd::mesh::MultiBlockSpec spec;
  spec.blocks.push_back({"a", 2, 2, vertexGrid(2, 2, 1.0, 1.0, [](Vector2 p) { return p; })});
  spec.blocks.push_back(
      {"b", 2, 2, vertexGrid(2, 2, 1.0, 1.0, [](Vector2 p) { return Vector2{p.x + 2.0, p.y}; })});
  using cfd::mesh::BlockSide;
  cfd::mesh::BoundaryPatchSpec walls{"walls", {}};
  for (Index b = 0; b < 2; ++b) {
    for (const BlockSide side :
         {BlockSide::Left, BlockSide::Right, BlockSide::Bottom, BlockSide::Top}) {
      walls.sides.push_back({b, side});
    }
  }
  spec.patches.push_back(walls);
  const Mesh mesh = MeshGeometry::createMultiBlock2D(spec);
  expectFatal(mesh, "connected_components",
              "mesh has 2 disconnected cell regions (cells per region: 4, 4)");
  // The report must outlive `issue`, which points into its issue list.
  const MeshQualityReport report = MeshQuality::evaluate(mesh);
  const MeshQualityIssue* issue =
      findIssue(report, MeshQualitySeverity::Fatal, "connected_components");
  ASSERT_NE(issue, nullptr);
  EXPECT_EQ(issue->entity, "mesh");
  EXPECT_EQ(issue->value.value(), 2.0);
}

TEST(MeshQualityReport, FatalIssuesAreCappedWithACountOfTheRest) {
  // Every one of 36 cells lists a non-existent face: 36 fatal problems.
  const Mesh base = MeshGeometry::createCartesian2D(6, 6, 1.0, 1.0);
  Parts parts(base);
  for (Index c = 0; c < 36; ++c) {
    Cell broken(c, base.cell(c).centroid(), base.cell(c).volume());
    broken.addFace(999);
    parts.cells[c] = broken;
  }
  const MeshQualityReport r = MeshQuality::evaluate(parts.build());
  EXPECT_EQ(r.status, MeshQualityStatus::Invalid);
  EXPECT_EQ(r.degenerateCells, 36u);
  EXPECT_EQ(countIssues(r, MeshQualitySeverity::Fatal, "connectivity"), cfd::mesh::kMaxFatalIssues);
  const MeshQualityIssue* more = findIssue(r, MeshQualitySeverity::Fatal, "more");
  ASSERT_NE(more, nullptr);
  EXPECT_EQ(more->count, 16u);
  EXPECT_EQ(more->message, "16 more fatal problems not listed");
  EXPECT_EQ(r.problems.size(), cfd::mesh::kMaxFatalIssues + 1);
}

TEST(MeshQualityReport, IdenticalMeshesGiveIdenticalReports) {
  const auto make = [] {
    return MeshGeometry::createStructuredQuad2D(
        5, 5, vertexGrid(5, 5, 1.0, 1.0, [](Vector2 p) {
          return Vector2{p.x + (0.03 * std::sin(kPi * p.x) * std::sin(2.0 * kPi * p.y)), p.y};
        }));
  };
  const MeshQualityReport a = MeshQuality::evaluate(make());
  const MeshQualityReport b = MeshQuality::evaluate(make());
  EXPECT_EQ(a.summaryLine(), b.summaryLine());
  EXPECT_EQ(a.nonOrthogonality.maximum, b.nonOrthogonality.maximum);
  EXPECT_EQ(a.nonOrthogonality.rms, b.nonOrthogonality.rms);
  EXPECT_EQ(a.skewness.worstId, b.skewness.worstId);
  ASSERT_EQ(a.issues.size(), b.issues.size());
  for (std::size_t k = 0; k < a.issues.size(); ++k) {
    EXPECT_EQ(cfd::mesh::formatMeshQualityIssue(a.issues[k]),
              cfd::mesh::formatMeshQualityIssue(b.issues[k]));
  }
}

}  // namespace
