// P12-MESH-001: mesh.json "structured_quad" -- parsing, cross-file
// validation against geometry.json, CaseWriter round trip, CaseBuilder
// construction and its validity gate -- plus the backward-compatibility
// guarantees for "structured_cartesian" (unchanged files, meshes and
// exports) and the export of vertex-defined meshes (VTK points are the
// true vertices; a mesh the exporters cannot describe is rejected, never
// silently mis-described).
#include <gtest/gtest.h>

#include <cmath>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <sstream>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "CaseFixture.hpp"
#include "cfd/core/Exception.hpp"
#include "cfd/io/CaseBuilder.hpp"
#include "cfd/io/CaseReader.hpp"
#include "cfd/io/CaseWriter.hpp"
#include "cfd/io/JSONWriter.hpp"
#include "cfd/io/VTKWriter.hpp"
#include "cfd/mesh/MeshGeometry.hpp"
#include "cfd/mesh/MeshQuality.hpp"

using cfd::CaseConfigurationError;
using cfd::Index;
using cfd::InvalidArgumentError;
using cfd::Real;
using cfd::Vector2;
using cfd::io::CaseBuilder;
using cfd::io::CaseDefinition;
using cfd::io::CaseReader;
using cfd::io::CaseWriter;
using cfd::mesh::Mesh;
using cfd::mesh::MeshGeometry;
using cfd::testutil::CaseFixture;

namespace {

// The CaseFixture domain is the unit square; a 3x2 vertex grid with the two
// interior vertices displaced.
std::vector<Vector2> quadVertices() {
  std::vector<Vector2> v;
  for (Index j = 0; j <= 2; ++j) {
    for (Index i = 0; i <= 3; ++i) {
      v.push_back(Vector2{static_cast<Real>(i) / 3.0, static_cast<Real>(j) / 2.0});
    }
  }
  v[(1 * 4) + 1] = Vector2{0.40, 0.55};
  v[(1 * 4) + 2] = Vector2{0.62, 0.45};
  return v;
}

std::string meshJson(const std::vector<Vector2>& vertices, Index nx = 3, Index ny = 2,
                     const std::string& type = "structured_quad") {
  nlohmann::json j{{"type", type}, {"nx", nx}, {"ny", ny}};
  nlohmann::json array = nlohmann::json::array();
  for (const auto& v : vertices) array.push_back(nlohmann::json::array({v.x, v.y}));
  j["vertices"] = array;
  return j.dump(2);
}

std::string readFile(const std::filesystem::path& path) {
  std::ifstream in(path, std::ios::binary);
  return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

std::string configErrorMessage(const CaseFixture& fixture) {
  try {
    (void)CaseReader{}.read(fixture.directory());
  } catch (const CaseConfigurationError& e) {
    return e.what();
  }
  return "";
}

cfd::pressure_velocity::SIMPLEResult zeroResult(Index cells) {
  cfd::pressure_velocity::SIMPLEResult result;
  result.velocity = cfd::fields::VectorField(cells, Vector2{0.0, 0.0});
  result.pressure = cfd::fields::ScalarField(cells, 0.0);
  return result;
}

}  // namespace

// --- Parsing -----------------------------------------------------------------

TEST(StructuredQuadCase, ParsesVertices) {
  CaseFixture fixture;
  fixture.write("mesh.json", meshJson(quadVertices()));
  const CaseDefinition definition = CaseReader{}.read(fixture.directory());
  EXPECT_EQ(definition.mesh.type, "structured_quad");
  EXPECT_EQ(definition.mesh.nx, 3u);
  EXPECT_EQ(definition.mesh.ny, 2u);
  const auto expected = quadVertices();
  ASSERT_EQ(definition.mesh.vertices.size(), expected.size());
  for (std::size_t k = 0; k < expected.size(); ++k) {
    EXPECT_EQ(definition.mesh.vertices[k].x, expected[k].x) << k;
    EXPECT_EQ(definition.mesh.vertices[k].y, expected[k].y) << k;
  }
}

TEST(StructuredQuadCase, CartesianMeshHasNoVerticesAndRejectsThem) {
  CaseFixture fixture;
  EXPECT_TRUE(CaseReader{}.read(fixture.directory()).mesh.vertices.empty());
  fixture.write("mesh.json", meshJson(quadVertices(), 3, 2, "structured_cartesian"));
  const std::string message = configErrorMessage(fixture);
  EXPECT_NE(message.find("vertices"), std::string::npos) << message;
  EXPECT_NE(message.find("structured_cartesian"), std::string::npos) << message;
}

TEST(StructuredQuadCase, MissingVerticesAreRejected) {
  CaseFixture fixture;
  fixture.write("mesh.json", R"({"type": "structured_quad", "nx": 3, "ny": 2})");
  EXPECT_NE(configErrorMessage(fixture).find("vertices"), std::string::npos);
}

TEST(StructuredQuadCase, WrongVertexCountIsRejected) {
  CaseFixture fixture;
  auto vertices = quadVertices();
  vertices.pop_back();
  fixture.write("mesh.json", meshJson(vertices));
  const std::string message = configErrorMessage(fixture);
  EXPECT_NE(message.find("(nx + 1) * (ny + 1) = 12"), std::string::npos) << message;
}

TEST(StructuredQuadCase, MalformedVertexIsRejected) {
  for (const char* bad : {R"([0.5])", R"([0.5, "a"])", R"([0.5, 0.5, 0.5])", R"({"x": 0.5})"}) {
    CaseFixture fixture;
    auto j = nlohmann::json::parse(meshJson(quadVertices()));
    j["vertices"][5] = nlohmann::json::parse(bad);
    fixture.write("mesh.json", j.dump());
    const std::string message = configErrorMessage(fixture);
    EXPECT_NE(message.find("vertices[5]"), std::string::npos) << bad << ": " << message;
  }
}

// JSON has no NaN/Inf literal, and an overflowing number ("1e400") is
// already rejected by the JSON reader itself (IOError "malformed JSON ...
// number overflow", readJsonFile). The parser's own finiteness check is the
// second line of defense, reached only by a programmatic MeshConfig -- the
// mesh builder rejects that too (test_structured_quad_mesh.cpp).
TEST(StructuredQuadCase, NonFiniteVertexIsRejected) {
  CaseFixture fixture;
  std::string text = meshJson(quadVertices());
  const std::string needle = "0.55";
  text.replace(text.find(needle), needle.size(), "1e400");
  fixture.write("mesh.json", text);
  try {
    (void)CaseReader{}.read(fixture.directory());
    FAIL() << "a non-finite vertex was accepted";
  } catch (const cfd::IOError& e) {
    EXPECT_NE(std::string(e.what()).find("number overflow"), std::string::npos) << e.what();
  }
}

// --- Cross-file validation against geometry.json ------------------------------

TEST(StructuredQuadCase, BoundaryVertexOffTheRectangleIsRejected) {
  struct Variant {
    Index vertex;
    Vector2 value;
    const char* expected;
  };
  for (const Variant& variant :
       {Variant{1, {1.0 / 3.0, 0.01}, "bottom edge"}, Variant{9, {1.0 / 3.0, 0.98}, "top edge"},
        Variant{4, {0.02, 0.5}, "left edge"}, Variant{7, {1.01, 0.5}, "right edge"}}) {
    CaseFixture fixture;
    auto vertices = quadVertices();
    vertices[variant.vertex] = variant.value;
    fixture.write("mesh.json", meshJson(vertices));
    const std::string message = configErrorMessage(fixture);
    EXPECT_NE(message.find(variant.expected), std::string::npos) << message;
    EXPECT_NE(message.find("vertices[" + std::to_string(variant.vertex) + "]"), std::string::npos)
        << message;
  }
}

TEST(StructuredQuadCase, NonMonotoneBoundaryEdgeIsRejected) {
  CaseFixture fixture;
  auto vertices = quadVertices();
  std::swap(vertices[1], vertices[2]);  // bottom edge x: 0, 2/3, 1/3, 1
  fixture.write("mesh.json", meshJson(vertices));
  EXPECT_NE(configErrorMessage(fixture).find("increase strictly in x along the bottom edge"),
            std::string::npos);
}

// --- CaseBuilder ----------------------------------------------------------------

TEST(StructuredQuadCase, BuilderConstructsTheVertexMesh) {
  CaseFixture fixture;
  fixture.write("mesh.json", meshJson(quadVertices()));
  const auto setup = CaseBuilder{}.build(CaseReader{}.read(fixture.directory()));
  const Mesh expected = MeshGeometry::createStructuredQuad2D(3, 2, quadVertices());
  ASSERT_EQ(setup.mesh.numberOfCells(), expected.numberOfCells());
  for (Index c = 0; c < expected.numberOfCells(); ++c) {
    EXPECT_EQ(setup.mesh.cell(c).centroid().x, expected.cell(c).centroid().x);
    EXPECT_EQ(setup.mesh.cell(c).centroid().y, expected.cell(c).centroid().y);
    EXPECT_EQ(setup.mesh.cell(c).volume(), expected.cell(c).volume());
  }
  ASSERT_NE(setup.mesh.structuredGrid(), nullptr);
  const auto report = cfd::mesh::MeshQuality::evaluate(setup.mesh);
  EXPECT_TRUE(report.valid);
  EXPECT_GT(report.maxNonOrthogonalityDegrees, 5.0);
}

// An interior vertex dragged across its neighbor folds two cells: the
// reader accepts it (boundary checks only), the builder must not.
TEST(StructuredQuadCase, BuilderRejectsFoldedMesh) {
  CaseFixture fixture;
  auto vertices = quadVertices();
  vertices[(1 * 4) + 1] = Vector2{0.9, 0.5};  // past vertex (2,1) at x = 0.62
  fixture.write("mesh.json", meshJson(vertices));
  const CaseDefinition definition = CaseReader{}.read(fixture.directory());
  try {
    (void)CaseBuilder{}.build(definition);
    FAIL() << "folded mesh was built";
  } catch (const CaseConfigurationError& e) {
    EXPECT_NE(std::string(e.what()).find("not a strictly convex counter-clockwise"),
              std::string::npos)
        << e.what();
  }
}

// structured_cartesian: CaseBuilder builds exactly createCartesian2D (the
// pre-existing construction) -- topology and geometry bit-identical.
TEST(StructuredQuadCase, CartesianCaseBuildsTheUnchangedCartesianMesh) {
  CaseFixture fixture;
  const auto setup = CaseBuilder{}.build(CaseReader{}.read(fixture.directory()));
  const Mesh expected = MeshGeometry::createCartesian2D(4, 4, 1.0, 1.0);
  ASSERT_EQ(setup.mesh.numberOfCells(), expected.numberOfCells());
  ASSERT_EQ(setup.mesh.numberOfFaces(), expected.numberOfFaces());
  for (Index c = 0; c < expected.numberOfCells(); ++c) {
    EXPECT_EQ(setup.mesh.cell(c).centroid().x, expected.cell(c).centroid().x);
    EXPECT_EQ(setup.mesh.cell(c).centroid().y, expected.cell(c).centroid().y);
    EXPECT_EQ(setup.mesh.cell(c).volume(), expected.cell(c).volume());
  }
  for (Index f = 0; f < expected.numberOfFaces(); ++f) {
    EXPECT_EQ(setup.mesh.face(f).areaVector().x, expected.face(f).areaVector().x);
    EXPECT_EQ(setup.mesh.face(f).areaVector().y, expected.face(f).areaVector().y);
    EXPECT_EQ(setup.mesh.face(f).centroid().x, expected.face(f).centroid().x);
    EXPECT_EQ(setup.mesh.face(f).centroid().y, expected.face(f).centroid().y);
  }
}

// --- CaseWriter -----------------------------------------------------------------

TEST(StructuredQuadCase, WriterRoundTripsVerticesExactly) {
  CaseFixture source;
  source.write("mesh.json", meshJson(quadVertices()));
  const CaseDefinition original = CaseReader{}.read(source.directory());
  CaseFixture target;
  CaseWriter::write(target.directory(), original);
  const CaseDefinition reread = CaseReader{}.read(target.directory());
  EXPECT_EQ(reread.mesh.type, "structured_quad");
  ASSERT_EQ(reread.mesh.vertices.size(), original.mesh.vertices.size());
  for (std::size_t k = 0; k < original.mesh.vertices.size(); ++k) {
    EXPECT_EQ(reread.mesh.vertices[k].x, original.mesh.vertices[k].x) << k;
    EXPECT_EQ(reread.mesh.vertices[k].y, original.mesh.vertices[k].y) << k;
  }
}

// Backward compatibility: a Cartesian case is written exactly as before --
// {type, nx, ny} and nothing else.
TEST(StructuredQuadCase, WriterKeepsCartesianMeshFileUnchanged) {
  CaseFixture source;
  const CaseDefinition original = CaseReader{}.read(source.directory());
  CaseFixture target;
  CaseWriter::write(target.directory(), original);
  const auto written = nlohmann::json::parse(readFile(target.directory() / "mesh.json"));
  EXPECT_EQ(written, (nlohmann::json{{"type", "structured_cartesian"}, {"nx", 4}, {"ny", 4}}));
}

// --- Export ----------------------------------------------------------------------

// VTK points of a structured_quad mesh are its true vertices.
TEST(StructuredQuadCase, VtkPointsAreTheMeshVertices) {
  const auto vertices = quadVertices();
  const Mesh mesh = MeshGeometry::createStructuredQuad2D(3, 2, vertices);
  const auto path = std::filesystem::temp_directory_path() / "cfdapp_quad_points.vtk";
  cfd::io::VTKWriter::writeSolution(path, mesh, zeroResult(mesh.numberOfCells()));
  std::istringstream in(readFile(path));
  std::string line;
  while (std::getline(in, line) && line.rfind("POINTS", 0) != 0) {
  }
  ASSERT_EQ(line, "POINTS 12 double");
  for (const auto& v : vertices) {
    Real x = 0.0;
    Real y = 0.0;
    Real z = 1.0;
    in >> x >> y >> z;
    EXPECT_EQ(x, v.x);  // written with 17 significant digits: exact round trip
    EXPECT_EQ(y, v.y);
    EXPECT_EQ(z, 0.0);
  }
  std::filesystem::remove(path);
}

// Backward compatibility: a Cartesian mesh's VTK file is byte-identical
// whether its points come from the vertex grid it now carries or from the
// pre-existing reconstruction (a grid-less copy of the same mesh takes
// that unchanged code path).
TEST(StructuredQuadCase, CartesianVtkIsByteIdenticalToTheReconstructedPoints) {
  for (const auto& [nx, ny, lx, ly] : {std::tuple<Index, Index, Real, Real>{4, 4, 1.0, 1.0},
                                       {7, 3, 0.3, 0.1},
                                       {64, 8, 8.0, 1.0}}) {
    const Mesh withGrid = MeshGeometry::createCartesian2D(nx, ny, lx, ly);
    const Mesh withoutGrid(withGrid.cells(), withGrid.faces(), withGrid.boundaryPatches());
    ASSERT_EQ(withoutGrid.structuredGrid(), nullptr);
    const auto a = std::filesystem::temp_directory_path() / "cfdapp_cart_grid.vtk";
    const auto b = std::filesystem::temp_directory_path() / "cfdapp_cart_nogrid.vtk";
    cfd::io::VTKWriter::writeSolution(a, withGrid, zeroResult(withGrid.numberOfCells()));
    cfd::io::VTKWriter::writeSolution(b, withoutGrid, zeroResult(withGrid.numberOfCells()));
    EXPECT_EQ(readFile(a), readFile(b)) << nx << "x" << ny;
    std::filesystem::remove(a);
    std::filesystem::remove(b);
  }
}

// A non-Cartesian mesh without a vertex grid cannot be described by the
// exporters' structured representation: rejected, never exported with a
// wrong nx/ny or wrong points.
TEST(StructuredQuadCase, ExportRejectsANonCartesianMeshWithoutGrid) {
  const Mesh quad = MeshGeometry::createStructuredQuad2D(3, 2, quadVertices());
  const Mesh gridless(quad.cells(), quad.faces(), quad.boundaryPatches());
  const auto path = std::filesystem::temp_directory_path() / "cfdapp_gridless.vtk";
  EXPECT_THROW(cfd::io::VTKWriter::writeSolution(path, gridless, zeroResult(6)),
               InvalidArgumentError);
  std::filesystem::remove(path);
}
