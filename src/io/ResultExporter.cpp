#include "cfd/io/ResultExporter.hpp"

#include <cmath>
#include <filesystem>

#include "cfd/core/Exception.hpp"
#include "cfd/io/CSVWriter.hpp"
#include "cfd/io/VTKWriter.hpp"

namespace cfd::io {

using cfd::Index;
using cfd::mesh::Mesh;
using cfd::pressure_velocity::SIMPLEResult;

namespace {

bool allFieldsFinite(const SIMPLEResult& result) {
  for (Index i = 0; i < result.velocity.size(); ++i) {
    if (!std::isfinite(result.velocity[i].x) || !std::isfinite(result.velocity[i].y)) return false;
  }
  for (Index i = 0; i < result.pressure.size(); ++i) {
    if (!std::isfinite(result.pressure[i])) return false;
  }
  return true;
}

}  // namespace

ResultExportSummary ResultExporter::write(const std::filesystem::path& outputDirectory,
                                          const Mesh& mesh, const SIMPLEResult& result,
                                          const RunMetadata& metadata) {
  std::error_code createError;
  std::filesystem::create_directories(outputDirectory, createError);
  if (createError) {
    throw IOError("Could not create output directory: " + outputDirectory.string() + ": " +
                  createError.message());
  }

  ResultExportSummary summary;

  summary.metadataPath = outputDirectory / "metadata.json";
  JSONWriter::writeMetadata(summary.metadataPath, metadata, mesh, result);

  summary.residualsCsvPath = outputDirectory / "residuals.csv";
  CSVWriter::writeResiduals(summary.residualsCsvPath, result);

  if (allFieldsFinite(result)) {
    const std::filesystem::path fieldsPath = outputDirectory / "fields.csv";
    CSVWriter::writeFields(fieldsPath, mesh, result);
    summary.fieldsCsvPath = fieldsPath;

    const std::filesystem::path vtkPath = outputDirectory / "solution.vtk";
    VTKWriter::writeSolution(vtkPath, mesh, result);
    summary.vtkPath = vtkPath;
  }

  return summary;
}

}  // namespace cfd::io
