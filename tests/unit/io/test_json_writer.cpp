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
      /*conductivity=*/0.6,   /*specificHeat=*/4180.0, /*status=*/"Converged",
      /*converged=*/true,     /*iterations=*/57,       /*finalResidual=*/1.2e-9};
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
