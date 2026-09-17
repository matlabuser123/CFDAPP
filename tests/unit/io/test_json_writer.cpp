// P1 -- Result Export, section 36: JSONWriter unit tests. Reads the
// generated file back with nlohmann::json (a test-only dependency here --
// JSONWriter's own public header never exposes it, per section 1's "the
// CFD numerical core must remain independent of file formats").
#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <limits>
#include <nlohmann/json.hpp>
#include <string>
#include <utility>
#include <vector>

#include "cfd/io/JSONWriter.hpp"
#include "cfd/mesh/MeshGeometry.hpp"
#include "cfd/mesh/MeshQuality.hpp"

using cfd::Vector2;
using cfd::fields::ScalarField;
using cfd::fields::VectorField;
using cfd::io::JSONWriter;
using cfd::io::RunMetadata;
using cfd::mesh::Mesh;
using cfd::mesh::MeshGeometry;
using cfd::pressure_velocity::SIMPLEResult;
using cfd::pressure_velocity::SIMPLEStatus;

namespace {

std::filesystem::path tempFile(const std::string& name) {
  return std::filesystem::temp_directory_path() / ("cfdapp_json_test_" + name);
}

nlohmann::json readJson(const std::filesystem::path& path) {
  std::ifstream in(path);
  nlohmann::json doc;
  in >> doc;
  return doc;
}

SIMPLEResult makeConvergedResultFor2x2() {
  SIMPLEResult result;
  result.velocity = VectorField(4, Vector2{0.1, 0.2});
  result.pressure = ScalarField(4, 5.0);
  result.status = SIMPLEStatus::Converged;
  result.iterations = 42;
  result.finalUResidual = 1.0e-8;
  result.finalVResidual = 2.0e-8;
  result.finalPressureResidual = 3.0e-6;
  result.finalContinuityResidual = 4.0e-9;
  result.globalMassImbalance = 1.05794e-18;
  return result;
}

RunMetadata makeMetadata() { return RunMetadata{"Test Cavity", 1.0, 0.01, "SIMPLE"}; }

}  // namespace

TEST(JSONWriterTest, WrittenFileIsValidJson) {
  const Mesh mesh = MeshGeometry::createCartesian2D(2, 2, 1.0, 1.0);
  const auto result = makeConvergedResultFor2x2();
  const auto path = tempFile("valid.json");

  JSONWriter::writeMetadata(path, makeMetadata(), mesh, result);
  EXPECT_NO_THROW(readJson(path));
}

TEST(JSONWriterTest, RequiredKeysExist) {
  const Mesh mesh = MeshGeometry::createCartesian2D(2, 2, 1.0, 1.0);
  const auto result = makeConvergedResultFor2x2();
  const auto path = tempFile("keys.json");
  JSONWriter::writeMetadata(path, makeMetadata(), mesh, result);
  const auto doc = readJson(path);

  EXPECT_TRUE(doc.contains("application"));
  EXPECT_TRUE(doc.contains("format_version"));
  ASSERT_TRUE(doc.contains("case"));
  EXPECT_TRUE(doc["case"].contains("name"));
  ASSERT_TRUE(doc.contains("mesh"));
  EXPECT_TRUE(doc["mesh"].contains("cells"));
  EXPECT_TRUE(doc["mesh"].contains("faces"));
  EXPECT_TRUE(doc["mesh"].contains("nx"));
  EXPECT_TRUE(doc["mesh"].contains("ny"));
  ASSERT_TRUE(doc.contains("physics"));
  EXPECT_TRUE(doc["physics"].contains("density"));
  EXPECT_TRUE(doc["physics"].contains("dynamic_viscosity"));
  ASSERT_TRUE(doc.contains("solver"));
  EXPECT_TRUE(doc["solver"].contains("type"));
  EXPECT_TRUE(doc["solver"].contains("converged"));
  EXPECT_TRUE(doc["solver"].contains("status"));
  EXPECT_TRUE(doc["solver"].contains("iterations"));
  ASSERT_TRUE(doc.contains("residuals"));
  EXPECT_TRUE(doc["residuals"].contains("u"));
  EXPECT_TRUE(doc["residuals"].contains("v"));
  EXPECT_TRUE(doc["residuals"].contains("p"));
  EXPECT_TRUE(doc["residuals"].contains("continuity"));
  ASSERT_TRUE(doc.contains("conservation"));
  EXPECT_TRUE(doc["conservation"].contains("global_mass_imbalance"));
  ASSERT_TRUE(doc.contains("numerics"));
  EXPECT_TRUE(doc["numerics"].contains("finite"));
}

TEST(JSONWriterTest, CaseAndMeshMetadataAreCorrect) {
  const Mesh mesh = MeshGeometry::createCartesian2D(3, 5, 1.0, 1.0);
  const auto result = makeConvergedResultFor2x2();  // field sizes unused by this assertion set.
  SIMPLEResult sizedResult = result;
  sizedResult.velocity = VectorField(15, Vector2{0.0, 0.0});
  sizedResult.pressure = ScalarField(15, 0.0);
  const auto path = tempFile("mesh_metadata.json");
  JSONWriter::writeMetadata(path, RunMetadata{"My Case", 2.0, 0.05, "SIMPLE"}, mesh, sizedResult);
  const auto doc = readJson(path);

  EXPECT_EQ(doc["case"]["name"], "My Case");
  EXPECT_EQ(doc["mesh"]["cells"], 15);
  EXPECT_EQ(doc["mesh"]["nx"], 3);
  EXPECT_EQ(doc["mesh"]["ny"], 5);
  EXPECT_DOUBLE_EQ(doc["physics"]["density"].get<double>(), 2.0);
  EXPECT_DOUBLE_EQ(doc["physics"]["dynamic_viscosity"].get<double>(), 0.05);
}

TEST(JSONWriterTest, SolverStatusAndIterationsAreCorrect) {
  const Mesh mesh = MeshGeometry::createCartesian2D(2, 2, 1.0, 1.0);
  auto result = makeConvergedResultFor2x2();
  result.status = SIMPLEStatus::MaxIterations;
  result.iterations = 999;
  const auto path = tempFile("status.json");
  JSONWriter::writeMetadata(path, makeMetadata(), mesh, result);
  const auto doc = readJson(path);

  EXPECT_EQ(doc["solver"]["status"], "MaxIterations");
  EXPECT_FALSE(doc["solver"]["converged"].get<bool>());
  EXPECT_EQ(doc["solver"]["iterations"], 999);
}

TEST(JSONWriterTest, EveryStatusMapsToItsOwnEnumName) {
  const Mesh mesh = MeshGeometry::createCartesian2D(2, 2, 1.0, 1.0);
  const std::vector<std::pair<SIMPLEStatus, std::string>> cases = {
      {SIMPLEStatus::Converged, "Converged"},
      {SIMPLEStatus::MaxIterations, "MaxIterations"},
      {SIMPLEStatus::MomentumFailure, "MomentumFailure"},
      {SIMPLEStatus::PressureCorrectionFailure, "PressureCorrectionFailure"},
      {SIMPLEStatus::NonFiniteState, "NonFiniteState"},
      {SIMPLEStatus::InvalidConfiguration, "InvalidConfiguration"},
  };
  for (const auto& [status, expectedName] : cases) {
    auto result = makeConvergedResultFor2x2();
    result.status = status;
    const auto path = tempFile("status_" + expectedName + ".json");
    JSONWriter::writeMetadata(path, makeMetadata(), mesh, result);
    EXPECT_EQ(readJson(path)["solver"]["status"], expectedName);
  }
}

TEST(JSONWriterTest, FinalResidualsAndMassImbalanceAreCorrect) {
  const Mesh mesh = MeshGeometry::createCartesian2D(2, 2, 1.0, 1.0);
  const auto result = makeConvergedResultFor2x2();
  const auto path = tempFile("residuals.json");
  JSONWriter::writeMetadata(path, makeMetadata(), mesh, result);
  const auto doc = readJson(path);

  EXPECT_DOUBLE_EQ(doc["residuals"]["u"].get<double>(), result.finalUResidual);
  EXPECT_DOUBLE_EQ(doc["residuals"]["v"].get<double>(), result.finalVResidual);
  EXPECT_DOUBLE_EQ(doc["residuals"]["p"].get<double>(), result.finalPressureResidual);
  EXPECT_DOUBLE_EQ(doc["residuals"]["continuity"].get<double>(), result.finalContinuityResidual);
  EXPECT_DOUBLE_EQ(doc["conservation"]["global_mass_imbalance"].get<double>(),
                   result.globalMassImbalance);
  // Numbers, not strings (section 16).
  EXPECT_TRUE(doc["residuals"]["u"].is_number());
  EXPECT_TRUE(doc["conservation"]["global_mass_imbalance"].is_number());
}

TEST(JSONWriterTest, FiniteFlagReflectsActualFieldFiniteness) {
  const Mesh mesh = MeshGeometry::createCartesian2D(2, 2, 1.0, 1.0);
  auto finiteResult = makeConvergedResultFor2x2();
  auto nonFiniteResult = makeConvergedResultFor2x2();
  nonFiniteResult.pressure[0] = std::numeric_limits<cfd::Real>::quiet_NaN();

  const auto finitePath = tempFile("finite.json");
  const auto nonFinitePath = tempFile("nonfinite.json");
  JSONWriter::writeMetadata(finitePath, makeMetadata(), mesh, finiteResult);
  JSONWriter::writeMetadata(nonFinitePath, makeMetadata(), mesh, nonFiniteResult);

  EXPECT_TRUE(readJson(finitePath)["numerics"]["finite"].get<bool>());
  EXPECT_FALSE(readJson(nonFinitePath)["numerics"]["finite"].get<bool>());
}

TEST(JSONWriterTest, FormatVersionIsOne) {
  const Mesh mesh = MeshGeometry::createCartesian2D(2, 2, 1.0, 1.0);
  const auto result = makeConvergedResultFor2x2();
  const auto path = tempFile("format_version.json");
  JSONWriter::writeMetadata(path, makeMetadata(), mesh, result);
  EXPECT_EQ(readJson(path)["format_version"], 1);
}

// --- P2-THERMAL-004: optional "thermal" section ----------------------------

TEST(JSONWriterTest, ThermalEnabledIsFalseByDefault) {
  const Mesh mesh = MeshGeometry::createCartesian2D(2, 2, 1.0, 1.0);
  const auto result = makeConvergedResultFor2x2();
  const auto path = tempFile("thermal_absent.json");
  JSONWriter::writeMetadata(path, makeMetadata(), mesh, result);
  const auto doc = readJson(path);

  ASSERT_TRUE(doc.contains("thermal"));
  EXPECT_FALSE(doc["thermal"]["enabled"].get<bool>());
  EXPECT_FALSE(doc["thermal"].contains("conductivity"));
}

TEST(JSONWriterTest, WritesThermalMetadataWhenProvided) {
  const Mesh mesh = MeshGeometry::createCartesian2D(2, 2, 1.0, 1.0);
  const auto result = makeConvergedResultFor2x2();
  const auto path = tempFile("thermal_present.json");
  const cfd::io::ThermalRunMetadata thermal{
      /*conductivity=*/0.6, /*specificHeat=*/4180.0, /*status=*/"Converged",
      /*converged=*/true,   /*iterations=*/57,       /*finalResidual=*/1.2e-9};
  JSONWriter::writeMetadata(path, makeMetadata(), mesh, result, thermal);
  const auto doc = readJson(path);

  EXPECT_TRUE(doc["thermal"]["enabled"].get<bool>());
  EXPECT_DOUBLE_EQ(doc["thermal"]["conductivity"].get<double>(), 0.6);
  EXPECT_DOUBLE_EQ(doc["thermal"]["specific_heat"].get<double>(), 4180.0);
  EXPECT_EQ(doc["thermal"]["status"], "Converged");
  EXPECT_TRUE(doc["thermal"]["converged"].get<bool>());
  EXPECT_EQ(doc["thermal"]["iterations"], 57);
  EXPECT_DOUBLE_EQ(doc["thermal"]["final_residual"].get<double>(), 1.2e-9);
}

// --- P6-PHYS-001: "species" array ------------------------------------------

TEST(JSONWriterTest, SpeciesArrayIsEmptyByDefault) {
  const Mesh mesh = MeshGeometry::createCartesian2D(2, 2, 1.0, 1.0);
  const auto result = makeConvergedResultFor2x2();
  const auto path = tempFile("species_absent.json");
  JSONWriter::writeMetadata(path, makeMetadata(), mesh, result);
  const auto doc = readJson(path);

  ASSERT_TRUE(doc.contains("species"));
  EXPECT_TRUE(doc["species"].is_array());
  EXPECT_TRUE(doc["species"].empty());
}

TEST(JSONWriterTest, WritesSpeciesMetadataInOrderWhenProvided) {
  const Mesh mesh = MeshGeometry::createCartesian2D(2, 2, 1.0, 1.0);
  const auto result = makeConvergedResultFor2x2();
  const auto path = tempFile("species_present.json");
  const std::vector<cfd::io::SpeciesRunMetadata> species{
      {"CO2", 1.6e-5, "Converged", true, 12, 3.4e-9},
      {"O2", 2.0e-5, "MaxIterations", false, 2000, 5.0e-3},
  };
  JSONWriter::writeMetadata(path, makeMetadata(), mesh, result, std::nullopt, species);
  const auto doc = readJson(path);

  ASSERT_EQ(doc["species"].size(), 2u);
  EXPECT_EQ(doc["species"][0]["name"], "CO2");
  EXPECT_DOUBLE_EQ(doc["species"][0]["diffusivity"].get<double>(), 1.6e-5);
  EXPECT_EQ(doc["species"][0]["status"], "Converged");
  EXPECT_TRUE(doc["species"][0]["converged"].get<bool>());
  EXPECT_EQ(doc["species"][0]["iterations"], 12);
  EXPECT_DOUBLE_EQ(doc["species"][0]["final_residual"].get<double>(), 3.4e-9);
  EXPECT_EQ(doc["species"][1]["name"], "O2");
  EXPECT_EQ(doc["species"][1]["status"], "MaxIterations");
  EXPECT_FALSE(doc["species"][1]["converged"].get<bool>());
}

TEST(JSONWriterTest, RepeatedWriteIsByteIdentical) {
  const Mesh mesh = MeshGeometry::createCartesian2D(2, 2, 1.0, 1.0);
  const auto result = makeConvergedResultFor2x2();
  const auto pathA = tempFile("repeat_a.json");
  const auto pathB = tempFile("repeat_b.json");
  JSONWriter::writeMetadata(pathA, makeMetadata(), mesh, result);
  JSONWriter::writeMetadata(pathB, makeMetadata(), mesh, result);

  std::ifstream inA(pathA);
  std::ifstream inB(pathB);
  const std::string contentA((std::istreambuf_iterator<char>(inA)),
                             std::istreambuf_iterator<char>());
  const std::string contentB((std::istreambuf_iterator<char>(inB)),
                             std::istreambuf_iterator<char>());
  EXPECT_EQ(contentA, contentB);
}

// P12-NUM-004: the two new statuses keep their own names, and the additive
// "robustness" diagnostics object carries the result's own values.
TEST(JSONWriterTest, RobustnessStatusesAndDiagnosticsAreExported) {
  const Mesh mesh = MeshGeometry::createCartesian2D(2, 2, 1.0, 1.0);
  for (const auto& [status, name] : std::vector<std::pair<SIMPLEStatus, std::string>>{
           {SIMPLEStatus::Stagnated, "Stagnated"}, {SIMPLEStatus::Diverging, "Diverging"}}) {
    auto result = makeConvergedResultFor2x2();
    result.status = status;
    const auto path = tempFile("status_" + name + ".json");
    JSONWriter::writeMetadata(path, makeMetadata(), mesh, result);
    EXPECT_EQ(readJson(path)["solver"]["status"], name);
  }
  auto result = makeConvergedResultFor2x2();
  result.robustness.convergenceCriterion = cfd::solver::ConvergenceCriterion::Normalized;
  result.robustness.statusDetail = "stagnation: example";
  result.robustness.uNormalizedHistory = {1.0, 0.25};
  result.robustness.vNormalizedHistory = {1.0, 0.5};
  result.robustness.pressureNormalizedHistory = {1.0, 0.125};
  result.robustness.continuityNormalizedHistory = {1.0, 2.0};
  result.robustness.velocityRelaxationHistory = {0.7, 0.49};
  result.robustness.pressureRelaxationHistory = {0.3, 0.21};
  result.robustness.relaxationDecreases = 1;
  result.robustness.linearSolverFallbacks = 3;
  result.robustness.linearSolverFallbackRecoveries = 2;
  const auto path = tempFile("robustness.json");
  JSONWriter::writeMetadata(path, makeMetadata(), mesh, result);
  const auto r = readJson(path)["robustness"];
  EXPECT_EQ(r["convergence_criterion"], "normalized");
  EXPECT_EQ(r["status_detail"], "stagnation: example");
  EXPECT_DOUBLE_EQ(r["normalized_residuals"]["u"].get<double>(), 0.25);
  EXPECT_DOUBLE_EQ(r["normalized_residuals"]["v"].get<double>(), 0.5);
  EXPECT_DOUBLE_EQ(r["normalized_residuals"]["p"].get<double>(), 0.125);
  EXPECT_DOUBLE_EQ(r["normalized_residuals"]["continuity"].get<double>(), 2.0);
  EXPECT_DOUBLE_EQ(r["final_velocity_relaxation"].get<double>(), 0.49);
  EXPECT_DOUBLE_EQ(r["final_pressure_relaxation"].get<double>(), 0.21);
  EXPECT_EQ(r["relaxation_decreases"], 1);
  EXPECT_EQ(r["linear_solver_fallbacks"], 3);
  EXPECT_EQ(r["linear_solver_fallback_recoveries"], 2);
}

// --- P12-MESH-004: "mesh_quality" -----------------------------------------------

TEST(JSONWriterTest, MeshQualityIsAbsentWhenNotGiven) {
  const Mesh mesh = MeshGeometry::createCartesian2D(2, 2, 1.0, 1.0);
  const auto path = tempFile("no_mesh_quality.json");
  JSONWriter::writeMetadata(path, makeMetadata(), mesh, makeConvergedResultFor2x2());
  EXPECT_FALSE(readJson(path).contains("mesh_quality"));
}

TEST(JSONWriterTest, MeshQualityIsExportedWithEveryMetricAndIssue) {
  // 1 x 4 cells of 1 x 0.25: aspect ratio 4 everywhere, above a warning
  // threshold of 3 -> one aggregated warning.
  const Mesh mesh = MeshGeometry::createCartesian2D(1, 4, 1.0, 1.0);
  cfd::mesh::MeshQualityThresholds thresholds;
  thresholds.aspectRatioWarning = 3.0;
  const auto report = cfd::mesh::MeshQuality::evaluate(mesh, thresholds);
  const auto path = tempFile("mesh_quality.json");
  JSONWriter::writeMetadata(path, makeMetadata(), mesh, makeConvergedResultFor2x2(), std::nullopt,
                            {}, std::nullopt, std::nullopt, report);
  const auto q = readJson(path)["mesh_quality"];
  EXPECT_EQ(q["status"], "valid_with_warnings");
  EXPECT_EQ(q["cells"], 4);
  EXPECT_EQ(q["faces"], 13);
  EXPECT_EQ(q["internal_faces"], 3);
  EXPECT_EQ(q["boundary_faces"], 10);
  for (const char* metric : {"cell_area", "face_length", "aspect_ratio", "non_orthogonality_deg",
                             "skewness", "expansion_ratio"}) {
    for (const char* key : {"count", "min", "max", "mean", "rms", "worst_id", "worst_location",
                            "above_warning", "warning_threshold"}) {
      EXPECT_TRUE(q[metric].contains(key)) << metric << "." << key;
    }
  }
  EXPECT_DOUBLE_EQ(q["aspect_ratio"]["max"].get<double>(), 4.0);
  EXPECT_EQ(q["aspect_ratio"]["above_warning"], 4);
  EXPECT_DOUBLE_EQ(q["aspect_ratio"]["warning_threshold"].get<double>(), 3.0);
  EXPECT_EQ(q["aspect_ratio"]["worst_id"], 0);
  EXPECT_DOUBLE_EQ(q["aspect_ratio"]["worst_location"][0].get<double>(), 0.5);
  EXPECT_DOUBLE_EQ(q["aspect_ratio"]["worst_location"][1].get<double>(), 0.125);
  EXPECT_TRUE(q["cell_area"]["warning_threshold"].is_null());  // no warning for areas
  EXPECT_DOUBLE_EQ(q["non_orthogonality_deg"]["warning_threshold"].get<double>(), 70.0);
  EXPECT_EQ(q["degenerate_cells"], 0);
  EXPECT_EQ(q["invalid_faces"], 0);
  EXPECT_EQ(q["connected_components"], 1);
  ASSERT_EQ(q["issues"].size(), 1u);
  const auto issue = q["issues"][0];
  EXPECT_EQ(issue["severity"], "warning");
  EXPECT_EQ(issue["metric"], "aspect_ratio");
  EXPECT_EQ(issue["entity"], "cell");
  EXPECT_EQ(issue["id"], 0);
  EXPECT_DOUBLE_EQ(issue["value"].get<double>(), 4.0);
  EXPECT_DOUBLE_EQ(issue["threshold"].get<double>(), 3.0);
  EXPECT_EQ(issue["count"], 4);
  EXPECT_EQ(issue["message"].get<std::string>(), report.issues[0].message);
}

TEST(JSONWriterTest, MeshQualityNonFiniteAndUnavailableValuesAreNull) {
  const Mesh mesh = MeshGeometry::createCartesian2D(2, 2, 1.0, 1.0);
  cfd::mesh::MeshQualityReport report;  // status invalid, every metric count 0
  report.cellArea.count = 1;
  report.cellArea.minimum = std::numeric_limits<double>::quiet_NaN();
  report.cellArea.maximum = std::numeric_limits<double>::infinity();
  report.issues.push_back(
      cfd::mesh::MeshQualityIssue{cfd::mesh::MeshQualitySeverity::Fatal, "cell_area", "cell", 3,
                                  std::nullopt, std::numeric_limits<double>::quiet_NaN(), 0.0, 1,
                                  "cell 3 has non-positive or non-finite area"});
  const auto path = tempFile("mesh_quality_null.json");
  JSONWriter::writeMetadata(path, makeMetadata(), mesh, makeConvergedResultFor2x2(), std::nullopt,
                            {}, std::nullopt, std::nullopt, report);
  const auto q = readJson(path)["mesh_quality"];  // parses: no NaN / Infinity tokens
  EXPECT_EQ(q["status"], "invalid");
  EXPECT_TRUE(q["cell_area"]["min"].is_null());
  EXPECT_TRUE(q["cell_area"]["max"].is_null());
  EXPECT_EQ(q["skewness"]["count"], 0);
  for (const char* key : {"min", "max", "mean", "rms", "worst_id", "worst_location"}) {
    EXPECT_TRUE(q["skewness"][key].is_null()) << key;
  }
  EXPECT_EQ(q["issues"][0]["severity"], "fatal");
  EXPECT_TRUE(q["issues"][0]["value"].is_null());
  EXPECT_TRUE(q["issues"][0]["location"].is_null());
  EXPECT_EQ(q["issues"][0]["id"], 3);
}
