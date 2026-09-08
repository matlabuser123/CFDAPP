#pragma once

// P1 -- Result Export: orchestrates CSVWriter/JSONWriter/VTKWriter
// (section 27 -- one coordinating call, not one giant writeEverything()
// that also contains their formatting logic) and implements the
// failed-solve export policy (section 29).

#include <filesystem>
#include <optional>

#include "cfd/io/JSONWriter.hpp"
#include "cfd/mesh/Mesh.hpp"
#include "cfd/pressure_velocity/SIMPLEResult.hpp"

namespace cfd::io {

// Which files were actually written -- fieldsCsvPath/vtkPath are empty
// when the result's fields were non-finite (see ResultExporter::write).
struct ResultExportSummary {
  std::filesystem::path metadataPath;
  std::filesystem::path residualsCsvPath;
  std::optional<std::filesystem::path> fieldsCsvPath;
  std::optional<std::filesystem::path> vtkPath;
};

class ResultExporter {
 public:
  // Creates `outputDirectory` if missing (section 30), then writes with
  // fixed, deterministic filenames (section 32 -- always
  // fields.csv/residuals.csv/metadata.json/solution.vtk, overwriting any
  // existing ones, never solution1.vtk/solution2.vtk/...):
  //
  //  - metadata.json and residuals.csv are always written -- residual
  //    history and run metadata remain valid diagnostic data even for a
  //    failed solve (section 29).
  //  - fields.csv and solution.vtk are written only if every exported
  //    velocity/pressure value is finite -- covers both Converged and
  //    MaxIterations ("latest finite solution", section 29) but skips a
  //    NonFiniteState (or any other status whose fields happen to be
  //    non-finite) rather than writing a corrupt field file (section 40).
  //    The JSON `numerics.finite` flag and `solver.status` always say
  //    which case this was; a failed solve is never mislabeled as
  //    converged (section 29's closing rule) since `solver.status`/
  //    `solver.converged` come directly from SIMPLEResult, not from
  //    whether files happened to be written.
  [[nodiscard]] static ResultExportSummary write(const std::filesystem::path& outputDirectory,
                                                 const cfd::mesh::Mesh& mesh,
                                                 const cfd::pressure_velocity::SIMPLEResult& result,
                                                 const RunMetadata& metadata);
};

}  // namespace cfd::io
