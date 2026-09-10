// P1 -- Result Export, section 35: CSVWriter unit tests.
#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <limits>
#include <sstream>
#include <vector>

#include "cfd/core/Exception.hpp"
#include "cfd/io/CSVWriter.hpp"
#include "cfd/mesh/MeshGeometry.hpp"

using cfd::Index;
using cfd::InvalidArgumentError;
using cfd::NumericalError;
using cfd::Vector2;
using cfd::fields::ScalarField;
using cfd::fields::VectorField;
using cfd::io::CSVWriter;
using cfd::mesh::Mesh;
using cfd::mesh::MeshGeometry;
using cfd::pressure_velocity::SIMPLEResult;

namespace {

// Unique-per-test-process temp file path -- avoids clobbering a real
// results/ directory and avoids collisions between tests run in
// parallel.
std::filesystem::path tempFile(const std::string& name) {
  return std::filesystem::temp_directory_path() / ("cfdapp_csv_test_" + name);
}

std::vector<std::string> readLines(const std::filesystem::path& path) {
  std::ifstream in(path);
  std::vector<std::string> lines;
  std::string line;
  while (std::getline(in, line)) lines.push_back(line);
  return lines;
}

SIMPLEResult makeResultFor2x2(bool includeHistory) {
  SIMPLEResult result;
  result.velocity = VectorField(4);
  result.velocity[0] = Vector2{1.0, 2.0};
  result.velocity[1] = Vector2{3.0, 4.0};
  result.velocity[2] = Vector2{-1.0, 0.0};
  result.velocity[3] = Vector2{0.0, 0.0};
  result.pressure = ScalarField(4);
  result.pressure[0] = 10.0;
  result.pressure[1] = 20.0;
  result.pressure[2] = 30.0;
  result.pressure[3] = 40.0;
  result.globalMassImbalance = 1.5e-9;
  if (includeHistory) {
    result.uResidualHistory = {0.5, 0.1, 0.01};
    result.vResidualHistory = {0.4, 0.09, 0.009};
    result.pressureResidualHistory = {0.3, 0.08, 0.008};
    result.continuityHistory = {0.2, 0.07, 0.007};
  }
  return result;
}

}  // namespace

TEST(CSVWriterTest, WriteFieldsHeaderIsExact) {
  const Mesh mesh = MeshGeometry::createCartesian2D(2, 2, 1.0, 1.0);
  const auto result = makeResultFor2x2(false);
  const auto path = tempFile("fields_header.csv");

  CSVWriter::writeFields(path, mesh, result);
  const auto lines = readLines(path);
  ASSERT_FALSE(lines.empty());
  EXPECT_EQ(lines.front(), "cell_id,x,y,velocity_x,velocity_y,velocity_magnitude,pressure");
  EXPECT_EQ(lines.size(), 1u + mesh.numberOfCells());  // header + one row per cell.
}

TEST(CSVWriterTest, WriteFieldsRowCountAndCellOrderMatchMesh) {
  const Mesh mesh = MeshGeometry::createCartesian2D(2, 2, 1.0, 1.0);
  const auto result = makeResultFor2x2(false);
  const auto path = tempFile("fields_order.csv");

  CSVWriter::writeFields(path, mesh, result);
  const auto lines = readLines(path);
  // Row k+1 (1-indexed after the header) must be cell k -- section 5:
  // deterministic cell-id order, never container/filesystem order.
  for (Index id = 0; id < mesh.numberOfCells(); ++id) {
    std::istringstream row(lines[id + 1]);
    std::string cellIdField;
    std::getline(row, cellIdField, ',');
    EXPECT_EQ(cellIdField, std::to_string(id)) << "row " << id;
  }
}

TEST(CSVWriterTest, WriteFieldsCoordinatesComeFromMesh) {
  const Mesh mesh = MeshGeometry::createCartesian2D(2, 2, 1.0, 1.0);
  const auto result = makeResultFor2x2(false);
  const auto path = tempFile("fields_coords.csv");

  CSVWriter::writeFields(path, mesh, result);
  const auto lines = readLines(path);
  std::istringstream row(lines[1]);  // cell 0.
  std::string cellId, x, y;
  std::getline(row, cellId, ',');
  std::getline(row, x, ',');
  std::getline(row, y, ',');
  EXPECT_DOUBLE_EQ(std::stod(x), mesh.cell(0).centroid().x);
  EXPECT_DOUBLE_EQ(std::stod(y), mesh.cell(0).centroid().y);
}

TEST(CSVWriterTest, WriteFieldsVelocityPressureAndMagnitudeAreCorrect) {
  const Mesh mesh = MeshGeometry::createCartesian2D(2, 2, 1.0, 1.0);
  const auto result = makeResultFor2x2(false);
  const auto path = tempFile("fields_values.csv");

  CSVWriter::writeFields(path, mesh, result);
  const auto lines = readLines(path);
  std::istringstream row(lines[2]);  // cell 1: velocity (3,4), pressure 20.
  std::string cellId, x, y, vx, vy, mag, pressure;
  std::getline(row, cellId, ',');
  std::getline(row, x, ',');
  std::getline(row, y, ',');
  std::getline(row, vx, ',');
  std::getline(row, vy, ',');
  std::getline(row, mag, ',');
  std::getline(row, pressure, ',');
  EXPECT_DOUBLE_EQ(std::stod(vx), 3.0);
  EXPECT_DOUBLE_EQ(std::stod(vy), 4.0);
  EXPECT_DOUBLE_EQ(std::stod(mag), 5.0);  // sqrt(3^2+4^2).
  EXPECT_DOUBLE_EQ(std::stod(pressure), 20.0);
}

TEST(CSVWriterTest, WriteFieldsUsesFullPrecisionScientificNotation) {
  const Mesh mesh = MeshGeometry::createCartesian2D(2, 2, 1.0, 1.0);
  auto result = makeResultFor2x2(false);
  result.pressure[0] = 1.0 / 3.0;  // needs many digits to round-trip exactly.
  const auto path = tempFile("fields_precision.csv");

  CSVWriter::writeFields(path, mesh, result);
  const auto lines = readLines(path);
  EXPECT_NE(lines[1].find('e'), std::string::npos) << "expected scientific notation: " << lines[1];
  std::istringstream row(lines[1]);
  std::string discarded, pressureField;
  for (int i = 0; i < 6; ++i) std::getline(row, discarded, ',');  // cell_id..velocity_magnitude.
  std::getline(row, pressureField, ',');                          // the 7th (last) field.
  EXPECT_DOUBLE_EQ(std::stod(pressureField), 1.0 / 3.0);
}

TEST(CSVWriterTest, WriteFieldsMismatchedSizeThrows) {
  const Mesh mesh = MeshGeometry::createCartesian2D(2, 2, 1.0, 1.0);
  SIMPLEResult result;
  result.velocity = VectorField(3);  // wrong size for a 4-cell mesh.
  result.pressure = ScalarField(4);
  const auto path = tempFile("fields_mismatch.csv");
  EXPECT_THROW((void)CSVWriter::writeFields(path, mesh, result), InvalidArgumentError);
}

TEST(CSVWriterTest, WriteFieldsNonFiniteValueThrows) {
  const Mesh mesh = MeshGeometry::createCartesian2D(2, 2, 1.0, 1.0);
  auto result = makeResultFor2x2(false);
  result.pressure[2] = std::numeric_limits<cfd::Real>::infinity();
  const auto path = tempFile("fields_nonfinite.csv");
  EXPECT_THROW((void)CSVWriter::writeFields(path, mesh, result), NumericalError);
}

TEST(CSVWriterTest, WriteFieldsUnwritablePathThrowsIOError) {
  const Mesh mesh = MeshGeometry::createCartesian2D(2, 2, 1.0, 1.0);
  const auto result = makeResultFor2x2(false);
  EXPECT_THROW(
      (void)CSVWriter::writeFields("this/directory/does/not/exist/fields.csv", mesh, result),
      cfd::IOError);
}

TEST(CSVWriterTest, WriteResidualsHeaderAndRowCount) {
  const auto result = makeResultFor2x2(true);
  const auto path = tempFile("residuals_header.csv");

  CSVWriter::writeResiduals(path, result);
  const auto lines = readLines(path);
  ASSERT_FALSE(lines.empty());
  EXPECT_EQ(lines.front(),
            "iteration,u_residual,v_residual,p_residual,continuity_residual,global_mass_imbalance");
  EXPECT_EQ(lines.size(), 1u + result.uResidualHistory.size());
}

TEST(CSVWriterTest, WriteResidualsIterationColumnIsOneIndexed) {
  const auto result = makeResultFor2x2(true);
  const auto path = tempFile("residuals_iteration.csv");

  CSVWriter::writeResiduals(path, result);
  const auto lines = readLines(path);
  for (std::size_t k = 0; k < result.uResidualHistory.size(); ++k) {
    std::istringstream row(lines[k + 1]);
    std::string iterationField;
    std::getline(row, iterationField, ',');
    EXPECT_EQ(iterationField, std::to_string(k + 1));
  }
}

TEST(CSVWriterTest, WriteResidualsValuesMatchHistoriesInOrder) {
  const auto result = makeResultFor2x2(true);
  const auto path = tempFile("residuals_values.csv");

  CSVWriter::writeResiduals(path, result);
  const auto lines = readLines(path);
  std::istringstream row(lines[2]);  // iteration 2 (index 1).
  std::string iteration, u, v, p, continuity, massImbalance;
  std::getline(row, iteration, ',');
  std::getline(row, u, ',');
  std::getline(row, v, ',');
  std::getline(row, p, ',');
  std::getline(row, continuity, ',');
  std::getline(row, massImbalance, ',');
  EXPECT_DOUBLE_EQ(std::stod(u), result.uResidualHistory[1]);
  EXPECT_DOUBLE_EQ(std::stod(v), result.vResidualHistory[1]);
  EXPECT_DOUBLE_EQ(std::stod(p), result.pressureResidualHistory[1]);
  EXPECT_DOUBLE_EQ(std::stod(continuity), result.continuityHistory[1]);
  EXPECT_DOUBLE_EQ(std::stod(massImbalance), result.globalMassImbalance);
}

TEST(CSVWriterTest, WriteResidualsInconsistentHistoryLengthsThrows) {
  auto result = makeResultFor2x2(true);
  result.vResidualHistory.push_back(0.001);  // now longer than the others.
  const auto path = tempFile("residuals_mismatch.csv");
  EXPECT_THROW((void)CSVWriter::writeResiduals(path, result), InvalidArgumentError);
}

TEST(CSVWriterTest, RepeatedWriteFieldsIsByteIdentical) {
  const Mesh mesh = MeshGeometry::createCartesian2D(2, 2, 1.0, 1.0);
  const auto result = makeResultFor2x2(false);
  const auto pathA = tempFile("fields_repeat_a.csv");
  const auto pathB = tempFile("fields_repeat_b.csv");

  CSVWriter::writeFields(pathA, mesh, result);
  CSVWriter::writeFields(pathB, mesh, result);
  EXPECT_EQ(readLines(pathA), readLines(pathB));
}

// --- P2-THERMAL-004: optional temperature column -------------------------

TEST(CSVWriterTest, OmitsTemperatureColumnWhenNotProvided) {
  const Mesh mesh = MeshGeometry::createCartesian2D(2, 2, 1.0, 1.0);
  const auto result = makeResultFor2x2(false);
  const auto path = tempFile("fields_no_temperature.csv");

  CSVWriter::writeFields(path, mesh, result);
  const auto lines = readLines(path);
  EXPECT_EQ(lines.front(), "cell_id,x,y,velocity_x,velocity_y,velocity_magnitude,pressure");
}

TEST(CSVWriterTest, AppendsTemperatureColumnWhenProvided) {
  const Mesh mesh = MeshGeometry::createCartesian2D(2, 2, 1.0, 1.0);
  const auto result = makeResultFor2x2(false);
  ScalarField temperature(4);
  temperature[0] = 310.0;
  temperature[1] = 305.0;
  temperature[2] = 300.0;
  temperature[3] = 295.0;
  const auto path = tempFile("fields_with_temperature.csv");

  CSVWriter::writeFields(path, mesh, result, temperature);
  const auto lines = readLines(path);
  EXPECT_EQ(lines.front(),
           "cell_id,x,y,velocity_x,velocity_y,velocity_magnitude,pressure,temperature");
  std::istringstream row(lines[2]);  // cell 1.
  std::string field;
  for (int i = 0; i < 7; ++i) std::getline(row, field, ',');  // skip through pressure.
  std::getline(row, field, ',');
  EXPECT_DOUBLE_EQ(std::stod(field), 305.0);
}

TEST(CSVWriterTest, MismatchedTemperatureSizeThrows) {
  const Mesh mesh = MeshGeometry::createCartesian2D(2, 2, 1.0, 1.0);
  const auto result = makeResultFor2x2(false);
  const ScalarField temperature(5);  // one too many.
  const auto path = tempFile("fields_bad_temperature_size.csv");

  EXPECT_THROW(CSVWriter::writeFields(path, mesh, result, temperature), InvalidArgumentError);
}

TEST(CSVWriterTest, NonFiniteTemperatureThrows) {
  const Mesh mesh = MeshGeometry::createCartesian2D(2, 2, 1.0, 1.0);
  const auto result = makeResultFor2x2(false);
  ScalarField temperature(4, 300.0);
  temperature[2] = std::numeric_limits<cfd::Real>::quiet_NaN();
  const auto path = tempFile("fields_nan_temperature.csv");

  EXPECT_THROW(CSVWriter::writeFields(path, mesh, result, temperature), NumericalError);
}
