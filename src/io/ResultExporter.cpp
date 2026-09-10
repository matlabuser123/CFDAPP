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
    const std::vector<NamedScalarField>& species,
    const std::vector<SpeciesRunMetadata>& speciesMetadata) {
  std::error_code createError;
  std::filesystem::create_directories(outputDirectory, createError);
  if (createError) {
    throw IOError("Could not create output directory: " + outputDirectory.string() + ": " +
                  createError.message());
  }

  ResultExportSummary summary;

  summary.metadataPath = outputDirectory / "metadata.json";
  JSONWriter::writeMetadata(summary.metadataPath, metadata, mesh, result, thermalMetadata,
                            speciesMetadata);

  summary.residualsCsvPath = outputDirectory / "residuals.csv";
  CSVWriter::writeResiduals(summary.residualsCsvPath, result);

  if (allFieldsFinite(result)) {
    // Only include temperature in the field files once it is itself
    // fully finite too -- the same "a partially-finite result never
    // produces a corrupt field file" policy extended to temperature
    // (P2-THERMAL-004), not a separate rule.
    const bool includeTemperature = temperature.has_value() && allFinite(*temperature);
    const std::optional<cfd::fields::ScalarField> temperatureForFields =
        includeTemperature ? temperature : std::nullopt;

    // P6-PHYS-001: each species filtered independently -- one non-finite
    // species does not drop any other still-finite species (or
    // temperature) from the export (see ResultExporter.hpp's own header
    // comment).
    std::vector<NamedScalarField> speciesForFields;
    for (const auto& [name, field] : species) {
      if (allFinite(field)) speciesForFields.emplace_back(name, field);
    }

    const std::filesystem::path fieldsPath = outputDirectory / "fields.csv";
    CSVWriter::writeFields(fieldsPath, mesh, result, temperatureForFields, speciesForFields);
    summary.fieldsCsvPath = fieldsPath;

    const std::filesystem::path vtkPath = outputDirectory / "solution.vtk";
    VTKWriter::writeSolution(vtkPath, mesh, result, temperatureForFields, speciesForFields);
    summary.vtkPath = vtkPath;
  }

  return summary;
}

}  // namespace cfd::io
