// P12-MESH-002: mesh.json "grading" -- parsing, validation against
// geometry.json before any mesh is built, CaseWriter round trip, CaseBuilder
// construction, and backward compatibility of ungraded cases.
#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

#include <nlohmann/json.hpp>

#include "CaseFixture.hpp"
#include "cfd/core/Exception.hpp"
#include "cfd/io/CaseBuilder.hpp"
#include "cfd/io/CaseReader.hpp"
#include "cfd/io/CaseWriter.hpp"
#include "cfd/mesh/MeshGeometry.hpp"
#include "cfd/mesh/MeshQuality.hpp"

using cfd::CaseConfigurationError;
using cfd::Index;
using cfd::Real;
using cfd::io::CaseBuilder;
using cfd::io::CaseDefinition;
using cfd::io::CaseReader;
using cfd::io::CaseWriter;
using cfd::mesh::AxisGrading;
using cfd::mesh::GradingCluster;
using cfd::mesh::GradingType;
using cfd::testutil::CaseFixture;

namespace {

std::string meshWithGrading(const std::string& grading, const char* type = "structured_cartesian") {
  return std::string(R"({"type": ")") + type + R"(", "nx": 8, "ny": 10, "grading": )" + grading +
         "}";
}

CaseDefinition readWith(const std::string& meshJson) {
  CaseFixture fixture;
  fixture.write("mesh.json", meshJson);
  return CaseReader{}.read(fixture.directory());
}

std::string errorFor(const std::string& meshJson, const std::string& geometry = "") {
  CaseFixture fixture;
  fixture.write("mesh.json", meshJson);
  if (!geometry.empty()) fixture.write("geometry.json", geometry);
  try {
    (void)CaseReader{}.read(fixture.directory());
  } catch (const CaseConfigurationError& e) {
    return e.what();
  }
  return "";
}

AxisGrading geometric(Real ratio, GradingCluster cluster) {
  return AxisGrading{GradingType::Geometric, ratio, cluster};
}

std::string readFile(const std::filesystem::path& path) {
  std::ifstream in(path, std::ios::binary);
  return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

}  // namespace

// --- Parsing ----------------------------------------------------------------------------

TEST(GradedMeshCase, NoGradingMeansNone) {
  CaseFixture fixture;
  EXPECT_FALSE(CaseReader{}.read(fixture.directory()).mesh.grading.has_value());
}

TEST(GradedMeshCase, ParsesYGrading) {
  const auto d =
      readWith(meshWithGrading(R"({"y": {"type": "geometric", "ratio": 1.2, "cluster": "both"}})"));
  ASSERT_TRUE(d.mesh.grading.has_value());
  EXPECT_EQ(d.mesh.grading->y, geometric(1.2, GradingCluster::Both));
  EXPECT_EQ(d.mesh.grading->x, AxisGrading{});  // absent axis: uniform
}

TEST(GradedMeshCase, ParsesXGrading) {
  const auto d = readWith(meshWithGrading(
      R"({"x": {"type": "geometric", "ratio": 1.05, "cluster": "left"}, "y": {"type": "uniform"}})"));
  EXPECT_EQ(d.mesh.grading->x, geometric(1.05, GradingCluster::Start));
  EXPECT_EQ(d.mesh.grading->y, AxisGrading{});
}

TEST(GradedMeshCase, ParsesXAndYGradingAndEveryClusterName) {
  struct Variant {
    const char* x;
    const char* y;
    GradingCluster xc;
    GradingCluster yc;
  };
  for (const Variant& v : {Variant{"left", "bottom", GradingCluster::Start, GradingCluster::Start},
                           Variant{"right", "top", GradingCluster::End, GradingCluster::End},
                           Variant{"both", "both", GradingCluster::Both, GradingCluster::Both}}) {
    const auto d = readWith(meshWithGrading(
        std::string(R"({"x": {"type": "geometric", "ratio": 1.1, "cluster": ")") + v.x +
        R"("}, "y": {"type": "geometric", "ratio": 1.3, "cluster": ")" + v.y + R"("}})"));
    EXPECT_EQ(d.mesh.grading->x, geometric(1.1, v.xc)) << v.x;
    EXPECT_EQ(d.mesh.grading->y, geometric(1.3, v.yc)) << v.y;
  }
}

// --- Rejection ------------------------------------------------------------------------------

TEST(GradedMeshCase, InvalidRatiosAreRejected) {
  for (const char* ratio : {"0", "-1.2", "0.5", "\"1.2\"", "true"}) {
    const std::string message =
        errorFor(meshWithGrading(std::string(R"({"y": {"type": "geometric", "ratio": )") + ratio +
                                 R"(, "cluster": "top"}})"));
    EXPECT_NE(message.find("grading.y.ratio"), std::string::npos) << ratio << ": " << message;
  }
}

// JSON has no NaN/Inf literal; an overflowing number is rejected by the JSON
// reader (the grading's own finiteness check backs it up programmatically).
TEST(GradedMeshCase, OverflowingRatioLiteralIsRejected) {
  CaseFixture fixture;
  fixture.write(
      "mesh.json",
      meshWithGrading(R"({"y": {"type": "geometric", "ratio": 1e400, "cluster": "top"}})"));
  EXPECT_THROW((void)CaseReader{}.read(fixture.directory()), cfd::IOError);
}

TEST(GradedMeshCase, InvalidTypeClusterAndKeysAreRejected) {
  struct Bad {
    const char* grading;
    const char* field;
  };
  for (const Bad& b : {
           Bad{R"({"y": {"type": "tanh"}})", "grading.y.type"},
           Bad{R"({"y": {"type": "geometric", "ratio": 1.2, "cluster": "left"}})",
               "grading.y.cluster"},  // x-axis name on y
           Bad{R"({"x": {"type": "geometric", "ratio": 1.2, "cluster": "top"}})",
               "grading.x.cluster"},
           Bad{R"({"y": {"type": "geometric", "ratio": 1.2}})", "grading.y.cluster"},
           Bad{R"({"y": {"type": "geometric", "cluster": "top"}})", "grading.y.ratio"},
           Bad{R"({"y": {"type": "uniform", "ratio": 1.2}})", "grading.y.ratio"},
           Bad{R"({"y": {"type": "geometric", "ratio": 1.2, "cluster": "top", "n": 3}})", "n"},
           Bad{R"({"z": {"type": "uniform"}})", "z"},
           Bad{R"([1.2])", "grading"},
           Bad{R"({"y": "geometric"})", "grading.y"},
       }) {
    const std::string message = errorFor(meshWithGrading(b.grading));
    EXPECT_NE(message.find(b.field), std::string::npos) << b.grading << ": " << message;
  }
}

TEST(GradedMeshCase, GradingIsRejectedForStructuredQuad) {
  CaseFixture fixture;
  fixture.write("mesh.json", R"({"type": "structured_quad", "nx": 1, "ny": 1,
      "vertices": [[0, 0], [1, 0], [0, 1], [1, 1]],
      "grading": {"y": {"type": "geometric", "ratio": 1.2, "cluster": "top"}}})");
  std::string message;
  try {
    (void)CaseReader{}.read(fixture.directory());
  } catch (const CaseConfigurationError& e) {
    message = e.what();
  }
  EXPECT_NE(message.find("grading"), std::string::npos) << message;
  EXPECT_NE(message.find("structured_quad"), std::string::npos) << message;
}

// Unusable distributions are rejected against geometry.json before any mesh
// is built, naming the axis: overflow (10^400) and vanishing cells
// (2^-40 of the height).
TEST(GradedMeshCase, UnusableDistributionsAreRejectedBeforeMeshConstruction) {
  const std::string overflow = errorFor(
      R"({"type": "structured_cartesian", "nx": 4, "ny": 400,
          "grading": {"y": {"type": "geometric", "ratio": 10, "cluster": "top"}}})");
  EXPECT_NE(overflow.find("grading.y.ratio"), std::string::npos) << overflow;
  EXPECT_NE(overflow.find("reduce the ratio or the cell count"), std::string::npos) << overflow;
  const std::string vanishing = errorFor(
      R"({"type": "structured_cartesian", "nx": 40, "ny": 4,
          "grading": {"x": {"type": "geometric", "ratio": 2, "cluster": "right"}}})");
  EXPECT_NE(vanishing.find("grading.x.ratio"), std::string::npos) << vanishing;
  EXPECT_NE(vanishing.find("smallest cell"), std::string::npos) << vanishing;
}

// Zero / negative domain lengths are rejected (geometry.json) before any
// grading is evaluated; zero cells too (mesh.json).
TEST(GradedMeshCase, ImpossibleDomainOrCellCountIsRejected) {
  const std::string grading = R"({"y": {"type": "geometric", "ratio": 1.2, "cluster": "both"}})";
  EXPECT_NE(errorFor(meshWithGrading(grading), R"({"type": "rectangle", "length": 0, "height": 1})")
                .find("length"),
            std::string::npos);
  EXPECT_NE(
      errorFor(meshWithGrading(grading), R"({"type": "rectangle", "length": 1, "height": -1})")
          .find("height"),
      std::string::npos);
  EXPECT_NE(
      errorFor(R"({"type": "structured_cartesian", "nx": 8, "ny": 0, "grading": )" + grading + "}")
          .find("ny"),
      std::string::npos);
}

// --- Round trip, build, backward compatibility ---------------------------------------------------

TEST(GradedMeshCase, WriterRoundTripsGradingExactly) {
  CaseFixture source;
  source.write("mesh.json",
               meshWithGrading(R"({"x": {"type": "geometric", "ratio": 1.0625, "cluster": "right"},
                                   "y": {"type": "geometric", "ratio": 1.2, "cluster": "both"}})"));
  const CaseDefinition original = CaseReader{}.read(source.directory());
  CaseFixture target;
  CaseWriter::write(target.directory(), original);
  const CaseDefinition reread = CaseReader{}.read(target.directory());
  ASSERT_TRUE(reread.mesh.grading.has_value());
  EXPECT_EQ(reread.mesh.grading, original.mesh.grading);
  const auto written = nlohmann::json::parse(readFile(target.directory() / "mesh.json"));
  EXPECT_EQ(written.at("grading").at("x").at("cluster"), "right");
  EXPECT_EQ(written.at("grading").at("y").at("ratio"), 1.2);
}

// An ungraded case is written exactly as before ({type, nx, ny}) and a
// uniform-only grading stays a (uniform) grading through the round trip.
TEST(GradedMeshCase, UngradedCaseIsWrittenUnchanged) {
  CaseFixture source;
  const CaseDefinition original = CaseReader{}.read(source.directory());
  CaseFixture target;
  CaseWriter::write(target.directory(), original);
  EXPECT_EQ(nlohmann::json::parse(readFile(target.directory() / "mesh.json")),
            (nlohmann::json{{"type", "structured_cartesian"}, {"nx", 4}, {"ny", 4}}));
  const auto uniformOnly = readWith(meshWithGrading(R"({})"));
  ASSERT_TRUE(uniformOnly.mesh.grading.has_value());
  EXPECT_EQ(*uniformOnly.mesh.grading, cfd::io::MeshGradingConfig{});
}

TEST(GradedMeshCase, BuilderBuildsTheGradedMesh) {
  CaseFixture fixture;
  fixture.write(
      "mesh.json",
      meshWithGrading(R"({"y": {"type": "geometric", "ratio": 1.2, "cluster": "both"}})"));
  const auto setup = CaseBuilder{}.build(CaseReader{}.read(fixture.directory()));
  const auto expected = cfd::mesh::MeshGeometry::createGraded2D(
      8, 10, 1.0, 1.0, AxisGrading{}, geometric(1.2, GradingCluster::Both));
  ASSERT_EQ(setup.mesh.numberOfCells(), expected.numberOfCells());
  for (Index c = 0; c < expected.numberOfCells(); ++c) {
    EXPECT_EQ(setup.mesh.cell(c).centroid().y, expected.cell(c).centroid().y);
    EXPECT_EQ(setup.mesh.cell(c).volume(), expected.cell(c).volume());
  }
  const auto report = cfd::mesh::MeshQuality::evaluate(setup.mesh);
  EXPECT_TRUE(report.valid);
  EXPECT_GT(report.minimumVolume, 0.0);
}

// A grading block that is uniform on both axes builds the Cartesian mesh
// bit for bit (the default path stays the default).
TEST(GradedMeshCase, UniformGradingBuildsTheCartesianMesh) {
  CaseFixture fixture;
  fixture.write("mesh.json",
                meshWithGrading(R"({"x": {"type": "uniform"}, "y": {"type": "uniform"}})"));
  const auto setup = CaseBuilder{}.build(CaseReader{}.read(fixture.directory()));
  const auto cartesian = cfd::mesh::MeshGeometry::createCartesian2D(8, 10, 1.0, 1.0);
  ASSERT_EQ(setup.mesh.numberOfFaces(), cartesian.numberOfFaces());
  for (Index c = 0; c < cartesian.numberOfCells(); ++c) {
    EXPECT_EQ(setup.mesh.cell(c).centroid().x, cartesian.cell(c).centroid().x);
    EXPECT_EQ(setup.mesh.cell(c).centroid().y, cartesian.cell(c).centroid().y);
    EXPECT_EQ(setup.mesh.cell(c).volume(), cartesian.cell(c).volume());
  }
}
