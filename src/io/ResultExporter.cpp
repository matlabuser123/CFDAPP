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
    if (!isFinite(result.velocity[i])) return false;
  }
  for (Index i = 0; i < result.pressure.size(); ++i) {
    if (!std::isfinite(result.pressure[i])) return false;
  }
  return true;
}

bool allFinite(const cfd::fields::ScalarField& field) {
  for (Index i = 0; i < field.size(); ++i) {
    if (!std::isfinite(field[i])) return false;
  }
  return true;
}

}  // namespace

ResultExportSummary ResultExporter::write(
    const std::filesystem::path& outputDirectory, const Mesh& mesh, const SIMPLEResult& result,
    const RunMetadata& metadata, const std::optional<cfd::fields::ScalarField>& temperature,
    const std::optional<ThermalRunMetadata>& thermalMetadata,
    const std::vector<NamedScalarField>& extraFields,
    const std::vector<SpeciesRunMetadata>& speciesMetadata,
    const std::optional<MultiphaseRunMetadata>& multiphaseMetadata,
    const std::optional<CompressibleRunMetadata>& compressibleMetadata,
    const std::optional<cfd::mesh::MeshQualityReport>& meshQuality) {
  std::error_code createError;
  std::filesystem::create_directories(outputDirectory, createError);
  if (createError) {
    throw IOError("Could not create output directory: " + outputDirectory.string() + ": " +
                  createError.message());
  }

  ResultExportSummary summary;

  summary.metadataPath = outputDirectory / "metadata.json";
  JSONWriter::writeMetadata(summary.metadataPath, metadata, mesh, result, thermalMetadata,
                            speciesMetadata, multiphaseMetadata, compressibleMetadata, meshQuality);

  summary.residualsCsvPath = outputDirectory / "residuals.csv";
  CSVWriter::writeResiduals(summary.residualsCsvPath, result);

  // P12-MESH-006: a 3D result -- fields.csv (writeFields3D) and hexahedral
  // VTK (writeCellFields: pressure, velocity magnitude, 3-component velocity).
  // CaseReader admits no thermal/species/multiphase/compressible field in 3D.
  if (mesh.dimension() == 3) {
    if (allFieldsFinite(result)) {
      const std::filesystem::path fieldsPath = outputDirectory / "fields.csv";
      CSVWriter::writeFields3D(fieldsPath, mesh, result);
      summary.fieldsCsvPath = fieldsPath;

      cfd::fields::ScalarField speed(mesh.numberOfCells());
      for (Index i = 0; i < mesh.numberOfCells(); ++i) speed[i] = magnitude(result.velocity[i]);
      const std::filesystem::path vtkPath = outputDirectory / "solution.vtk";
      VTKWriter::writeCellFields(vtkPath, mesh,
                                 {{"pressure", result.pressure}, {"velocity_magnitude", speed}},
                                 {{"velocity", result.velocity}});
      summary.vtkPath = vtkPath;
    }
    return summary;
  }

  if (allFieldsFinite(result)) {
    // Only include temperature in the field files once it is itself
    // fully finite too -- the same "a partially-finite result never
    // produces a corrupt field file" policy extended to temperature
    // (P2-THERMAL-004), not a separate rule.
    const bool includeTemperature = temperature.has_value() && allFinite(*temperature);
    const std::optional<cfd::fields::ScalarField> temperatureForFields =
        includeTemperature ? temperature : std::nullopt;

    // P6-PHYS-001 (generalized by P6-PHYS-002/003): each entry filtered
    // independently -- one non-finite field does not drop any other
    // still-finite field (or temperature) from the export (see
    // ResultExporter.hpp's own header comment).
    std::vector<NamedScalarField> fieldsForExport;
    for (const auto& [name, field] : extraFields) {
      if (allFinite(field)) fieldsForExport.emplace_back(name, field);
    }

    const std::filesystem::path fieldsPath = outputDirectory / "fields.csv";
    CSVWriter::writeFields(fieldsPath, mesh, result, temperatureForFields, fieldsForExport);
    summary.fieldsCsvPath = fieldsPath;

    const std::filesystem::path vtkPath = outputDirectory / "solution.vtk";
    VTKWriter::writeSolution(vtkPath, mesh, result, temperatureForFields, fieldsForExport);
    summary.vtkPath = vtkPath;
  }

  return summary;
}

}  // namespace cfd::io
