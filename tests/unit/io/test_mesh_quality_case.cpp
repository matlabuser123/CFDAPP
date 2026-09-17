// P12-MESH-004 -- the production mesh-quality gate in CaseBuilder: the
// report is stored in SimulationSetup (the one evaluation every consumer
// reads), warnings never reject a case, and an invalid mesh is a case error
// that carries the summary line and every fatal issue, formatted.

#include <gtest/gtest.h>

#include <sstream>
#include <string>

#include "CaseFixture.hpp"
#include "cfd/core/Exception.hpp"
#include "cfd/io/CaseBuilder.hpp"
#include "cfd/io/CaseReader.hpp"
#include "cfd/mesh/MeshQuality.hpp"

using cfd::CaseConfigurationError;
using cfd::io::CaseBuilder;
using cfd::io::CaseReader;
using cfd::mesh::MeshQualitySeverity;
using cfd::mesh::MeshQualityStatus;
using cfd::testutil::CaseFixture;

namespace {

std::string buildError(const CaseFixture& fixture) {
  try {
    (void)CaseBuilder{}.build(CaseReader{}.read(fixture.directory()));
  } catch (const CaseConfigurationError& e) {
    return e.what();
  }
  return "no error";
}

// A 4 x 4 structured_quad on the unit square whose centre vertex is moved
// to (0.6, 0.55): valid, non-orthogonal beyond the 10-degree information
// level.
std::string distortedQuadMesh() {
  std::ostringstream json;
  json << R"({"type": "structured_quad", "nx": 4, "ny": 4, "vertices": [)";
  for (int j = 0; j <= 4; ++j) {
    for (int i = 0; i <= 4; ++i) {
      const bool centre = i == 2 && j == 2;
      json << (i + j > 0 ? ", " : "") << "[" << (centre ? 0.6 : i / 4.0) << ", "
           << (centre ? 0.55 : j / 4.0) << "]";
    }
  }
  json << "]}";
  return json.str();
}

}  // namespace

TEST(MeshQualityCase, BuilderStoresTheProductionReportInTheSetup) {
  CaseFixture fixture;
  const auto setup = CaseBuilder{}.build(CaseReader{}.read(fixture.directory()));
  EXPECT_EQ(setup.meshQuality.status, MeshQualityStatus::Valid);
  EXPECT_EQ(setup.meshQuality.cellCount, 16u);
  EXPECT_EQ(setup.meshQuality.summaryLine(),
            cfd::mesh::MeshQuality::evaluate(setup.mesh).summaryLine());
  EXPECT_TRUE(setup.meshQuality.issues.empty());
}

TEST(MeshQualityCase, NonOrthogonalMeshCarriesTheCorrectionAdvice) {
  CaseFixture fixture;
  fixture.write("mesh.json", distortedQuadMesh());
  const auto setup = CaseBuilder{}.build(CaseReader{}.read(fixture.directory()));
  EXPECT_EQ(setup.meshQuality.status, MeshQualityStatus::Valid);
  EXPECT_GT(setup.meshQuality.nonOrthogonality.maximum, 10.0);
  bool advice = false;
  for (const auto& issue : setup.meshQuality.issues) {
    advice = advice || (issue.severity == MeshQualitySeverity::Info &&
                        issue.message.find("non_orthogonal_corrections") != std::string::npos);
  }
  EXPECT_TRUE(advice);
}

TEST(MeshQualityCase, WarningMeshIsAcceptedNotRejected) {
  CaseFixture fixture;
  fixture.write("mesh.json", R"({"type": "structured_cartesian", "nx": 4, "ny": 4,
    "grading": {"y": {"type": "geometric", "ratio": 2.5, "cluster": "both"}}})");
  const auto setup = CaseBuilder{}.build(CaseReader{}.read(fixture.directory()));
  EXPECT_EQ(setup.meshQuality.status, MeshQualityStatus::ValidWithWarnings);
  EXPECT_TRUE(setup.meshQuality.valid);
  EXPECT_NEAR(setup.meshQuality.expansionRatio.maximum, 2.5, 1e-12);
}

TEST(MeshQualityCase, InvalidMeshErrorCarriesTheSummaryAndEveryFatalIssue) {
  CaseFixture fixture;
  const auto block = [](const char* name, double x0) {
    std::ostringstream json;
    json << R"({"name": ")" << name << R"(", "nx": 4, "ny": 4, "vertices": [)";
    for (int j = 0; j <= 4; ++j) {
      for (int i = 0; i <= 4; ++i) {
        json << (i + j > 0 ? ", " : "") << "[" << x0 + (0.5 * i / 4.0) << ", " << j / 4.0 << "]";
      }
    }
    json << "]}";
    return json.str();
  };
  std::string sides;
  for (const char* b : {"a", "b"}) {
    for (const char* s : {"left", "right", "bottom", "top"}) {
      sides += std::string(sides.empty() ? "" : ", ") + R"({"block": ")" + b + R"(", "side": ")" +
               s + R"("})";
    }
  }
  fixture.write("mesh.json", R"({"type": "multiblock", "blocks": [)" + block("a", 0.0) + ", " +
                                 block("b", 1.5) + R"(], "interfaces": [], "patches": [)" +
                                 R"({"name": "walls", "sides": [)" + sides + "]}]}");
  fixture.write("geometry.json", R"({"type": "mesh_defined"})");
  fixture.write("boundaries.json", R"({"patches": {"walls": {"velocity": {"type": "wall"},
    "pressure": {"type": "fixed_gradient", "value": 0.0}}}})");
  const std::string e = buildError(fixture);
  EXPECT_NE(e.find("mesh.json: the multiblock mesh is invalid (invalid: 32 cells, cell area "),
            std::string::npos)
      << e;
  EXPECT_NE(e.find("\n  fatal connected_components: mesh has 2 disconnected cell regions (cells "
                   "per region: 16, 16); the fluid domain must be one connected region [value 2; "
                   "threshold 1]"),
            std::string::npos)
      << e;
}
