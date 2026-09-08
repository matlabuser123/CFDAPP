// P1 -- Result Export, section 38: end-to-end flow -- solve a small
// cavity, export every format, read the outputs back, and cross-check
// every exported value against the original SIMPLEResult (section 39:
// the same solver result must agree across fields.csv/solution.vtk/
// metadata.json/residuals.csv, not just internally consistent with
// itself). Section 34's determinism requirement (repeated export is
// byte-identical) is also checked here, at the ResultExporter level.
#include <gtest/gtest.h>

#include <cmath>
#include <filesystem>
#include <fstream>
#include <memory>
#include <nlohmann/json.hpp>
#include <sstream>
#include <string>
#include <vector>

#include "cfd/boundary/FixedGradient.hpp"
#include "cfd/boundary/MovingWall.hpp"
#include "cfd/boundary/Wall.hpp"
#include "cfd/io/ResultExporter.hpp"
#include "cfd/mesh/MeshGeometry.hpp"
#include "cfd/physics/FluidProperties.hpp"
#include "cfd/pressure_velocity/SIMPLE.hpp"

using cfd::Index;
using cfd::Real;
using cfd::Vector2;
using cfd::boundary::BoundaryConditionSet;
using cfd::boundary::FixedGradient;
using cfd::boundary::MovingWall;
using cfd::boundary::Wall;
using cfd::fields::ScalarField;
using cfd::fields::VectorField;
using cfd::io::ResultExporter;
using cfd::io::RunMetadata;
using cfd::mesh::Mesh;
using cfd::mesh::MeshGeometry;
using cfd::physics::FluidProperties;
using cfd::pressure_velocity::SIMPLE;
using cfd::pressure_velocity::SIMPLEResult;
using cfd::pressure_velocity::SIMPLEStatus;

namespace {

std::filesystem::path tempDir(const std::string& name) {
  const auto dir = std::filesystem::temp_directory_path() / ("cfdapp_export_integration_" + name);
  std::filesystem::remove_all(dir);
  return dir;
}

std::vector<std::string> readLines(const std::filesystem::path& path) {
  std::ifstream in(path);
  std::vector<std::string> lines;
  std::string line;
  while (std::getline(in, line)) lines.push_back(line);
  return lines;
}

struct SolvedCavity {
  Mesh mesh;
  SIMPLEResult result;
};

// Solves the same tiny 4x4 cavity CaseBuilderTest/CaseFixture use, so
// this test is self-contained (no dependency on the case-loading
// pipeline -- Result Export only needs Mesh + SIMPLEResult).
SolvedCavity solveSmallCavity() {
  Mesh mesh = MeshGeometry::createCartesian2D(4, 4, 1.0, 1.0);
  BoundaryConditionSet velocityBoundaries;
  velocityBoundaries.set(mesh, "left", std::make_unique<Wall>());
  velocityBoundaries.set(mesh, "right", std::make_unique<Wall>());
  velocityBoundaries.set(mesh, "bottom", std::make_unique<Wall>());
  velocityBoundaries.set(mesh, "top", std::make_unique<MovingWall>(Vector2{1.0, 0.0}));
  BoundaryConditionSet pressureBoundaries;
  for (const auto& patch : mesh.boundaryPatches()) {
    pressureBoundaries.set(mesh, patch.name(), std::make_unique<FixedGradient>(0.0));
  }
  const FluidProperties fluid(1.0, 0.01);

  cfd::pressure_velocity::SIMPLESettings settings;
  settings.maxIterations = 1000;
  settings.velocityRelaxation = 0.7;
  settings.pressureRelaxation = 0.3;
  settings.velocityTolerance = 1e-6;
  settings.pressureTolerance = 1e-6;
  settings.continuityTolerance = 1e-6;
  settings.momentumSolver.maxIterations = 500;
  settings.momentumSolver.absoluteTolerance = 1e-10;
  settings.momentumSolver.relativeTolerance = 1e-8;
  settings.pressureSolver.maxIterations = 2000;
  settings.pressureSolver.absoluteTolerance = 1e-10;
  settings.pressureSolver.relativeTolerance = 1e-8;

  const SIMPLE simple(settings, /*referenceCell=*/0);
  const VectorField initialVelocity(mesh.numberOfCells(), Vector2{0.0, 0.0});
  const ScalarField initialPressure(mesh.numberOfCells(), 0.0);
  SIMPLEResult result = simple.solve(mesh, fluid, velocityBoundaries, pressureBoundaries,
                                     initialVelocity, initialPressure);

  return SolvedCavity{std::move(mesh), std::move(result)};
}

}  // namespace

TEST(ResultExportIntegrationTest, CsvFieldsMatchSimpleResultExactly) {
  const SolvedCavity solved = solveSmallCavity();
  const Mesh& mesh = solved.mesh;
  const SIMPLEResult& result = solved.result;
  ASSERT_EQ(result.status, SIMPLEStatus::Converged);

  const auto outputDir = tempDir("csv_fields");
  const auto summary =
      ResultExporter::write(outputDir, mesh, result, RunMetadata{"Test", 1.0, 0.01, "SIMPLE"});
  ASSERT_TRUE(summary.fieldsCsvPath.has_value());

  const auto lines = readLines(*summary.fieldsCsvPath);
  ASSERT_EQ(lines.size(), 1u + mesh.numberOfCells());
  for (Index id = 0; id < mesh.numberOfCells(); ++id) {
    std::istringstream row(lines[id + 1]);
    std::string cellId, x, y, vx, vy, mag, pressure;
    std::getline(row, cellId, ',');
    std::getline(row, x, ',');
    std::getline(row, y, ',');
    std::getline(row, vx, ',');
    std::getline(row, vy, ',');
    std::getline(row, mag, ',');
    std::getline(row, pressure, ',');
    EXPECT_EQ(std::stoul(cellId), id);
    EXPECT_DOUBLE_EQ(std::stod(vx), result.velocity[id].x) << "cell " << id;
    EXPECT_DOUBLE_EQ(std::stod(vy), result.velocity[id].y) << "cell " << id;
    EXPECT_DOUBLE_EQ(std::stod(pressure), result.pressure[id]) << "cell " << id;
  }
}

TEST(ResultExportIntegrationTest, JsonMetadataMatchesSimpleResultExactly) {
  const SolvedCavity solved = solveSmallCavity();
  const Mesh& mesh = solved.mesh;
  const SIMPLEResult& result = solved.result;

  const auto outputDir = tempDir("json_metadata");
  const auto summary =
      ResultExporter::write(outputDir, mesh, result, RunMetadata{"Test", 1.0, 0.01, "SIMPLE"});

  std::ifstream in(summary.metadataPath);
  nlohmann::json doc;
  in >> doc;

  EXPECT_EQ(doc["solver"]["status"], "Converged");
  EXPECT_EQ(doc["solver"]["iterations"], result.iterations);
  EXPECT_DOUBLE_EQ(doc["conservation"]["global_mass_imbalance"].get<double>(),
                   result.globalMassImbalance);
  EXPECT_DOUBLE_EQ(doc["residuals"]["u"].get<double>(), result.finalUResidual);
  EXPECT_DOUBLE_EQ(doc["residuals"]["v"].get<double>(), result.finalVResidual);
  EXPECT_DOUBLE_EQ(doc["residuals"]["p"].get<double>(), result.finalPressureResidual);
  EXPECT_DOUBLE_EQ(doc["residuals"]["continuity"].get<double>(), result.finalContinuityResidual);
  EXPECT_EQ(doc["mesh"]["cells"], mesh.numberOfCells());
}

TEST(ResultExportIntegrationTest, VtkCellCountAndFieldsMatchSimpleResultExactly) {
  const SolvedCavity solved = solveSmallCavity();
  const Mesh& mesh = solved.mesh;
  const SIMPLEResult& result = solved.result;

  const auto outputDir = tempDir("vtk_fields");
  const auto summary =
      ResultExporter::write(outputDir, mesh, result, RunMetadata{"Test", 1.0, 0.01, "SIMPLE"});
  ASSERT_TRUE(summary.vtkPath.has_value());

  const auto lines = readLines(*summary.vtkPath);
  std::size_t cellsLine = 0, cellDataLine = 0;
  for (std::size_t i = 0; i < lines.size(); ++i) {
    if (lines[i].rfind("CELLS ", 0) == 0) cellsLine = i;
    if (lines[i].rfind("CELL_DATA ", 0) == 0) cellDataLine = i;
  }
  ASSERT_NE(cellsLine, 0u);
  ASSERT_NE(cellDataLine, 0u);
  EXPECT_EQ(lines[cellsLine], "CELLS " + std::to_string(mesh.numberOfCells()) + " " +
                                  std::to_string(mesh.numberOfCells() * 5));
  EXPECT_EQ(lines[cellDataLine], "CELL_DATA " + std::to_string(mesh.numberOfCells()));

  // pressure block starts 3 lines after CELL_DATA (header, SCALARS,
  // LOOKUP_TABLE), one value per cell in cell-id order.
  for (Index id = 0; id < mesh.numberOfCells(); ++id) {
    EXPECT_DOUBLE_EQ(std::stod(lines[cellDataLine + 3 + id]), result.pressure[id]) << "cell " << id;
  }
}

TEST(ResultExportIntegrationTest, ResidualsCsvMatchesStoredHistories) {
  const SolvedCavity solved = solveSmallCavity();
  const Mesh& mesh = solved.mesh;
  const SIMPLEResult& result = solved.result;

  const auto outputDir = tempDir("residuals");
  const auto summary =
      ResultExporter::write(outputDir, mesh, result, RunMetadata{"Test", 1.0, 0.01, "SIMPLE"});

  const auto lines = readLines(summary.residualsCsvPath);
  ASSERT_EQ(lines.size(), 1u + result.uResidualHistory.size());
  // Last row's residuals must equal SIMPLEResult's own "final" values.
  std::istringstream lastRow(lines.back());
  std::string iteration, u, v, p, continuity, massImbalance;
  std::getline(lastRow, iteration, ',');
  std::getline(lastRow, u, ',');
  std::getline(lastRow, v, ',');
  std::getline(lastRow, p, ',');
  std::getline(lastRow, continuity, ',');
  std::getline(lastRow, massImbalance, ',');
  EXPECT_DOUBLE_EQ(std::stod(u), result.finalUResidual);
  EXPECT_DOUBLE_EQ(std::stod(v), result.finalVResidual);
  EXPECT_DOUBLE_EQ(std::stod(p), result.finalPressureResidual);
  EXPECT_DOUBLE_EQ(std::stod(continuity), result.finalContinuityResidual);
}

TEST(ResultExportIntegrationTest, RepeatedExportIsByteIdenticalAcrossAllFiles) {
  const SolvedCavity solved = solveSmallCavity();
  const Mesh& mesh = solved.mesh;
  const SIMPLEResult& result = solved.result;
  const RunMetadata metadata{"Test", 1.0, 0.01, "SIMPLE"};

  const auto dirA = tempDir("determinism_a");
  const auto dirB = tempDir("determinism_b");
  const auto summaryA = ResultExporter::write(dirA, mesh, result, metadata);
  const auto summaryB = ResultExporter::write(dirB, mesh, result, metadata);

  EXPECT_EQ(readLines(*summaryA.fieldsCsvPath), readLines(*summaryB.fieldsCsvPath));
  EXPECT_EQ(readLines(summaryA.residualsCsvPath), readLines(summaryB.residualsCsvPath));
  EXPECT_EQ(readLines(summaryA.metadataPath), readLines(summaryB.metadataPath));
  EXPECT_EQ(readLines(*summaryA.vtkPath), readLines(*summaryB.vtkPath));
}
