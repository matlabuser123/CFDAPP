// P12-MESH-003: mesh.json "multiblock" + geometry.json "mesh_defined" --
// parsing and its errors, the geometry <-> mesh cross-check, boundaries.json
// against the mesh's own named patches, CaseWriter round trip, CaseBuilder
// construction (every geometric failure a case error, a disconnected domain
// rejected by the validity gate), and the VTK / JSON export of multi-block
// meshes.
#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <functional>
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
using cfd::Real;
using cfd::Vector2;
using cfd::io::CaseBuilder;
using cfd::io::CaseDefinition;
using cfd::io::CaseReader;
using cfd::io::CaseWriter;
using cfd::mesh::Mesh;
using cfd::testutil::CaseFixture;
using nlohmann::json;

namespace {

json blockJson(const std::string& name, Real x0, Real x1, Real y0, Real y1, Index nx, Index ny) {
  json vertices = json::array();
  for (Index j = 0; j <= ny; ++j) {
    for (Index i = 0; i <= nx; ++i) {
      vertices.push_back(
          json::array({x0 + ((x1 - x0) * static_cast<Real>(i) / static_cast<Real>(nx)),
                       y0 + ((y1 - y0) * static_cast<Real>(j) / static_cast<Real>(ny))}));
    }
  }
  return json{{"name", name}, {"nx", nx}, {"ny", ny}, {"vertices", vertices}};
}

json side(const std::string& block, const std::string& s) {
  return json{{"block", block}, {"side", s}};
}

// L-shaped step channel (upstream [0,1]x[.5,1], upper [1,3]x[.5,1], lower
// [1,3]x[0,.5]) -- 8 + 16 + 16 = 40 cells.
json lShapeMesh() {
  return json{
      {"type", "multiblock"},
      {"blocks",
       {blockJson("upstream", 0.0, 1.0, 0.5, 1.0, 4, 2),
        blockJson("upper", 1.0, 3.0, 0.5, 1.0, 8, 2),
        blockJson("lower", 1.0, 3.0, 0.0, 0.5, 8, 2)}},
      {"interfaces",
       {json{{"first", side("upstream", "right")}, {"second", side("upper", "left")}},
        json{{"first", side("lower", "top")},
             {"second", side("upper", "bottom")},
             {"orientation", "aligned"}}}},
      {"patches",
       {json{{"name", "inlet"}, {"sides", {side("upstream", "left")}}},
        json{{"name", "outlet"}, {"sides", {side("upper", "right"), side("lower", "right")}}},
        json{{"name", "top_wall"}, {"sides", {side("upstream", "top"), side("upper", "top")}}},
        json{{"name", "bottom_wall"},
             {"sides", {side("upstream", "bottom"), side("lower", "bottom")}}},
        json{{"name", "step"}, {"sides", {side("lower", "left")}}}}}};
}

json lShapeBoundaries() {
  const json wall{{"velocity", {{"type", "wall"}}},
                  {"pressure", {{"type", "fixed_gradient"}, {"value", 0.0}}}};
  return json{{"patches",
               {{"inlet",
                 {{"velocity", {{"type", "inlet"}, {"value", {1.0, 0.0}}}},
                  {"pressure", {{"type", "fixed_gradient"}, {"value", 0.0}}}}},
                {"outlet",
                 {{"velocity", {{"type", "outlet"}}},
                  {"pressure", {{"type", "fixed_value"}, {"value", 0.0}}}}},
                {"top_wall", wall},
                {"bottom_wall", wall},
                {"step", wall}}}};
}

// A CaseFixture turned into the L-shaped multiblock case; `edit` may alter
// the mesh.json document first.
void writeLShape(const CaseFixture& fixture, const std::function<void(json&)>& edit = {}) {
  json mesh = lShapeMesh();
  if (edit) edit(mesh);
  fixture.write("mesh.json", mesh.dump(2));
  fixture.write("geometry.json", R"({"type": "mesh_defined"})");
  fixture.write("boundaries.json", lShapeBoundaries().dump(2));
}

std::string readFile(const std::filesystem::path& path) {
  std::ifstream in(path, std::ios::binary);
  return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

std::string readError(const CaseFixture& fixture) {
  try {
    (void)CaseReader{}.read(fixture.directory());
  } catch (const CaseConfigurationError& e) {
    return e.what();
  }
  return "no error";
}

std::string buildError(const CaseFixture& fixture) {
  try {
    (void)CaseBuilder{}.build(CaseReader{}.read(fixture.directory()));
  } catch (const CaseConfigurationError& e) {
    return e.what();
  }
  return "no error";
}

cfd::pressure_velocity::SIMPLEResult zeroResult(Index cells) {
  cfd::pressure_velocity::SIMPLEResult result;
  result.velocity = cfd::fields::VectorField(cells, Vector2{0.0, 0.0});
  result.pressure = cfd::fields::ScalarField(cells, 0.0);
  return result;
}

}  // namespace

// --- Parsing ------------------------------------------------------------------

TEST(MultiBlockCase, ReaderParsesBlocksInterfacesAndPatches) {
  CaseFixture fixture;
  writeLShape(fixture);
  const CaseDefinition d = CaseReader{}.read(fixture.directory());
  EXPECT_EQ(d.geometry.type, "mesh_defined");
  EXPECT_EQ(d.mesh.type, "multiblock");
  EXPECT_EQ(d.mesh.nx, 0u);
  EXPECT_TRUE(d.mesh.vertices.empty());
  EXPECT_FALSE(d.mesh.grading.has_value());
  ASSERT_EQ(d.mesh.blocks.size(), 3u);
  EXPECT_EQ(d.mesh.blocks[1].name, "upper");
  EXPECT_EQ(d.mesh.blocks[1].nx, 8u);
  EXPECT_EQ(d.mesh.blocks[1].vertices.size(), 27u);
  EXPECT_EQ(d.mesh.blocks[2].vertices[4].x, 2.0);
  ASSERT_EQ(d.mesh.interfaces.size(), 2u);
  EXPECT_EQ(d.mesh.interfaces[0].first.block, "upstream");
  EXPECT_EQ(d.mesh.interfaces[0].second.side, "left");
  EXPECT_FALSE(d.mesh.interfaces[0].reversed);  // "orientation" defaults to aligned
  ASSERT_EQ(d.mesh.patches.size(), 5u);
  EXPECT_EQ(d.mesh.patches[1].name, "outlet");
  EXPECT_EQ(d.mesh.patches[1].sides.size(), 2u);
  EXPECT_EQ(cfd::io::meshPatchNames(d.mesh),
            (std::vector<std::string>{"inlet", "outlet", "top_wall", "bottom_wall", "step"}));
  EXPECT_EQ(d.boundaries.patches.size(), 5u);
}

TEST(MultiBlockCase, ParserErrorsNameTheField) {
  struct Probe {
    const char* what;
    std::function<void(json&)> edit;
    const char* fragment;
  };
  const std::vector<Probe> probes = {
      {"nx on multiblock", [](json& m) { m["nx"] = 4; },
       "field \"nx\" must satisfy be absent for type multiblock"},
      {"grading on multiblock", [](json& m) { m["grading"] = json::object(); }, "grading"},
      {"no blocks", [](json& m) { m.erase("blocks"); }, "blocks"},
      {"empty blocks", [](json& m) { m["blocks"] = json::array(); }, "non-empty array of blocks"},
      {"bad block name", [](json& m) { m["blocks"][0]["name"] = "up stream"; },
       "field \"blocks[0].name\" must satisfy be 1-64 characters"},
      {"duplicate block", [](json& m) { m["blocks"][2]["name"] = "upper"; },
       "field \"blocks[2].name\" must satisfy be unique"},
      {"zero nx", [](json& m) { m["blocks"][1]["nx"] = 0; },
       "field \"blocks[1].nx\" must satisfy be > 0"},
      {"vertex count",
       [](json& m) { m["blocks"][1]["vertices"].erase(m["blocks"][1]["vertices"].begin()); },
       "field \"blocks[1].vertices\" must satisfy be an array of (nx + 1) * (ny + 1) = 27"},
      {"vertex shape", [](json& m) { m["blocks"][0]["vertices"][3] = json::array({1.0}); },
       "field \"blocks[0].vertices[3]\" must satisfy be an array of exactly 2 numbers"},
      {"unknown block key", [](json& m) { m["blocks"][0]["colour"] = "red"; }, "colour"},
      {"unknown interface block",
       [](json& m) { m["interfaces"][0]["second"]["block"] = "nowhere"; },
       "field \"interfaces[0].second.block\" must satisfy name a block of this mesh"},
      {"bad side", [](json& m) { m["interfaces"][1]["first"]["side"] = "front"; },
       "field \"interfaces[1].first.side\" must satisfy be one of: left, right, bottom, top"},
      {"bad orientation", [](json& m) { m["interfaces"][1]["orientation"] = "flipped"; },
       "field \"interfaces[1].orientation\" must satisfy be one of: aligned, reversed"},
      {"no patches", [](json& m) { m.erase("patches"); }, "patches"},
      {"duplicate patch", [](json& m) { m["patches"][4]["name"] = "inlet"; },
       "field \"patches[4].name\" must satisfy be unique"},
      {"empty patch", [](json& m) { m["patches"][4]["sides"] = json::array(); },
       "field \"patches[4].sides\" must satisfy be a non-empty array"},
      {"side reused", [](json& m) { m["patches"][4]["sides"].push_back(side("upper", "left")); },
       "not reuse block 'upper' side 'left' (already used by interfaces[0].second"},
      // json::array(...), not a braced list: `j = {one_json_object}` is an array under GCC but
      // the object itself under Clang (basic_json::operator=(basic_json) wins over the
      // initializer_list constructor for a single element), which made this probe hit the
      // "sides must be a non-empty array" schema rule instead of the reuse rule it targets.
      {"side unassigned",
       [](json& m) { m["patches"][4]["sides"] = json::array({side("upper", "right")}); },
       "not reuse block 'upper' side 'right'"},
      {"side missing", [](json& m) { m["patches"].erase(4); },
       "block 'lower' side 'left' is in no interface and no patch"},
  };
  for (const auto& probe : probes) {
    CaseFixture fixture;
    writeLShape(fixture, probe.edit);
    const std::string message = readError(fixture);
    EXPECT_NE(message.find(probe.fragment), std::string::npos) << probe.what << ": " << message;
    EXPECT_NE(message.find("mesh.json"), std::string::npos) << probe.what << ": " << message;
  }
  // Multiblock keys on a single-grid mesh type.
  CaseFixture fixture;
  fixture.write("mesh.json",
                R"({"type": "structured_cartesian", "nx": 4, "ny": 4, "patches": []})");
  EXPECT_NE(
      readError(fixture).find("field \"patches\" must satisfy be absent unless type is multiblock"),
      std::string::npos)
      << readError(fixture);
}

TEST(MultiBlockCase, GeometryMustBeMeshDefinedExactlyForMultiblock) {
  {
    CaseFixture fixture;
    writeLShape(fixture);
    fixture.write("geometry.json", R"({"type": "rectangle", "length": 3.0, "height": 1.0})");
    const std::string m = readError(fixture);
    EXPECT_NE(m.find("be mesh_defined exactly when mesh.json type is multiblock"),
              std::string::npos)
        << m;
  }
  {
    CaseFixture fixture;  // Cartesian mesh with a mesh_defined geometry
    fixture.write("geometry.json", R"({"type": "mesh_defined"})");
    const std::string m = readError(fixture);
    EXPECT_NE(m.find("(mesh type structured_cartesian)"), std::string::npos) << m;
  }
  {
    CaseFixture fixture;
    writeLShape(fixture);
    fixture.write("geometry.json", R"({"type": "mesh_defined", "length": 3.0})");
    const std::string m = readError(fixture);
    EXPECT_NE(m.find("field \"length\" must satisfy be absent for type mesh_defined"),
              std::string::npos)
        << m;
  }
}

TEST(MultiBlockCase, BoundariesMustConfigureExactlyTheMeshPatches) {
  {
    CaseFixture fixture;
    writeLShape(fixture);
    json b = lShapeBoundaries();
    b["patches"].erase("step");
    fixture.write("boundaries.json", b.dump());
    EXPECT_NE(readError(fixture).find("field \"patches.step\" must satisfy be configured"),
              std::string::npos)
        << readError(fixture);
  }
  {
    CaseFixture fixture;
    writeLShape(fixture);
    json b = lShapeBoundaries();
    b["patches"]["left"] = b["patches"]["step"];  // a canonical name the mesh does not have
    fixture.write("boundaries.json", b.dump());
    const std::string m = readError(fixture);
    EXPECT_NE(m.find("field \"patches.left\" must satisfy name a patch that exists on the "
                     "generated mesh (inlet, "
                     "outlet, top_wall, bottom_wall, step)"),
              std::string::npos)
        << m;
  }
}

// --- CaseWriter --------------------------------------------------------------------

TEST(MultiBlockCase, WriterRoundTripsExactly) {
  CaseFixture source;
  writeLShape(source, [](json& m) { m["interfaces"][1]["orientation"] = "aligned"; });
  const CaseDefinition original = CaseReader{}.read(source.directory());
  CaseFixture target;
  CaseWriter::write(target.directory(), original);
  EXPECT_EQ(json::parse(readFile(target.directory() / "geometry.json")),
            (json{{"type", "mesh_defined"}}));
  const json written = json::parse(readFile(target.directory() / "mesh.json"));
  EXPECT_FALSE(written.contains("nx"));
  EXPECT_FALSE(written.contains("vertices"));
  const CaseDefinition reread = CaseReader{}.read(target.directory());
  ASSERT_EQ(reread.mesh.blocks.size(), original.mesh.blocks.size());
  for (std::size_t b = 0; b < original.mesh.blocks.size(); ++b) {
    EXPECT_EQ(reread.mesh.blocks[b].name, original.mesh.blocks[b].name);
    EXPECT_EQ(reread.mesh.blocks[b].nx, original.mesh.blocks[b].nx);
    EXPECT_EQ(reread.mesh.blocks[b].ny, original.mesh.blocks[b].ny);
    EXPECT_EQ(reread.mesh.blocks[b].vertices, original.mesh.blocks[b].vertices);  // bitwise
  }
  ASSERT_EQ(reread.mesh.interfaces.size(), 2u);
  for (std::size_t n = 0; n < 2; ++n) {
    EXPECT_EQ(reread.mesh.interfaces[n].first, original.mesh.interfaces[n].first);
    EXPECT_EQ(reread.mesh.interfaces[n].second, original.mesh.interfaces[n].second);
    EXPECT_EQ(reread.mesh.interfaces[n].reversed, original.mesh.interfaces[n].reversed);
  }
  ASSERT_EQ(reread.mesh.patches.size(), 5u);
  for (std::size_t p = 0; p < 5; ++p) {
    EXPECT_EQ(reread.mesh.patches[p].name, original.mesh.patches[p].name);
    EXPECT_EQ(reread.mesh.patches[p].sides, original.mesh.patches[p].sides);
  }
  EXPECT_EQ(reread.boundaries.patches.size(), 5u);
}

// --- CaseBuilder -------------------------------------------------------------------

TEST(MultiBlockCase, BuilderConstructsTheMultiBlockMeshWithNamedBoundaries) {
  CaseFixture fixture;
  writeLShape(fixture);
  const auto setup = CaseBuilder{}.build(CaseReader{}.read(fixture.directory()));
  EXPECT_EQ(setup.mesh.numberOfCells(), 40u);
  EXPECT_EQ(setup.mesh.structuredBlocks().size(), 3u);
  EXPECT_EQ(setup.mesh.boundaryPatches().size(), 5u);
  for (const char* name : {"inlet", "outlet", "top_wall", "bottom_wall", "step"}) {
    EXPECT_TRUE(setup.velocityBoundaries.has(name)) << name;
    EXPECT_TRUE(setup.pressureBoundaries.has(name)) << name;
  }
  EXPECT_TRUE(setup.velocityBoundaries.missingPatches(setup.mesh).empty());
  const auto quality = cfd::mesh::MeshQuality::evaluate(setup.mesh);
  EXPECT_TRUE(quality.valid);
  EXPECT_EQ(quality.connectedComponents, 1u);
}

TEST(MultiBlockCase, GeometricErrorsAreCaseErrors) {
  {
    // Interface vertices that do not coincide (the lower block lifted).
    CaseFixture fixture;
    writeLShape(fixture, [](json& m) {
      for (auto& v : m["blocks"][2]["vertices"]) v[1] = v[1].get<Real>() + 0.01;
    });
    const std::string e = buildError(fixture);
    EXPECT_NE(e.find("mesh.json: invalid multiblock mesh: multi-block mesh: interface 1"),
              std::string::npos)
        << e;
    EXPECT_NE(e.find("does not coincide"), std::string::npos) << e;
  }
  {
    // An inverted cell.
    CaseFixture fixture;
    writeLShape(fixture, [](json& m) {
      m["blocks"][0]["vertices"][6] = json::array({0.6, 0.8});  // past vertex 7 (0.5, 0.75)
    });
    const std::string e = buildError(fixture);
    EXPECT_NE(e.find("block 'upstream' cell ("), std::string::npos) << e;
  }
  {
    // Overlapping blocks: the lower block moved under the upstream one.
    CaseFixture fixture;
    writeLShape(fixture, [](json& m) {
      m["interfaces"].erase(1);
      m["blocks"][2] = blockJson("lower", 0.5, 2.5, -0.25, 0.75, 8, 2);
      m["patches"][4]["sides"].push_back(side("lower", "top"));
      m["patches"][4]["sides"].push_back(side("upper", "bottom"));
    });
    const std::string e = buildError(fixture);
    EXPECT_NE(e.find("mesh.json: invalid multiblock mesh"), std::string::npos) << e;
    EXPECT_NE(e.find("cross or touch"), std::string::npos) << e;
  }
  {
    // Disconnected: the lower block detached (no interface, not touching).
    CaseFixture fixture;
    writeLShape(fixture, [](json& m) {
      m["interfaces"].erase(1);
      m["blocks"][2] = blockJson("lower", 1.0, 3.0, -1.0, -0.5, 8, 2);
      m["patches"][4]["sides"].push_back(side("lower", "top"));
      m["patches"][4]["sides"].push_back(side("upper", "bottom"));
    });
    const std::string e = buildError(fixture);
    EXPECT_NE(e.find("mesh.json: the multiblock mesh is invalid"), std::string::npos) << e;
    EXPECT_NE(e.find("mesh has 2 disconnected cell regions (cells per region: 24, 16)"),
              std::string::npos)
        << e;
  }
}

// --- Export --------------------------------------------------------------------------

// VTK: every block's vertices, then one quad per cell whose four points are
// that cell's own corners (checked against the block vertex grids).
TEST(MultiBlockCase, VtkWritesBlockVerticesAndCellCorners) {
  CaseFixture fixture;
  writeLShape(fixture);
  const CaseDefinition d = CaseReader{}.read(fixture.directory());
  const auto setup = CaseBuilder{}.build(d);
  const auto path = std::filesystem::temp_directory_path() / "cfdapp_multiblock.vtk";
  cfd::io::VTKWriter::writeSolution(path, setup.mesh, zeroResult(setup.mesh.numberOfCells()));
  std::istringstream in(readFile(path));
  std::string line;
  while (std::getline(in, line) && line.rfind("POINTS", 0) != 0) {
  }
  ASSERT_EQ(line, "POINTS 69 double");  // 15 + 27 + 27
  std::vector<Vector2> points;
  for (Index k = 0; k < 69; ++k) {
    Real x = 0.0, y = 0.0, z = 1.0;
    in >> x >> y >> z;
    points.push_back(Vector2{x, y});
    EXPECT_EQ(z, 0.0);
  }
  std::vector<Vector2> expected;
  for (const auto& block : d.mesh.blocks) {
    expected.insert(expected.end(), block.vertices.begin(), block.vertices.end());
  }
  EXPECT_EQ(points, expected);
  std::string keyword;
  Index cells = 0, size = 0;
  in >> keyword >> cells >> size;
  ASSERT_EQ(keyword, "CELLS");
  ASSERT_EQ(cells, 40u);
  EXPECT_EQ(size, 200u);
  for (Index c = 0; c < cells; ++c) {
    Index n = 0, p[4] = {0, 0, 0, 0};
    in >> n >> p[0] >> p[1] >> p[2] >> p[3];
    ASSERT_EQ(n, 4u);
    // Shoelace centroid / area of the written quad == the mesh cell's.
    Real area = 0.0;
    for (int k = 0; k < 4; ++k) {
      const Vector2& a = points[p[k]];
      const Vector2& b = points[p[(k + 1) % 4]];
      area += 0.5 * ((a.x * b.y) - (b.x * a.y));
    }
    EXPECT_NEAR(area, setup.mesh.cell(c).volume(), 1e-14) << c;  // counter-clockwise, > 0
    const Vector2 mean = 0.25 * (points[p[0]] + points[p[1]] + points[p[2]] + points[p[3]]);
    EXPECT_NEAR(mean.x, setup.mesh.cell(c).centroid().x, 1e-14) << c;  // rectangles
    EXPECT_NEAR(mean.y, setup.mesh.cell(c).centroid().y, 1e-14) << c;
  }
  while (std::getline(in, line) && line.rfind("CELL_TYPES", 0) != 0) {
  }
  EXPECT_EQ(line, "CELL_TYPES 40");
  std::filesystem::remove(path);
}

TEST(MultiBlockCase, JsonMetadataListsTheBlocks) {
  CaseFixture fixture;
  writeLShape(fixture);
  const auto setup = CaseBuilder{}.build(CaseReader{}.read(fixture.directory()));
  const auto path = std::filesystem::temp_directory_path() / "cfdapp_multiblock_metadata.json";
  cfd::io::JSONWriter::writeMetadata(path, cfd::io::RunMetadata{"L", 1.0, 0.01, "SIMPLE"},
                                     setup.mesh, zeroResult(setup.mesh.numberOfCells()));
  const json doc = json::parse(readFile(path));
  EXPECT_EQ(doc.at("mesh").at("nx"), 0);
  EXPECT_EQ(doc.at("mesh").at("ny"), 0);
  EXPECT_EQ(doc.at("mesh").at("blocks"),
            (json::array({json{{"name", "upstream"}, {"nx", 4}, {"ny", 2}},
                          json{{"name", "upper"}, {"nx", 8}, {"ny", 2}},
                          json{{"name", "lower"}, {"nx", 8}, {"ny", 2}}})));
  std::filesystem::remove(path);
  // Single-grid meshes: no "blocks" key (metadata unchanged).
  const Mesh cartesian = cfd::mesh::MeshGeometry::createCartesian2D(4, 3, 1.0, 1.0);
  cfd::io::JSONWriter::writeMetadata(path, cfd::io::RunMetadata{"C", 1.0, 0.01, "SIMPLE"},
                                     cartesian, zeroResult(12));
  const json cart = json::parse(readFile(path));
  EXPECT_EQ(cart.at("mesh").at("nx"), 4);
  EXPECT_FALSE(cart.at("mesh").contains("blocks"));
  std::filesystem::remove(path);
}
