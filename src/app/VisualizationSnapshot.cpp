#include "cfd/app/VisualizationSnapshot.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <nlohmann/json.hpp>
#include <sstream>

namespace cfd::app {

namespace {

bool allFinite(const std::vector<Real>& values) {
  return std::all_of(values.begin(), values.end(), [](Real v) { return std::isfinite(v); });
}

// P6-PHYS-001/002/003: cfd::fields::ScalarField -> std::vector<Real>, the
// same conversion buildSnapshot() already does inline for temperature --
// factored out here since species/multiphase/compressible each need it
// at least once more.
std::vector<Real> toVector(const cfd::fields::ScalarField& field) {
  std::vector<Real> values(static_cast<std::size_t>(field.size()));
  for (cfd::Index i = 0; i < field.size(); ++i) values[static_cast<std::size_t>(i)] = field[i];
  return values;
}

std::vector<std::string> splitCsvLine(const std::string& line) {
  std::vector<std::string> fields;
  std::stringstream ss(line);
  std::string field;
  while (std::getline(ss, field, ',')) fields.push_back(field);
  return fields;
}

}  // namespace

std::vector<std::string> VisualizationSnapshot::availableScalarFields() const {
  if (!valid) return {};
  std::vector<std::string> fields{"pressure", "velocity_magnitude"};
  if (temperature.has_value()) fields.push_back("temperature");
  for (const auto& [name, unused] : extraScalarFields) {
    (void)unused;
    fields.push_back(name);
  }
  return fields;
}

const std::vector<Real>* VisualizationSnapshot::scalarField(const std::string& name) const {
  if (!valid) return nullptr;
  if (name == "pressure") return &pressure;
  if (name == "velocity_magnitude") return &velocityMagnitude;
  if (name == "temperature" && temperature.has_value()) return &(*temperature);
  for (const auto& [fieldName, values] : extraScalarFields) {
    if (fieldName == name) return &values;
  }
  return nullptr;
}

std::vector<std::string> VisualizationSnapshot::availableResidualSeries() const {
  if (!valid) return {};
  return {"u", "v", "pressure", "continuity"};
}

const std::vector<Real>* VisualizationSnapshot::residualSeries(const std::string& name) const {
  if (!valid) return nullptr;
  if (name == "u") return &uResidualHistory;
  if (name == "v") return &vResidualHistory;
  if (name == "pressure") return &pressureResidualHistory;
  if (name == "continuity") return &continuityResidualHistory;
  return nullptr;
}

VisualizationSnapshot buildSnapshot(const ProjectRunResult& run) {
  VisualizationSnapshot snapshot;
  if (!run.mesh.has_value() || !run.simpleResult.has_value() || !run.caseDefinition.has_value()) {
    return snapshot;
  }
  const auto& mesh = *run.mesh;
  const auto& result = *run.simpleResult;
  if (static_cast<std::size_t>(result.velocity.size()) != mesh.numberOfCells() ||
      static_cast<std::size_t>(result.pressure.size()) != mesh.numberOfCells()) {
    return snapshot;
  }

  const std::size_t n = mesh.numberOfCells();
  snapshot.points.resize(n);
  snapshot.pressure.resize(n);
  snapshot.velocityX.resize(n);
  snapshot.velocityY.resize(n);
  snapshot.velocityMagnitude.resize(n);
  for (const auto& cell : mesh.cells()) {
    const Index id = cell.id();
    snapshot.points[static_cast<std::size_t>(id)] = cell.centroid();
    snapshot.pressure[static_cast<std::size_t>(id)] = result.pressure[id];
    snapshot.velocityX[static_cast<std::size_t>(id)] = result.velocity[id].x;
    snapshot.velocityY[static_cast<std::size_t>(id)] = result.velocity[id].y;
    snapshot.velocityMagnitude[static_cast<std::size_t>(id)] = magnitude(result.velocity[id]);
  }

  // section: "only a fully-finite solution is worth showing" -- same
  // policy ResultExporter's own field-file gating already applies.
  if (!allFinite(snapshot.pressure) || !allFinite(snapshot.velocityX) ||
      !allFinite(snapshot.velocityY)) {
    return VisualizationSnapshot{};
  }

  if (run.thermalResult.has_value() &&
      static_cast<std::size_t>(run.thermalResult->temperature.size()) == n) {
    std::vector<Real> temperature(n);
    for (Index i = 0; i < run.thermalResult->temperature.size(); ++i) {
      temperature[static_cast<std::size_t>(i)] = run.thermalResult->temperature[i];
    }
    if (allFinite(temperature)) snapshot.temperature = std::move(temperature);
  }

  // P6-PHYS-001/002/003: same field names ProjectRunner.cpp's own CSV/
  // VTK export already uses (concentration_<name>, volume_fraction/
  // mixture_density/mixture_viscosity, density/pressure_absolute/
  // compressible_temperature/mach_number) -- a field selected in the GUI
  // is therefore the exact same field a reloaded case's fields.csv/
  // solution.vtk would show, never a second naming scheme.
  for (const auto& speciesRun : run.speciesResults) {
    if (static_cast<std::size_t>(speciesRun.result.concentration.size()) != n) continue;
    std::vector<Real> values = toVector(speciesRun.result.concentration);
    if (allFinite(values)) {
      snapshot.extraScalarFields.emplace_back("concentration_" + speciesRun.name,
                                              std::move(values));
    }
  }
  if (run.multiphaseResult.has_value()) {
    const auto& mp = *run.multiphaseResult;
    if (static_cast<std::size_t>(mp.alphaStep.alpha.size()) == n) {
      std::vector<Real> alpha = toVector(mp.alphaStep.alpha);
      std::vector<Real> density = toVector(mp.mixtureDensity);
      std::vector<Real> viscosity = toVector(mp.mixtureViscosity);
      if (allFinite(alpha))
        snapshot.extraScalarFields.emplace_back("volume_fraction", std::move(alpha));
      if (allFinite(density)) {
        snapshot.extraScalarFields.emplace_back("mixture_density", std::move(density));
      }
      if (allFinite(viscosity)) {
        snapshot.extraScalarFields.emplace_back("mixture_viscosity", std::move(viscosity));
      }
    }
  }
  if (run.compressibleResult.has_value()) {
    const auto& c = *run.compressibleResult;
    if (static_cast<std::size_t>(c.density.size()) == n) {
      std::vector<Real> density = toVector(c.density);
      std::vector<Real> pressureAbsolute = toVector(c.pressureAbsolute);
      std::vector<Real> compressibleTemperature = toVector(c.temperature);
      std::vector<Real> mach = toVector(c.machNumber);
      if (allFinite(density))
        snapshot.extraScalarFields.emplace_back("density", std::move(density));
      if (allFinite(pressureAbsolute)) {
        snapshot.extraScalarFields.emplace_back("pressure_absolute", std::move(pressureAbsolute));
      }
      if (allFinite(compressibleTemperature)) {
        snapshot.extraScalarFields.emplace_back("compressible_temperature",
                                                std::move(compressibleTemperature));
      }
      if (allFinite(mach)) snapshot.extraScalarFields.emplace_back("mach_number", std::move(mach));
    }
  }

  snapshot.nx = run.caseDefinition->mesh.nx;
  snapshot.ny = run.caseDefinition->mesh.ny;
  snapshot.uResidualHistory = result.uResidualHistory;
  snapshot.vResidualHistory = result.vResidualHistory;
  snapshot.pressureResidualHistory = result.pressureResidualHistory;
  snapshot.continuityResidualHistory = result.continuityHistory;
  snapshot.valid = true;
  return snapshot;
}

VisualizationSnapshot loadSnapshotFromResults(const std::filesystem::path& resultsDirectory) {
  VisualizationSnapshot snapshot;

  const std::filesystem::path metadataPath = resultsDirectory / "metadata.json";
  const std::filesystem::path fieldsPath = resultsDirectory / "fields.csv";
  const std::filesystem::path residualsPath = resultsDirectory / "residuals.csv";
  if (!std::filesystem::exists(metadataPath) || !std::filesystem::exists(fieldsPath) ||
      !std::filesystem::exists(residualsPath)) {
    return snapshot;
  }

  nlohmann::json metadata;
  try {
    std::ifstream metadataIn(metadataPath);
    metadataIn >> metadata;
    snapshot.nx = metadata.at("mesh").at("nx").get<Index>();
    snapshot.ny = metadata.at("mesh").at("ny").get<Index>();
  } catch (const std::exception&) {
    return VisualizationSnapshot{};
  }

  // --- fields.csv: cell_id,x,y,velocity_x,velocity_y,velocity_magnitude,
  // pressure[,temperature][,<any further named field>...]. Every column
  // beyond the first 7 is either "temperature" or (P6-PHYS-001/002/003)
  // one of species/multiphase/compressible's own named fields
  // (concentration_<name>, volume_fraction, mixture_density,
  // mixture_viscosity, density, pressure_absolute,
  // compressible_temperature, mach_number, ...) -- parsed generically by
  // column *name* here (matching CSVWriter.cpp's own "the caller
  // supplies the full column name, no fixed per-physics-module set"
  // convention) rather than hardcoding one column per physics module, so
  // a future physics module's own export needs no change here either.
  std::ifstream fieldsIn(fieldsPath);
  std::string headerLine;
  if (!std::getline(fieldsIn, headerLine)) return VisualizationSnapshot{};
  const std::vector<std::string> header = splitCsvLine(headerLine);
  if (header.size() < 7) return VisualizationSnapshot{};
  std::optional<std::size_t> temperatureColumn;
  std::vector<std::pair<std::string, std::size_t>> extraColumns;  // {name, column index}.
  for (std::size_t col = 7; col < header.size(); ++col) {
    if (header[col] == "temperature") {
      temperatureColumn = col;
    } else {
      extraColumns.emplace_back(header[col], col);
    }
  }

  std::vector<Real> temperatureValues;
  std::vector<std::vector<Real>> extraColumnValues(extraColumns.size());
  std::string line;
  while (std::getline(fieldsIn, line)) {
    if (line.empty()) continue;
    const std::vector<std::string> row = splitCsvLine(line);
    if (row.size() < 7) return VisualizationSnapshot{};
    try {
      const Real x = std::stod(row[1]);
      const Real y = std::stod(row[2]);
      snapshot.points.push_back(Vector2{x, y});
      snapshot.velocityX.push_back(std::stod(row[3]));
      snapshot.velocityY.push_back(std::stod(row[4]));
      snapshot.velocityMagnitude.push_back(std::stod(row[5]));
      snapshot.pressure.push_back(std::stod(row[6]));
      if (temperatureColumn.has_value() && row.size() > *temperatureColumn) {
        temperatureValues.push_back(std::stod(row[*temperatureColumn]));
      }
      for (std::size_t i = 0; i < extraColumns.size(); ++i) {
        const std::size_t col = extraColumns[i].second;
        if (row.size() > col) extraColumnValues[i].push_back(std::stod(row[col]));
      }
    } catch (const std::exception&) {
      return VisualizationSnapshot{};
    }
  }
  if (snapshot.points.empty()) return VisualizationSnapshot{};
  for (std::size_t i = 0; i < extraColumns.size(); ++i) {
    if (extraColumnValues[i].size() == snapshot.points.size()) {
      snapshot.extraScalarFields.emplace_back(extraColumns[i].first,
                                              std::move(extraColumnValues[i]));
    }
  }
  if (temperatureColumn.has_value() && temperatureValues.size() == snapshot.points.size()) {
    snapshot.temperature = std::move(temperatureValues);
  }

  // --- residuals.csv:
  // iteration,u_residual,v_residual,p_residual,continuity_residual,global_mass_imbalance
  std::ifstream residualsIn(residualsPath);
  std::string residualsHeader;
  if (!std::getline(residualsIn, residualsHeader)) return VisualizationSnapshot{};
  while (std::getline(residualsIn, line)) {
    if (line.empty()) continue;
    const std::vector<std::string> row = splitCsvLine(line);
    if (row.size() < 5) return VisualizationSnapshot{};
    try {
      snapshot.uResidualHistory.push_back(std::stod(row[1]));
      snapshot.vResidualHistory.push_back(std::stod(row[2]));
      snapshot.pressureResidualHistory.push_back(std::stod(row[3]));
      snapshot.continuityResidualHistory.push_back(std::stod(row[4]));
    } catch (const std::exception&) {
      return VisualizationSnapshot{};
    }
  }

  snapshot.valid = true;
  return snapshot;
}

}  // namespace cfd::app
