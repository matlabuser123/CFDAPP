// P12-MESH-001: MeshGeometry::createStructuredQuad2D (the production
// non-orthogonal structured quad mesh), the vertex grid every production
// mesh carries (Mesh::structuredGrid), and MeshQuality's validity checks.
// Verification tests: geometry checked against hand-derived exact values
// and against createCartesian2D's own geometry, not re-derived formulas.
#include <gtest/gtest.h>

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
#include "cfd/mesh/MeshFingerprint.hpp"
#include "cfd/mesh/MeshGeometry.hpp"
#include "cfd/mesh/MeshQuality.hpp"

using cfd::Index;
using cfd::InvalidArgumentError;
using cfd::Real;
using cfd::Vector2;
using cfd::mesh::Mesh;
using cfd::mesh::MeshGeometry;
using cfd::mesh::MeshQuality;

namespace {

std::vector<Vector2> latticeVertices(Index nx, Index ny, Real lengthX, Real lengthY) {
  std::vector<Vector2> vertices;
  const Real dx = lengthX / static_cast<Real>(nx);
  const Real dy = lengthY / static_cast<Real>(ny);
  for (Index j = 0; j <= ny; ++j) {
    for (Index i = 0; i <= nx; ++i) {
      vertices.push_back(Vector2{static_cast<Real>(i) * dx, static_cast<Real>(j) * dy});
    }
  }
  return vertices;
}

// A smooth distortion of the unit-square lattice: interior vertices moved,
// boundary vertices kept on the square (max face non-orthogonality ~30
// degrees at 8x8).
std::vector<Vector2> distortedVertices(Index nx, Index ny) {
  std::vector<Vector2> vertices = latticeVertices(nx, ny, 1.0, 1.0);
  const Real pi = std::acos(-1.0);
  for (Index j = 1; j < ny; ++j) {
    for (Index i = 1; i < nx; ++i) {
      Vector2& v = vertices[(j * (nx + 1)) + i];
      const Vector2 base = v;
      v.x = base.x + (0.06 * std::sin(pi * base.x) * std::sin(2.0 * pi * base.y));
      v.y = base.y + (0.05 * std::sin(2.0 * pi * base.x) * std::sin(pi * base.y));
    }
  }
  return vertices;
}

Vector2& vertexAt(std::vector<Vector2>& vertices, Index nx, Index i, Index j) {
  return vertices[(j * (nx + 1)) + i];
}

void expectNear(const Vector2& a, const Vector2& b, Real tolerance) {
  EXPECT_NEAR(a.x, b.x, tolerance);
  EXPECT_NEAR(a.y, b.y, tolerance);
}

}  // namespace

// --- Geometry ---------------------------------------------------------------

// Vertices on a uniform rectangular lattice reproduce createCartesian2D:
// same topology exactly, geometry to round-off.
TEST(StructuredQuadMesh, LatticeVerticesReproduceCartesianMesh) {
  const Index nx = 5;
  const Index ny = 3;
  const Mesh cartesian = MeshGeometry::createCartesian2D(nx, ny, 2.0, 0.75);
  const Mesh quad =
      MeshGeometry::createStructuredQuad2D(nx, ny, latticeVertices(nx, ny, 2.0, 0.75));

  ASSERT_EQ(quad.numberOfCells(), cartesian.numberOfCells());
  ASSERT_EQ(quad.numberOfFaces(), cartesian.numberOfFaces());
  for (Index c = 0; c < quad.numberOfCells(); ++c) {
    expectNear(quad.cell(c).centroid(), cartesian.cell(c).centroid(), 1e-14);
    EXPECT_NEAR(quad.cell(c).volume(), cartesian.cell(c).volume(), 1e-14);
    EXPECT_EQ(quad.cell(c).faceIds(), cartesian.cell(c).faceIds());
  }
  for (Index f = 0; f < quad.numberOfFaces(); ++f) {
    const auto& a = quad.face(f);
    const auto& b = cartesian.face(f);
    EXPECT_EQ(a.owner(), b.owner());
    EXPECT_EQ(a.neighbor(), b.neighbor());
    expectNear(a.centroid(), b.centroid(), 1e-14);
    expectNear(a.areaVector(), b.areaVector(), 1e-14);
  }
  ASSERT_EQ(quad.boundaryPatches().size(), 4u);
  for (const char* name : {"left", "right", "bottom", "top"}) {
    EXPECT_EQ(quad.boundaryPatch(name).faceIds(), cartesian.boundaryPatch(name).faceIds()) << name;
  }
}

// One convex trapezoid cell, geometry derived by hand: vertices (0,0),
// (2,0), (1.5,1), (0.5,1). Area (2 + 1) / 2 = 1.5; centroid x = 1 by
// symmetry, y = h (b1 + 2 b2) / (3 (b1 + b2)) = (2 + 2) / 9 = 4/9. Face
// area vectors: bottom (0,-2), top (0,1), left (-1,0.5), right (1,0.5).
TEST(StructuredQuadMesh, TrapezoidGeometryIsExact) {
  const std::vector<Vector2> vertices = {{0.0, 0.0}, {2.0, 0.0}, {0.5, 1.0}, {1.5, 1.0}};
  const Mesh mesh = MeshGeometry::createStructuredQuad2D(1, 1, vertices);
  ASSERT_EQ(mesh.numberOfCells(), 1u);
  EXPECT_DOUBLE_EQ(mesh.cell(0).volume(), 1.5);
  EXPECT_NEAR(mesh.cell(0).centroid().x, 1.0, 1e-15);
  EXPECT_NEAR(mesh.cell(0).centroid().y, 4.0 / 9.0, 1e-15);

  const auto faceOf = [&](const char* patch) {
    return mesh.face(mesh.boundaryPatch(patch).faceIds().at(0));
  };
  expectNear(faceOf("bottom").areaVector(), {0.0, -2.0}, 1e-15);
  expectNear(faceOf("top").areaVector(), {0.0, 1.0}, 1e-15);
  expectNear(faceOf("left").areaVector(), {-1.0, 0.5}, 1e-15);
  expectNear(faceOf("right").areaVector(), {1.0, 0.5}, 1e-15);
  expectNear(faceOf("left").centroid(), {0.25, 0.5}, 1e-15);
  expectNear(faceOf("right").centroid(), {1.75, 0.5}, 1e-15);
}

// A distorted mesh: every cell closed and outward-oriented, cells tile the
// domain exactly, the boundary is closed, and the mesh is genuinely
// non-orthogonal and skewed (MeshQuality).
TEST(StructuredQuadMesh, DistortedMeshIsConservativeAndGenuinelyNonOrthogonal) {
  const Index n = 8;
  const Mesh mesh = MeshGeometry::createStructuredQuad2D(n, n, distortedVertices(n, n));

  Real area = 0.0;
  for (const auto& cell : mesh.cells()) area += cell.volume();
  EXPECT_NEAR(area, 1.0, 1e-14);

  Vector2 boundarySum{0.0, 0.0};
  for (const auto& face : mesh.faces()) {
    if (face.isBoundary()) boundarySum += face.areaVector();
  }
  EXPECT_NEAR(boundarySum.x, 0.0, 1e-14);
  EXPECT_NEAR(boundarySum.y, 0.0, 1e-14);

  const auto report = MeshQuality::evaluate(mesh);
  EXPECT_TRUE(report.valid);
  EXPECT_TRUE(report.problems.empty());
  EXPECT_GT(report.maxNonOrthogonalityDegrees, 20.0);
  EXPECT_GT(report.maxSkewness, 0.01);
}

TEST(StructuredQuadMesh, CarriesItsVertexGrid) {
  const std::vector<Vector2> vertices = distortedVertices(4, 3);
  const Mesh mesh = MeshGeometry::createStructuredQuad2D(4, 3, vertices);
  const auto* grid = mesh.structuredGrid();
  ASSERT_NE(grid, nullptr);
  EXPECT_EQ(grid->nx, 4u);
  EXPECT_EQ(grid->ny, 3u);
  ASSERT_EQ(grid->vertices.size(), vertices.size());
  for (std::size_t k = 0; k < vertices.size(); ++k) {
    EXPECT_EQ(grid->vertices[k].x, vertices[k].x);
    EXPECT_EQ(grid->vertices[k].y, vertices[k].y);
  }
  EXPECT_EQ(grid->vertex(2, 1).x, vertices[(1 * 5) + 2].x);
}

// createCartesian2D now carries its vertex grid (for exporters) with
// vertex(i, j) = (i * dx, j * dy), dx = L / nx -- exactly the coordinates
// the pre-existing VTK export reconstructed.
TEST(StructuredQuadMesh, CartesianMeshCarriesItsLatticeGrid) {
  const Mesh mesh = MeshGeometry::createCartesian2D(3, 2, 1.5, 0.7);
  const auto* grid = mesh.structuredGrid();
  ASSERT_NE(grid, nullptr);
  EXPECT_EQ(grid->nx, 3u);
  EXPECT_EQ(grid->ny, 2u);
  const Real dx = 1.5 / 3.0;
  const Real dy = 0.7 / 2.0;
  for (Index j = 0; j <= 2; ++j) {
    for (Index i = 0; i <= 3; ++i) {
      EXPECT_EQ(grid->vertex(i, j).x, static_cast<Real>(i) * dx);
      EXPECT_EQ(grid->vertex(i, j).y, static_cast<Real>(j) * dy);
    }
  }
}

TEST(StructuredQuadMesh, MeshWithoutGridReportsNone) {
  const Mesh source = MeshGeometry::createCartesian2D(2, 2, 1.0, 1.0);
  const Mesh copy(source.cells(), source.faces(), source.boundaryPatches());
  EXPECT_EQ(copy.structuredGrid(), nullptr);
}

TEST(StructuredQuadMesh, MeshRejectsGridThatDoesNotMatchItsCells) {
  const Mesh source = MeshGeometry::createCartesian2D(2, 2, 1.0, 1.0);
  EXPECT_THROW((void)Mesh(source.cells(), source.faces(), source.boundaryPatches(),
                          cfd::mesh::StructuredGrid{3, 2, latticeVertices(3, 2, 1.0, 1.0), {}}),
               InvalidArgumentError);
  EXPECT_THROW((void)Mesh(source.cells(), source.faces(), source.boundaryPatches(),
                          cfd::mesh::StructuredGrid{2, 2, latticeVertices(2, 1, 1.0, 1.0), {}}),
               InvalidArgumentError);
}

// --- Determinism ---------------------------------------------------------------

TEST(StructuredQuadMesh, ConstructionIsDeterministic) {
  const std::vector<Vector2> vertices = distortedVertices(6, 5);
  const Mesh a = MeshGeometry::createStructuredQuad2D(6, 5, vertices);
  const Mesh b = MeshGeometry::createStructuredQuad2D(6, 5, vertices);
  EXPECT_EQ(cfd::mesh::computeMeshFingerprint(a), cfd::mesh::computeMeshFingerprint(b));
  for (Index c = 0; c < a.numberOfCells(); ++c) {
    EXPECT_EQ(a.cell(c).centroid().x, b.cell(c).centroid().x);
    EXPECT_EQ(a.cell(c).centroid().y, b.cell(c).centroid().y);
    EXPECT_EQ(a.cell(c).volume(), b.cell(c).volume());
  }
  std::vector<Vector2> moved = vertices;
  vertexAt(moved, 6, 3, 2).x += 1e-3;
  EXPECT_NE(cfd::mesh::computeMeshFingerprint(a),
            cfd::mesh::computeMeshFingerprint(MeshGeometry::createStructuredQuad2D(6, 5, moved)));
}

// --- Rejection of invalid vertex grids -------------------------------------------

TEST(StructuredQuadMesh, RejectsZeroCells) {
  EXPECT_THROW((void)MeshGeometry::createStructuredQuad2D(0, 2, latticeVertices(1, 2, 1.0, 1.0)),
               InvalidArgumentError);
  EXPECT_THROW((void)MeshGeometry::createStructuredQuad2D(2, 0, latticeVertices(2, 1, 1.0, 1.0)),
               InvalidArgumentError);
}

TEST(StructuredQuadMesh, RejectsWrongVertexCount) {
  std::vector<Vector2> vertices = latticeVertices(3, 3, 1.0, 1.0);
  vertices.pop_back();
  EXPECT_THROW((void)MeshGeometry::createStructuredQuad2D(3, 3, vertices), InvalidArgumentError);
}

TEST(StructuredQuadMesh, RejectsNonFiniteVertex) {
  for (const Real bad :
       {std::numeric_limits<Real>::quiet_NaN(), std::numeric_limits<Real>::infinity(),
        -std::numeric_limits<Real>::infinity()}) {
    std::vector<Vector2> vertices = latticeVertices(3, 3, 1.0, 1.0);
    vertexAt(vertices, 3, 1, 2).y = bad;
    EXPECT_THROW((void)MeshGeometry::createStructuredQuad2D(3, 3, vertices), InvalidArgumentError);
  }
}

// An interior vertex pushed past its neighbor folds the two cells sharing
// it (one of them turns clockwise / self-intersects).
TEST(StructuredQuadMesh, RejectsFoldedCells) {
  std::vector<Vector2> vertices = latticeVertices(3, 3, 3.0, 3.0);
  vertexAt(vertices, 3, 1, 1).x = 2.5;  // past vertex (2, 1) at x = 2
  EXPECT_THROW((void)MeshGeometry::createStructuredQuad2D(3, 3, vertices), InvalidArgumentError);
}

// Mirroring the lattice (x -> -x) keeps every cell's shape but orders its
// corners clockwise.
TEST(StructuredQuadMesh, RejectsClockwiseCells) {
  std::vector<Vector2> vertices = latticeVertices(2, 2, 1.0, 1.0);
  for (auto& v : vertices) v.x = -v.x;
  EXPECT_THROW((void)MeshGeometry::createStructuredQuad2D(2, 2, vertices), InvalidArgumentError);
}

// A dart (one corner pushed inside the quad): positive area, but a reflex
// corner -- non-convex.
TEST(StructuredQuadMesh, RejectsNonConvexCell) {
  const std::vector<Vector2> vertices = {{0.0, 0.0}, {2.0, 0.0}, {0.0, 2.0}, {0.6, 0.6}};
  EXPECT_THROW((void)MeshGeometry::createStructuredQuad2D(1, 1, vertices), InvalidArgumentError);
}

// A collapsed edge (two coincident corners) and a zero-area (collinear)
// cell.
TEST(StructuredQuadMesh, RejectsDegenerateCells) {
  const std::vector<Vector2> collapsed = {{0.0, 0.0}, {1.0, 0.0}, {0.0, 1.0}, {0.0, 1.0}};
  EXPECT_THROW((void)MeshGeometry::createStructuredQuad2D(1, 1, collapsed), InvalidArgumentError);
  const std::vector<Vector2> flat = {{0.0, 0.0}, {1.0, 0.0}, {0.0, 0.0}, {1.0, 0.0}};
  EXPECT_THROW((void)MeshGeometry::createStructuredQuad2D(1, 1, flat), InvalidArgumentError);
}

// --- MeshQuality validity checks (P12-MESH-001) ----------------------------------

namespace {

bool hasProblemContaining(const cfd::mesh::MeshQualityReport& report, const std::string& text) {
  for (const auto& problem : report.problems) {
    if (problem.find(text) != std::string::npos) return true;
  }
  return false;
}

// Two unit cells side by side (x in [0,2], y in [0,1]), built by hand so
// individual pieces can be broken.
struct TwoCellMesh {
  std::vector<cfd::mesh::Cell> cells;
  std::vector<cfd::mesh::Face> faces;
  std::vector<cfd::mesh::BoundaryPatch> patches;

  TwoCellMesh() {
    cells.emplace_back(0, Vector2{0.5, 0.5}, 1.0);
    cells.emplace_back(1, Vector2{1.5, 0.5}, 1.0);
    faces.emplace_back(0, 0, 1, Vector2{1.0, 0.5}, Vector2{1.0, 0.0});
    faces.emplace_back(1, 0, std::nullopt, Vector2{0.0, 0.5}, Vector2{-1.0, 0.0});
    faces.emplace_back(2, 1, std::nullopt, Vector2{2.0, 0.5}, Vector2{1.0, 0.0});
    faces.emplace_back(3, 0, std::nullopt, Vector2{0.5, 0.0}, Vector2{0.0, -1.0});
    faces.emplace_back(4, 1, std::nullopt, Vector2{1.5, 0.0}, Vector2{0.0, -1.0});
    faces.emplace_back(5, 0, std::nullopt, Vector2{0.5, 1.0}, Vector2{0.0, 1.0});
    faces.emplace_back(6, 1, std::nullopt, Vector2{1.5, 1.0}, Vector2{0.0, 1.0});
    for (const Index f : {0, 1, 3, 5}) cells[0].addFace(f);
    for (const Index f : {0, 2, 4, 6}) cells[1].addFace(f);
    patches.emplace_back("left", std::vector<Index>{1});
    patches.emplace_back("right", std::vector<Index>{2});
    patches.emplace_back("bottom", std::vector<Index>{3, 4});
    patches.emplace_back("top", std::vector<Index>{5, 6});
  }

  [[nodiscard]] cfd::mesh::MeshQualityReport evaluate() const {
    return MeshQuality::evaluate(Mesh(cells, faces, patches));
  }
};

}  // namespace

TEST(MeshValidity, HandBuiltTwoCellMeshIsValid) {
  const auto report = TwoCellMesh{}.evaluate();
  EXPECT_TRUE(report.valid);
  EXPECT_TRUE(report.problems.empty());
}

TEST(MeshValidity, DetectsInvertedInternalFace) {
  TwoCellMesh m;
  m.faces[0] = cfd::mesh::Face(0, 0, 1, Vector2{1.0, 0.5}, Vector2{-1.0, 0.0});
  const auto report = m.evaluate();
  EXPECT_FALSE(report.valid);
  EXPECT_TRUE(hasProblemContaining(report, "face 0 area vector does not point from owner"));
}

TEST(MeshValidity, DetectsInwardBoundaryFace) {
  TwoCellMesh m;
  m.faces[3] = cfd::mesh::Face(3, 0, std::nullopt, Vector2{0.5, 0.0}, Vector2{0.0, 1.0});
  const auto report = m.evaluate();
  EXPECT_FALSE(report.valid);
  EXPECT_TRUE(hasProblemContaining(report, "face 3 (boundary) area vector does not point out"));
  EXPECT_TRUE(hasProblemContaining(report, "cell 0 is not closed"));
}

TEST(MeshValidity, DetectsOpenCell) {
  TwoCellMesh m;
  m.faces[6] = cfd::mesh::Face(6, 1, std::nullopt, Vector2{1.5, 1.0}, Vector2{0.0, 2.0});
  const auto report = m.evaluate();
  EXPECT_FALSE(report.valid);
  EXPECT_TRUE(hasProblemContaining(report, "cell 1 is not closed"));
}

TEST(MeshValidity, DetectsCellListingAFaceItDoesNotOwnOrNeighbor) {
  TwoCellMesh m;
  m.cells[0].addFace(2);  // face 2 belongs to cell 1 only
  const auto report = m.evaluate();
  EXPECT_FALSE(report.valid);
  EXPECT_TRUE(
      hasProblemContaining(report, "cell 0 lists face 2 that it neither owns nor neighbors"));
}

TEST(MeshValidity, DetectsCellWithTooFewFaces) {
  TwoCellMesh m;
  m.cells[1] = cfd::mesh::Cell(1, Vector2{1.5, 0.5}, 1.0);
  m.cells[1].addFace(0);
  m.cells[1].addFace(2);
  const auto report = m.evaluate();
  EXPECT_FALSE(report.valid);
  EXPECT_TRUE(hasProblemContaining(report, "cell 1 has fewer than 3 faces"));
}

TEST(MeshValidity, DetectsBoundaryFaceInNoPatchOrInTwo) {
  TwoCellMesh uncovered;
  uncovered.patches[2] = cfd::mesh::BoundaryPatch("bottom", std::vector<Index>{3});
  auto report = uncovered.evaluate();
  EXPECT_FALSE(report.valid);
  EXPECT_TRUE(hasProblemContaining(report, "face 4 (boundary) is in 0 boundary patches"));

  TwoCellMesh doubled;
  doubled.patches[3] = cfd::mesh::BoundaryPatch("top", std::vector<Index>{5, 6, 3});
  report = doubled.evaluate();
  EXPECT_FALSE(report.valid);
  EXPECT_TRUE(hasProblemContaining(report, "face 3 (boundary) is in 2 boundary patches"));
}

TEST(MeshValidity, DetectsPatchListingAnInternalFace) {
  TwoCellMesh m;
  m.patches[0] = cfd::mesh::BoundaryPatch("left", std::vector<Index>{1, 0});
  const auto report = m.evaluate();
  EXPECT_FALSE(report.valid);
  EXPECT_TRUE(hasProblemContaining(report, "patch 'left' lists internal face 0"));
}

// Non-finite or non-positive geometry never reaches a Mesh at all: the
// entity constructors reject it (so MeshQuality's own volume/area checks
// are a second line of defense).
TEST(MeshValidity, EntitiesRejectNonFiniteAndNonPositiveGeometry) {
  const Real nan = std::numeric_limits<Real>::quiet_NaN();
  const Real inf = std::numeric_limits<Real>::infinity();
  EXPECT_THROW((void)cfd::mesh::Cell(0, Vector2{nan, 0.5}, 1.0), InvalidArgumentError);
  EXPECT_THROW((void)cfd::mesh::Cell(0, Vector2{0.5, 0.5}, inf), InvalidArgumentError);
  EXPECT_THROW((void)cfd::mesh::Cell(0, Vector2{0.5, 0.5}, 0.0), InvalidArgumentError);
  EXPECT_THROW((void)cfd::mesh::Cell(0, Vector2{0.5, 0.5}, -1.0), InvalidArgumentError);
  EXPECT_THROW((void)cfd::mesh::Face(0, 0, 1, Vector2{inf, 0.5}, Vector2{1.0, 0.0}),
               InvalidArgumentError);
  EXPECT_THROW((void)cfd::mesh::Face(0, 0, 1, Vector2{1.0, 0.5}, Vector2{nan, 0.0}),
               InvalidArgumentError);
  EXPECT_THROW((void)cfd::mesh::Face(0, 0, 1, Vector2{1.0, 0.5}, Vector2{0.0, 0.0}),
               InvalidArgumentError);
}

TEST(MeshValidity, ProblemListIsBounded) {
  // Every internal face of a 12x12 mesh inverted: 264 problems, at most 21
  // reported (20 + the "more" marker).
  const Mesh source = MeshGeometry::createCartesian2D(12, 12, 1.0, 1.0);
  std::vector<cfd::mesh::Face> faces;
  for (const auto& face : source.faces()) {
    faces.emplace_back(face.id(), face.owner(), face.neighbor(), face.centroid(),
                       face.isBoundary() ? face.areaVector() : (face.areaVector() * -1.0));
  }
  const auto report = MeshQuality::evaluate(Mesh(source.cells(), faces, source.boundaryPatches()));
  EXPECT_FALSE(report.valid);
  EXPECT_EQ(report.problems.size(), 21u);
  EXPECT_EQ(report.problems.back(), "... more problems not listed");
}

TEST(MeshValidity, EveryCartesianAndStructuredQuadMeshIsValid) {
  for (const auto& [nx, ny] : {std::pair<Index, Index>{1, 1}, {7, 3}, {20, 20}}) {
    EXPECT_TRUE(MeshQuality::evaluate(MeshGeometry::createCartesian2D(nx, ny, 2.0, 1.0)).valid);
  }
  for (const Index n : {Index{2}, Index{8}, Index{32}}) {
    const auto report =
        MeshQuality::evaluate(MeshGeometry::createStructuredQuad2D(n, n, distortedVertices(n, n)));
    EXPECT_TRUE(report.valid) << n;
    EXPECT_TRUE(report.problems.empty()) << n;
  }
}
