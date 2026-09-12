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
    case SIMPLEStatus::Cancelled:
      return "Cancelled";
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
                               const Mesh& mesh, const SIMPLEResult& result,
                               const std::optional<ThermalRunMetadata>& thermal,
                               const std::vector<SpeciesRunMetadata>& species,
                               const std::optional<MultiphaseRunMetadata>& multiphase,
                               const std::optional<CompressibleRunMetadata>& compressible) {
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

  // P2-THERMAL-004: "thermal.enabled" is always present (a documented
  // default for every nonthermal case, matching every other field here --
  // never a silently-absent key), the rest only when thermal is enabled.
  doc["thermal"]["enabled"] = thermal.has_value();
  if (thermal.has_value()) {
    doc["thermal"]["conductivity"] = thermal->conductivity;
    doc["thermal"]["specific_heat"] = thermal->specificHeat;
    doc["thermal"]["status"] = thermal->status;
    doc["thermal"]["converged"] = thermal->converged;
    doc["thermal"]["iterations"] = thermal->iterations;
    doc["thermal"]["final_residual"] = thermal->finalResidual;
  }

  // P6-PHYS-001: always a JSON array (possibly empty) -- see
  // writeMetadata's own header comment on why a list-shaped field needs
  // no separate "enabled" flag.
  doc["species"] = nlohmann::json::array();
  for (const SpeciesRunMetadata& s : species) {
    doc["species"].push_back({{"name", s.name},
                              {"diffusivity", s.diffusivity},
                              {"status", s.status},
                              {"converged", s.converged},
                              {"iterations", s.iterations},
                              {"final_residual", s.finalResidual}});
  }

  // P6-PHYS-002: "multiphase.enabled" is always present, same convention
  // as "thermal.enabled" above.
  doc["multiphase"]["enabled"] = multiphase.has_value();
  if (multiphase.has_value()) {
    doc["multiphase"]["phase1"]["name"] = multiphase->phase1Name;
    doc["multiphase"]["phase1"]["density"] = multiphase->phase1Density;
    doc["multiphase"]["phase1"]["viscosity"] = multiphase->phase1Viscosity;
    doc["multiphase"]["phase2"]["name"] = multiphase->phase2Name;
    doc["multiphase"]["phase2"]["density"] = multiphase->phase2Density;
    doc["multiphase"]["phase2"]["viscosity"] = multiphase->phase2Viscosity;
    doc["multiphase"]["status"] = multiphase->status;
    doc["multiphase"]["converged"] = multiphase->converged;
    doc["multiphase"]["conservation"]["phase1_volume"] = multiphase->phase1Volume;
  }

  // P6-PHYS-003: "compressible.enabled" is always present, same
  // convention as "thermal.enabled" above.
  doc["compressible"]["enabled"] = compressible.has_value();
  if (compressible.has_value()) {
    doc["compressible"]["gas_constant"] = compressible->gasConstant;
    doc["compressible"]["specific_heat_pressure"] = compressible->specificHeatPressure;
    doc["compressible"]["reference_pressure"] = compressible->referencePressure;
    doc["compressible"]["thermal_coupled"] = compressible->thermalCoupled;
    doc["compressible"]["coupled"] = compressible->coupled;
    doc["compressible"]["status"] = compressible->status;
    doc["compressible"]["mach_max"] = compressible->machMax;
    doc["compressible"]["global_continuity_imbalance"] = compressible->globalContinuityImbalance;
  }

  std::ofstream out(path);
  if (!out) {
    throw IOError("Could not open output file for writing: " + path.string());
  }
  // 2-space indentation, single trailing newline -- the one convention
  // (section 15), not left to whatever dump()'s default happens to be.
  out << doc.dump(2) << '\n';
}

}  // namespace cfd::io
