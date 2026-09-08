#include "cfd/io/JSONWriter.hpp"

#include <cmath>
#include <fstream>
#include <nlohmann/json.hpp>

#include "StructuredMeshInfo.hpp"
#include "cfd/core/Exception.hpp"

namespace cfd::io {

using cfd::Index;
using cfd::mesh::Mesh;
using cfd::pressure_velocity::SIMPLEResult;
using cfd::pressure_velocity::SIMPLEStatus;

namespace {

// The exact enum names, not a second invented vocabulary (P1 -- Result
// Export section 12).
std::string_view statusName(SIMPLEStatus status) {
  switch (status) {
    case SIMPLEStatus::Converged:
      return "Converged";
    case SIMPLEStatus::MaxIterations:
      return "MaxIterations";
    case SIMPLEStatus::MomentumFailure:
      return "MomentumFailure";
    case SIMPLEStatus::PressureCorrectionFailure:
      return "PressureCorrectionFailure";
    case SIMPLEStatus::NonFiniteState:
      return "NonFiniteState";
    case SIMPLEStatus::InvalidConfiguration:
      return "InvalidConfiguration";
  }
  return "Unknown";
}

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

void JSONWriter::writeMetadata(const std::filesystem::path& path, const RunMetadata& metadata,
                               const Mesh& mesh, const SIMPLEResult& result) {
  const auto structured = detail::inferStructuredMeshInfo(mesh);

  nlohmann::json doc;
  doc["application"] = "CFDApp";
  doc["format_version"] = 1;
  doc["case"]["name"] = metadata.caseName;
  doc["mesh"]["cells"] = mesh.numberOfCells();
  doc["mesh"]["faces"] = mesh.numberOfFaces();
  doc["mesh"]["nx"] = structured.nx;
  doc["mesh"]["ny"] = structured.ny;
  doc["physics"]["density"] = metadata.density;
  doc["physics"]["dynamic_viscosity"] = metadata.dynamicViscosity;
  doc["solver"]["type"] = metadata.solverType;
  doc["solver"]["converged"] = result.converged();
  doc["solver"]["status"] = std::string(statusName(result.status));
  doc["solver"]["iterations"] = result.iterations;
  doc["residuals"]["u"] = result.finalUResidual;
  doc["residuals"]["v"] = result.finalVResidual;
  doc["residuals"]["p"] = result.finalPressureResidual;
  doc["residuals"]["continuity"] = result.finalContinuityResidual;
  doc["conservation"]["global_mass_imbalance"] = result.globalMassImbalance;
  doc["numerics"]["finite"] = allFieldsFinite(result);

  std::ofstream out(path);
  if (!out) {
    throw IOError("Could not open output file for writing: " + path.string());
  }
  // 2-space indentation, single trailing newline -- the one convention
  // (section 15), not left to whatever dump()'s default happens to be.
  out << doc.dump(2) << '\n';
}

}  // namespace cfd::io
