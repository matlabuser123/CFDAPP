// P1 -- Result Export, section 37: VTKWriter unit tests. Tiny meshes
// (1x1, 2x1, 2x2), parsed back as plain text -- no ParaView/VTK library
// needed to check header, point/cell counts, connectivity, and field
// values structurally.
#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <limits>
#include <sstream>
#include <string>
#include <vector>

#include "cfd/core/Exception.hpp"
#include "cfd/io/VTKWriter.hpp"
#include "cfd/mesh/MeshGeometry.hpp"

using cfd::Index;
using cfd::InvalidArgumentError;
using cfd::NumericalError;
using cfd::Vector2;
using cfd::fields::ScalarField;
using cfd::fields::VectorField;
using cfd::io::VTKWriter;
using cfd::mesh::Mesh;
using cfd::mesh::MeshGeometry;
using cfd::pressure_velocity::SIMPLEResult;

namespace {

std::filesystem::path tempFile(const std::string& name) {
  return std::filesystem::temp_directory_path() / ("cfdapp_vtk_test_" + name);
}

std::vector<std::string> readLines(const std::filesystem::path& path) {
  std::ifstream in(path);
  std::vector<std::string> lines;
  std::string line;
  while (std::getline(in, line)) lines.push_back(line);
  return lines;
}

SIMPLEResult makeResult(Index numberOfCells) {
  SIMPLEResult result;
  result.velocity = VectorField(numberOfCells);
  result.pressure = ScalarField(numberOfCells);
  for (Index i = 0; i < numberOfCells; ++i) {
    result.velocity[i] = Vector2{static_cast<cfd::Real>(i) + 1.0, static_cast<cfd::Real>(i) + 2.0};
    result.pressure[i] = static_cast<cfd::Real>(i) * 10.0;
  }
  return result;
}

// Locates the (1-indexed within the file) line containing `marker`
// exactly; ASSERT-fails the calling test if not found.
std::size_t findLine(const std::vector<std::string>& lines, const std::string& prefix) {
  for (std::size_t i = 0; i < lines.size(); ++i) {
    if (lines[i].rfind(prefix, 0) == 0) return i;
  }
  ADD_FAILURE() << "line starting with \"" << prefix << "\" not found";
  return 0;
}

}  // namespace

TEST(VTKWriterTest, HeaderIsWellFormed) {
  const Mesh mesh = MeshGeometry::createCartesian2D(1, 1, 1.0, 1.0);
  const auto path = tempFile("header.vtk");
  VTKWriter::writeSolution(path, mesh, makeResult(1));
  const auto lines = readLines(path);

  ASSERT_GE(lines.size(), 4u);
  EXPECT_EQ(lines[0], "# vtk DataFile Version 3.0");
  EXPECT_EQ(lines[2], "ASCII");
  EXPECT_EQ(lines[3], "DATASET UNSTRUCTURED_GRID");
}

TEST(VTKWriterTest, PointCountMatchesStructuredGrid) {
  // 2x1 cells -> (2+1)*(1+1) = 6 points.
  const Mesh mesh = MeshGeometry::createCartesian2D(2, 1, 2.0, 1.0);
  const auto path = tempFile("points_2x1.vtk");
  VTKWriter::writeSolution(path, mesh, makeResult(2));
  const auto lines = readLines(path);

  const auto pointsLine = findLine(lines, "POINTS ");
  EXPECT_EQ(lines[pointsLine], "POINTS 6 double");
  // Exactly 6 coordinate lines follow before the next section.
  Index count = 0;
  for (std::size_t i = pointsLine + 1; i < lines.size() && lines[i].rfind("CELLS", 0) != 0; ++i)
    ++count;
  EXPECT_EQ(count, 6u);
}

TEST(VTKWriterTest, CellCountAndConnectivityAreCorrectFor2x2) {
  const Mesh mesh = MeshGeometry::createCartesian2D(2, 2, 1.0, 1.0);
  const auto path = tempFile("cells_2x2.vtk");
  VTKWriter::writeSolution(path, mesh, makeResult(4));
  const auto lines = readLines(path);

  const auto cellsLine = findLine(lines, "CELLS ");
  EXPECT_EQ(lines[cellsLine], "CELLS 4 20");  // 4 cells * (1 count + 4 indices) = 20.
  // Cell 0 (i=0,j=0): points (nx=2, so row stride 3) 0,1,4,3.
  EXPECT_EQ(lines[cellsLine + 1], "4 0 1 4 3");
  // Cell 1 (i=1,j=0): 1,2,5,4.
  EXPECT_EQ(lines[cellsLine + 2], "4 1 2 5 4");

  const auto typesLine = findLine(lines, "CELL_TYPES ");
  EXPECT_EQ(lines[typesLine], "CELL_TYPES 4");
  for (int k = 1; k <= 4; ++k) EXPECT_EQ(lines[typesLine + k], "9");  // VTK_QUAD.
}

TEST(VTKWriterTest, CellDataSectionHasPressureVelocityThenMagnitudeInOrder) {
  const Mesh mesh = MeshGeometry::createCartesian2D(1, 1, 1.0, 1.0);
  auto result = makeResult(1);
  result.pressure[0] = 42.0;
  result.velocity[0] = Vector2{3.0, 4.0};
  const auto path = tempFile("celldata_order.vtk");
  VTKWriter::writeSolution(path, mesh, result);
  const auto lines = readLines(path);

  const auto cellDataLine = findLine(lines, "CELL_DATA ");
  EXPECT_EQ(lines[cellDataLine], "CELL_DATA 1");
  EXPECT_EQ(lines[cellDataLine + 1], "SCALARS pressure double 1");
  EXPECT_EQ(lines[cellDataLine + 2], "LOOKUP_TABLE default");
  EXPECT_DOUBLE_EQ(std::stod(lines[cellDataLine + 3]), 42.0);
  EXPECT_EQ(lines[cellDataLine + 4], "VECTORS velocity double");
  std::istringstream velocityRow(lines[cellDataLine + 5]);
  double vx = 0.0, vy = 0.0, vz = 0.0;
  velocityRow >> vx >> vy >> vz;
  EXPECT_DOUBLE_EQ(vx, 3.0);
  EXPECT_DOUBLE_EQ(vy, 4.0);
  EXPECT_DOUBLE_EQ(vz, 0.0);
  EXPECT_EQ(lines[cellDataLine + 6], "SCALARS velocity_magnitude double 1");
  EXPECT_EQ(lines[cellDataLine + 7], "LOOKUP_TABLE default");
  EXPECT_DOUBLE_EQ(std::stod(lines[cellDataLine + 8]), 5.0);
}

TEST(VTKWriterTest, ZCoordinateIsAlwaysZeroFor2DMesh) {
  const Mesh mesh = MeshGeometry::createCartesian2D(1, 1, 1.0, 1.0);
  const auto path = tempFile("z_coord.vtk");
  VTKWriter::writeSolution(path, mesh, makeResult(1));
  const auto lines = readLines(path);
  const auto pointsLine = findLine(lines, "POINTS ");
  std::istringstream firstPoint(lines[pointsLine + 1]);
  double x = 0.0, y = 0.0, z = 1.0;
  firstPoint >> x >> y >> z;
  EXPECT_DOUBLE_EQ(z, 0.0);
}

TEST(VTKWriterTest, MismatchedSizeThrows) {
  const Mesh mesh = MeshGeometry::createCartesian2D(2, 2, 1.0, 1.0);
  const auto result = makeResult(3);  // wrong size for a 4-cell mesh.
  const auto path = tempFile("mismatch.vtk");
  EXPECT_THROW((void)VTKWriter::writeSolution(path, mesh, result), InvalidArgumentError);
}

TEST(VTKWriterTest, NonFiniteValueThrows) {
  const Mesh mesh = MeshGeometry::createCartesian2D(1, 1, 1.0, 1.0);
  auto result = makeResult(1);
  result.velocity[0].x = std::numeric_limits<cfd::Real>::infinity();
  const auto path = tempFile("nonfinite.vtk");
  EXPECT_THROW((void)VTKWriter::writeSolution(path, mesh, result), NumericalError);
}

TEST(VTKWriterTest, RepeatedWriteIsByteIdentical) {
  const Mesh mesh = MeshGeometry::createCartesian2D(2, 2, 1.0, 1.0);
  const auto result = makeResult(4);
  const auto pathA = tempFile("repeat_a.vtk");
  const auto pathB = tempFile("repeat_b.vtk");
  VTKWriter::writeSolution(pathA, mesh, result);
  VTKWriter::writeSolution(pathB, mesh, result);
  EXPECT_EQ(readLines(pathA), readLines(pathB));
}

// --- P2-THERMAL-004: optional temperature SCALARS block -------------------

TEST(VTKWriterTest, OmitsTemperatureBlockWhenNotProvided) {
  const Mesh mesh = MeshGeometry::createCartesian2D(1, 1, 1.0, 1.0);
  const auto path = tempFile("no_temperature.vtk");
  VTKWriter::writeSolution(path, mesh, makeResult(1));
  const auto lines = readLines(path);
  for (const auto& line : lines) {
    EXPECT_EQ(line.find("temperature"), std::string::npos);
  }
}

TEST(VTKWriterTest, AppendsTemperatureBlockAfterVelocityMagnitudeWhenProvided) {
  const Mesh mesh = MeshGeometry::createCartesian2D(1, 1, 1.0, 1.0);
  const auto result = makeResult(1);
  ScalarField temperature(1);
  temperature[0] = 273.15;
  const auto path = tempFile("with_temperature.vtk");
  VTKWriter::writeSolution(path, mesh, result, temperature);
  const auto lines = readLines(path);

  const auto magnitudeLine = findLine(lines, "SCALARS velocity_magnitude");
  // LOOKUP_TABLE + one value row for velocity_magnitude, then the
  // temperature block starts.
  EXPECT_EQ(lines[magnitudeLine + 3], "SCALARS temperature double 1");
  EXPECT_EQ(lines[magnitudeLine + 4], "LOOKUP_TABLE default");
  EXPECT_DOUBLE_EQ(std::stod(lines[magnitudeLine + 5]), 273.15);
}

TEST(VTKWriterTest, MismatchedTemperatureSizeThrows) {
  const Mesh mesh = MeshGeometry::createCartesian2D(2, 2, 1.0, 1.0);
  const auto result = makeResult(4);
  const ScalarField temperature(3);  // wrong size.
  const auto path = tempFile("bad_temperature_size.vtk");
  EXPECT_THROW((void)VTKWriter::writeSolution(path, mesh, result, temperature),
               InvalidArgumentError);
}

TEST(VTKWriterTest, NonFiniteTemperatureThrows) {
  const Mesh mesh = MeshGeometry::createCartesian2D(1, 1, 1.0, 1.0);
  const auto result = makeResult(1);
  ScalarField temperature(1);
  temperature[0] = std::numeric_limits<cfd::Real>::infinity();
  const auto path = tempFile("nonfinite_temperature.vtk");
  EXPECT_THROW((void)VTKWriter::writeSolution(path, mesh, result, temperature), NumericalError);
}

// --- P6-PHYS-001: optional species SCALARS blocks --------------------------

TEST(VTKWriterTest, OmitsSpeciesBlocksWhenNotProvided) {
  const Mesh mesh = MeshGeometry::createCartesian2D(1, 1, 1.0, 1.0);
  const auto path = tempFile("no_species.vtk");
  VTKWriter::writeSolution(path, mesh, makeResult(1));
  const auto lines = readLines(path);
  for (const auto& line : lines) {
    EXPECT_EQ(line.find("concentration_"), std::string::npos);
  }
}

TEST(VTKWriterTest, AppendsOneSpeciesBlockAfterVelocityMagnitudeWhenProvided) {
  const Mesh mesh = MeshGeometry::createCartesian2D(1, 1, 1.0, 1.0);
  const auto result = makeResult(1);
  ScalarField co2(1);
  co2[0] = 0.5;
  const auto path = tempFile("with_one_species.vtk");
  VTKWriter::writeSolution(path, mesh, result, std::nullopt, {{"CO2", co2}});
  const auto lines = readLines(path);

  const auto magnitudeLine = findLine(lines, "SCALARS velocity_magnitude");
  EXPECT_EQ(lines[magnitudeLine + 3], "SCALARS concentration_CO2 double 1");
  EXPECT_EQ(lines[magnitudeLine + 4], "LOOKUP_TABLE default");
  EXPECT_DOUBLE_EQ(std::stod(lines[magnitudeLine + 5]), 0.5);
}

TEST(VTKWriterTest, MultipleSpeciesWrittenInOrderAfterTemperature) {
  const Mesh mesh = MeshGeometry::createCartesian2D(1, 1, 1.0, 1.0);
  const auto result = makeResult(1);
  ScalarField temperature(1, 300.0);
  ScalarField co2(1, 0.5);
  ScalarField o2(1, 0.21);
  const auto path = tempFile("with_temperature_and_species.vtk");
  VTKWriter::writeSolution(path, mesh, result, temperature, {{"CO2", co2}, {"O2", o2}});
  const auto lines = readLines(path);

  const auto temperatureLine = findLine(lines, "SCALARS temperature");
  // +0 "SCALARS temperature ...", +1 "LOOKUP_TABLE default", +2 the one
  // temperature value row -- then the CO2 block starts.
  EXPECT_EQ(lines[temperatureLine + 3], "SCALARS concentration_CO2 double 1");
  EXPECT_DOUBLE_EQ(std::stod(lines[temperatureLine + 5]), 0.5);
  EXPECT_EQ(lines[temperatureLine + 6], "SCALARS concentration_O2 double 1");
  EXPECT_DOUBLE_EQ(std::stod(lines[temperatureLine + 8]), 0.21);
}

TEST(VTKWriterTest, MismatchedSpeciesSizeThrows) {
  const Mesh mesh = MeshGeometry::createCartesian2D(2, 2, 1.0, 1.0);
  const auto result = makeResult(4);
  const ScalarField co2(3);  // wrong size.
  const auto path = tempFile("bad_species_size.vtk");
  EXPECT_THROW((void)VTKWriter::writeSolution(path, mesh, result, std::nullopt, {{"CO2", co2}}),
               InvalidArgumentError);
}

TEST(VTKWriterTest, NonFiniteSpeciesThrows) {
  const Mesh mesh = MeshGeometry::createCartesian2D(1, 1, 1.0, 1.0);
  const auto result = makeResult(1);
  ScalarField co2(1);
  co2[0] = std::numeric_limits<cfd::Real>::quiet_NaN();
  const auto path = tempFile("nonfinite_species.vtk");
  EXPECT_THROW((void)VTKWriter::writeSolution(path, mesh, result, std::nullopt, {{"CO2", co2}}),
               NumericalError);
}
