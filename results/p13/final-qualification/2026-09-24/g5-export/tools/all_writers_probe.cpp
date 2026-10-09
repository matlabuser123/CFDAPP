// P13 FINAL QUALIFICATION -- G5: the /dev/full probe, EXTENDED to every writer
// ResultExporter actually calls in production.
//
// WHY THIS EXISTS, recorded rather than silent.
// results/p13/export/tools/partial_write_probe.cpp is the frozen instrument and
// is used unmodified alongside this one. It measures FOUR writers:
//
//     CSVWriter::writeFields, CSVWriter::writeResiduals,
//     JSONWriter::writeMetadata, VTKWriter::writeCellFields
//
// ResultExporter.cpp calls SIX:
//
//     line  56  JSONWriter::writeMetadata        covered by the frozen probe
//     line  60  CSVWriter::writeResiduals        covered
//     line  68  CSVWriter::writeFields3D         NOT covered   <- 3D fields path
//     line  74  VTKWriter::writeCellFields       covered
//     line 101  CSVWriter::writeFields           covered
//     line 105  VTKWriter::writeSolution         NOT covered   <- 2D solution.vtk
//
// The permanent test tests/integration/io/test_export_write_failure.cpp is named
// `EveryWriterReportsAFailedWrite` and also covers only those same four. So the
// writer that produces solution.vtk for every 2D case -- the ordinary export
// path -- has never had its write-failure detection measured.
//
// The production protection IS present at all five openDeterministicOutput
// sites (CSVWriter.cpp:79,108,150 and VTKWriter.cpp:172,277) plus JSONWriter's
// own flush-and-check. This probe measures whether that is true rather than
// inferring it from the call sites, which is the whole point of the /dev/full
// instrument.
//
// Same contract as the frozen probe: exit code == number of writers that
// silently reported success. 0 is the pass.
#include <cstdio>
#include <optional>
#include <string>
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

int returnedNormally = 0;
int threw = 0;
bool healthyMode = false;  // argv[1] given -> write to a real directory

template <typename F>
void probe(const char* name, F&& call) {
  try {
    call();
    std::printf("  %-32s -> RETURNED NORMALLY\n", name);
    ++returnedNormally;
  } catch (const cfd::IOError& e) {
    std::printf("  %-32s -> threw IOError: %s\n", name, e.what());
    ++threw;
  } catch (const std::exception& e) {
    // Anything other than IOError is reported separately: the frozen criterion
    // W1 says the writers throw IOError naming the path.
    std::printf("  %-32s -> threw NON-IOError: %s  (criterion W1 wants IOError)\n", name, e.what());
    ++threw;
  }
}

cfd::pressure_velocity::SIMPLEResult makeResult(const cfd::mesh::Mesh& mesh, bool threeD) {
  cfd::pressure_velocity::SIMPLEResult result;
  result.velocity = cfd::fields::VectorField(mesh.numberOfCells());
  result.pressure = cfd::fields::ScalarField(mesh.numberOfCells());
  result.massFlux = cfd::fields::SurfaceField(mesh.numberOfFaces());
  result.iterations = 1;
  result.uResidualHistory = {1.0};
  result.vResidualHistory = {1.0};
  if (threeD) result.wResidualHistory = {1.0};
  result.pressureResidualHistory = {1.0};
  result.continuityHistory = {1.0};
  result.globalMassImbalanceHistory = {0.0};
  return result;
}

}  // namespace

int main(int argc, char** argv) {
  // No argument  -> /dev/full. Criterion W1: every writer must throw, so
  //                 "returned normally" must be 0. Exit code = that count.
  // A directory  -> control N3, "point the probe at a normal file". The same
  //                 instrument on a healthy path: every writer must SUCCEED, so
  //                 "threw" must be 0. Exit code = that count. A check that
  //                 fired everywhere would satisfy W1 while breaking every real
  //                 export, and this is what rules that out.
  std::string dir;
  if (argc > 1) {
    healthyMode = true;
    dir = argv[1];
  }
  const auto dest = [&](const char* leaf) -> std::string {
    return healthyMode ? dir + "/" + leaf : std::string("/dev/full");
  };

  if (healthyMode) {
    std::printf("G5 control N3 -- every production writer, written to a REAL directory\n");
    std::printf("destination: %s\nevery writer must SUCCEED here\n\n", dir.c_str());
  } else {
    std::printf("G5 -- every production writer, written to /dev/full\n");
    std::printf("opens succeed; every write fails ENOSPC\n\n");
  }

  const auto mesh2d = cfd::mesh::MeshGeometry::createCartesian2D(4, 4, 1.0, 1.0);
  const auto r2d = makeResult(mesh2d, false);
  const auto mesh3d = cfd::mesh::MeshGeometry::createCartesian3D(3, 3, 3, 1.0, 1.0, 1.0);
  const auto r3d = makeResult(mesh3d, true);

  const cfd::io::RunMetadata metadata{"g5-all-writers", 1.0, 0.01, "SIMPLE"};
  const std::vector<cfd::io::NamedScalarField> scalars{{"p", r3d.pressure}};

  std::printf("-- the four the frozen probe already covers --\n");
  probe("CSVWriter::writeFields",
        [&] { cfd::io::CSVWriter::writeFields(dest("fields.csv"), mesh2d, r2d); });
  probe("CSVWriter::writeResiduals",
        [&] { cfd::io::CSVWriter::writeResiduals(dest("residuals.csv"), r2d); });
  probe("JSONWriter::writeMetadata", [&] {
    cfd::io::JSONWriter::writeMetadata(dest("metadata.json"), metadata, mesh2d, r2d);
  });
  probe("VTKWriter::writeCellFields",
        [&] { cfd::io::VTKWriter::writeCellFields(dest("cells.vtk"), mesh3d, scalars); });

  std::printf("\n-- the two it does not --\n");
  probe("CSVWriter::writeFields3D",
        [&] { cfd::io::CSVWriter::writeFields3D(dest("fields3d.csv"), mesh3d, r3d); });
  probe("VTKWriter::writeSolution",
        [&] { cfd::io::VTKWriter::writeSolution(dest("solution.vtk"), mesh2d, r2d); });

  std::printf("\n  returned normally: %d    threw: %d\n", returnedNormally, threw);
  if (healthyMode) {
    std::printf("control N3 exit code (= writers that threw on a HEALTHY path): %d\n", threw);
    return threw;
  }
  std::printf("probe exit code (= number of writers that silently succeeded): %d\n",
              returnedNormally);
  return returnedNormally;
}
