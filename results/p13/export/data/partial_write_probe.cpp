// P13-002/P13-004 audit probe: does an export report success when every write
// fails?
//
// /dev/full is the deterministic way to ask. It opens successfully and every
// write to it fails with ENOSPC -- exactly a full disk or an exhausted quota,
// without needing either. If a writer reports success against /dev/full, it
// will report success against a real full disk.
#include <cstdio>

#include "cfd/core/Exception.hpp"
#include "cfd/fields/ScalarField.hpp"
#include "cfd/fields/SurfaceField.hpp"
#include "cfd/fields/VectorField.hpp"
#include "cfd/io/CSVWriter.hpp"
#include "cfd/io/JSONWriter.hpp"
#include "cfd/mesh/MeshGeometry.hpp"
#include "cfd/pressure_velocity/SIMPLEResult.hpp"

int main() {
  const auto mesh = cfd::mesh::MeshGeometry::createCartesian2D(4, 4, 1.0, 1.0);
  cfd::pressure_velocity::SIMPLEResult result;
  result.velocity = cfd::fields::VectorField(mesh.numberOfCells());
  result.pressure = cfd::fields::ScalarField(mesh.numberOfCells());
  result.massFlux = cfd::fields::SurfaceField(mesh.numberOfFaces());
  result.iterations = 1;
  result.uResidualHistory = {1.0};
  result.vResidualHistory = {1.0};
  result.pressureResidualHistory = {1.0};
  result.continuityHistory = {1.0};

  int silentSuccesses = 0;

  try {
    cfd::io::CSVWriter::writeFields("/dev/full", mesh, result);
    std::printf("  CSVWriter::writeFields   -> RETURNED NORMALLY (every write failed)\n");
    ++silentSuccesses;
  } catch (const cfd::Error& e) {
    std::printf("  CSVWriter::writeFields   -> threw: %s\n", e.what());
  }

  try {
    cfd::io::CSVWriter::writeResiduals("/dev/full", result);
    std::printf("  CSVWriter::writeResiduals-> RETURNED NORMALLY (every write failed)\n");
    ++silentSuccesses;
  } catch (const cfd::Error& e) {
    std::printf("  CSVWriter::writeResiduals-> threw: %s\n", e.what());
  }

  try {
    cfd::io::RunMetadata metadata{"probe", 1.0, 0.01, "SIMPLE"};
    cfd::io::JSONWriter::writeMetadata("/dev/full", metadata, mesh, result);
    std::printf("  JSONWriter::writeMetadata-> RETURNED NORMALLY (every write failed)\n");
    ++silentSuccesses;
  } catch (const cfd::Error& e) {
    std::printf("  JSONWriter::writeMetadata-> threw: %s\n", e.what());
  }

  std::printf("\n  writers that silently reported success: %d\n", silentSuccesses);
  return silentSuccesses;
}
