// P13-004 -- an export that cannot be written must say so.
//
// Every writer used to check the OPEN and never the WRITE. A full disk, an
// exhausted quota or an I/O error part way through leaves std::ofstream in a
// failed state, and its destructor closes it without complaint -- so the
// writer returned normally, ResultExporter reported success, and the user got
// a `Results:` block listing empty files and exit code 0.
//
// Measured, not theorised: all four writers returned normally when written to
// /dev/full (results/p13/export/logs/00_partial_write_BASELINE.log).
//
// /dev/full is the deterministic way to test this: it opens successfully and
// fails every write with ENOSPC, which is exactly a full disk without needing
// one. It is POSIX-only, so these tests skip elsewhere rather than pretending
// to have run -- a skipped test that says so beats a test that quietly checks
// nothing on half its platforms.
#include <gtest/gtest.h>

#include <filesystem>
#include <vector>

#include "cfd/core/Exception.hpp"
#include "cfd/fields/ScalarField.hpp"
#include "cfd/fields/SurfaceField.hpp"
#include "cfd/fields/VectorField.hpp"
#include "cfd/io/CSVWriter.hpp"
#include "cfd/io/JSONWriter.hpp"
#include "cfd/io/VTKWriter.hpp"
#include "cfd/mesh/MeshGeometry.hpp"
#include "cfd/pressure_velocity/SIMPLEResult.hpp"

namespace {

bool haveDevFull() {
  std::error_code ec;
  return std::filesystem::exists("/dev/full", ec) && !ec;
}

cfd::pressure_velocity::SIMPLEResult makeResult(const cfd::mesh::Mesh& mesh) {
  cfd::pressure_velocity::SIMPLEResult result;
  result.velocity = cfd::fields::VectorField(mesh.numberOfCells());
  result.pressure = cfd::fields::ScalarField(mesh.numberOfCells());
  result.massFlux = cfd::fields::SurfaceField(mesh.numberOfFaces());
  result.iterations = 1;
  result.uResidualHistory = {1.0};
  result.vResidualHistory = {1.0};
  result.pressureResidualHistory = {1.0};
  result.continuityHistory = {1.0};
  result.globalMassImbalanceHistory = {0.0};
  return result;
}

}  // namespace

TEST(ExportWriteFailureTest, EveryWriterReportsAFailedWrite) {
  if (!haveDevFull()) GTEST_SKIP() << "/dev/full is POSIX-only";
  const auto mesh = cfd::mesh::MeshGeometry::createCartesian2D(4, 4, 1.0, 1.0);
  const auto result = makeResult(mesh);

  EXPECT_THROW(cfd::io::CSVWriter::writeFields("/dev/full", mesh, result), cfd::IOError);
  EXPECT_THROW(cfd::io::CSVWriter::writeResiduals("/dev/full", result), cfd::IOError);

  const cfd::io::RunMetadata metadata{"write-failure", 1.0, 0.01, "SIMPLE"};
  EXPECT_THROW(cfd::io::JSONWriter::writeMetadata("/dev/full", metadata, mesh, result),
               cfd::IOError);

  const std::vector<cfd::io::NamedScalarField> scalars{{"p", result.pressure}};
  EXPECT_THROW(cfd::io::VTKWriter::writeCellFields("/dev/full", mesh, scalars), cfd::IOError);
}

TEST(ExportWriteFailureTest, TheFailureNamesTheFile) {
  // "Do not silently report success" is only half of it: a failure a user
  // cannot act on is barely better than none.
  if (!haveDevFull()) GTEST_SKIP() << "/dev/full is POSIX-only";
  const auto mesh = cfd::mesh::MeshGeometry::createCartesian2D(4, 4, 1.0, 1.0);
  const auto result = makeResult(mesh);
  try {
    cfd::io::CSVWriter::writeResiduals("/dev/full", result);
    FAIL() << "a completely failed write was reported as success";
  } catch (const cfd::IOError& e) {
    const std::string message = e.what();
    EXPECT_NE(message.find("/dev/full"), std::string::npos) << message;
  }
}

TEST(ExportWriteFailureTest, AHealthyWriteStillSucceeds) {
  // The non-vacuity half (control N3): a check that fired on every path would
  // pass the tests above while breaking every real export.
  const auto dir = std::filesystem::temp_directory_path() / "cfdapp_export_ok";
  std::filesystem::create_directories(dir);
  const auto mesh = cfd::mesh::MeshGeometry::createCartesian2D(4, 4, 1.0, 1.0);
  const auto result = makeResult(mesh);

  EXPECT_NO_THROW(cfd::io::CSVWriter::writeResiduals(dir / "residuals.csv", result));
  EXPECT_TRUE(std::filesystem::exists(dir / "residuals.csv"));
  EXPECT_GT(std::filesystem::file_size(dir / "residuals.csv"), 0u);
}
