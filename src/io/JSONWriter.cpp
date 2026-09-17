#include "cfd/io/JSONWriter.hpp"

#include <cmath>
#include <fstream>
#include <nlohmann/json.hpp>

#include "StructuredMeshInfo.hpp"
#include "cfd/core/Exception.hpp"
#include "cfd/physics/ContinuityEquation.hpp"

namespace cfd::io {

using cfd::Index;
using cfd::Real;
using cfd::Vector2;
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
    case SIMPLEStatus::Stagnated:
      return "Stagnated";
    case SIMPLEStatus::Diverging:
      return "Diverging";
  }
  return "Unknown";
}

// P12-MESH-004: JSON of the mesh-quality report (see JSONWriter.hpp).
nlohmann::json finiteOrNull(Real value) {
  return std::isfinite(value) ? nlohmann::json(value) : nlohmann::json(nullptr);
}

nlohmann::json pointJson(const Vector2& p) {
  return nlohmann::json::array({finiteOrNull(p.x), finiteOrNull(p.y)});
}

nlohmann::json metricJson(const cfd::mesh::MeshQualityMetric& m,
                          std::optional<Real> warningThreshold) {
  nlohmann::json j;
  j["count"] = m.count;
  if (m.count == 0) {
    for (const char* key : {"min", "max", "mean", "rms", "worst_id", "worst_location"}) {
      j[key] = nullptr;
    }
  } else {
    j["min"] = finiteOrNull(m.minimum);
    j["max"] = finiteOrNull(m.maximum);
    j["mean"] = finiteOrNull(m.mean);
    j["rms"] = finiteOrNull(m.rms);
    j["worst_id"] = m.worstId;
    j["worst_location"] = pointJson(m.worstLocation);
  }
  j["above_warning"] = m.aboveWarning;
  j["warning_threshold"] =
      warningThreshold.has_value() ? nlohmann::json(*warningThreshold) : nlohmann::json(nullptr);
  return j;
}

nlohmann::json meshQualityJson(const cfd::mesh::MeshQualityReport& q) {
  nlohmann::json j;
  j["status"] = cfd::mesh::meshQualityStatusName(q.status);
  j["cells"] = q.cellCount;
  j["faces"] = q.faceCount;
  j["internal_faces"] = q.internalFaceCount;
  j["boundary_faces"] = q.boundaryFaceCount;
  j["cell_area"] = metricJson(q.cellArea, std::nullopt);
  j["face_length"] = metricJson(q.faceLength, std::nullopt);
  j["aspect_ratio"] = metricJson(q.aspectRatio, q.thresholds.aspectRatioWarning);
  j["non_orthogonality_deg"] =
      metricJson(q.nonOrthogonality, q.thresholds.nonOrthogonalityWarningDegrees);
  j["skewness"] = metricJson(q.skewness, q.thresholds.skewnessWarning);
  j["expansion_ratio"] = metricJson(q.expansionRatio, q.thresholds.expansionRatioWarning);
  j["degenerate_cells"] = q.degenerateCells;
  j["invalid_faces"] = q.invalidFaces;
  j["connected_components"] = q.connectedComponents;
  nlohmann::json issues = nlohmann::json::array();
  for (const auto& issue : q.issues) {
    nlohmann::json i;
    i["severity"] = cfd::mesh::meshQualitySeverityName(issue.severity);
    i["metric"] = issue.metric;
    i["entity"] = issue.entity;
    i["id"] = issue.id.has_value() ? nlohmann::json(*issue.id) : nlohmann::json(nullptr);
    i["location"] =
        issue.location.has_value() ? pointJson(*issue.location) : nlohmann::json(nullptr);
    i["value"] = issue.value.has_value() ? finiteOrNull(*issue.value) : nlohmann::json(nullptr);
    i["threshold"] =
        issue.threshold.has_value() ? finiteOrNull(*issue.threshold) : nlohmann::json(nullptr);
    i["count"] = issue.count;
    i["message"] = issue.message;
    issues.push_back(std::move(i));
  }
  j["issues"] = std::move(issues);
  return j;
}

bool allFieldsFinite(const SIMPLEResult& result) {
  for (Index i = 0; i < result.velocity.size(); ++i) {
    if (!isFinite(result.velocity[i])) return false;
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
                               const std::optional<CompressibleRunMetadata>& compressible,
                               const std::optional<cfd::mesh::MeshQualityReport>& meshQuality) {
  // P12-MESH-006: a 3D mesh has no 2D structured layout; its grid (nx, ny,
  // nz and extents) is reported instead, plus the keys below that only a 3D
  // (or an explicitly non-linear face-flux) result carries -- a default 2D
  // result's metadata is exactly as before.
  const bool threeDimensional = mesh.dimension() == 3;
  const auto structured = threeDimensional ? detail::StructuredMeshInfo{0, 0, 0.0, 0.0}
                                           : detail::inferStructuredMeshInfo(mesh);

  nlohmann::json doc;
  doc["application"] = "CFDApp";
  doc["format_version"] = 1;
  doc["case"]["name"] = metadata.caseName;
  doc["mesh"]["cells"] = mesh.numberOfCells();
  doc["mesh"]["faces"] = mesh.numberOfFaces();
  if (threeDimensional) {
    doc["mesh"]["dimension"] = 3;
    const cfd::mesh::StructuredGrid* grid = mesh.structuredGrid();
    doc["mesh"]["nx"] = grid != nullptr ? grid->nx : 0;
    doc["mesh"]["ny"] = grid != nullptr ? grid->ny : 0;
    doc["mesh"]["nz"] = grid != nullptr ? grid->nz : 0;
    if (grid != nullptr && !grid->vertices.empty()) {
      const Vector3 extent = grid->vertices.back() - grid->vertices.front();
      doc["mesh"]["lx"] = extent.x;
      doc["mesh"]["ly"] = extent.y;
      doc["mesh"]["lz"] = extent.z;
    }
  } else {
    doc["mesh"]["nx"] = structured.nx;
    doc["mesh"]["ny"] = structured.ny;
  }
  // P12-MESH-003: a multi-block mesh reports nx = ny = 0 (not one grid) and
  // its blocks, in cell-id order.
  if (mesh.structuredBlocks().size() > 1) {
    nlohmann::json blocks = nlohmann::json::array();
    for (const auto& block : mesh.structuredBlocks()) {
      blocks.push_back({{"name", block.name}, {"nx", block.nx}, {"ny", block.ny}});
    }
    doc["mesh"]["blocks"] = std::move(blocks);
  }
  doc["physics"]["density"] = metadata.density;
  doc["physics"]["dynamic_viscosity"] = metadata.dynamicViscosity;
  doc["solver"]["type"] = metadata.solverType;
  doc["solver"]["converged"] = result.converged();
  doc["solver"]["status"] = std::string(statusName(result.status));
  doc["solver"]["iterations"] = result.iterations;
  if (threeDimensional || result.faceFlux != cfd::pressure_velocity::FaceFluxScheme::Linear) {
    doc["solver"]["face_flux"] =
        std::string(cfd::pressure_velocity::faceFluxSchemeName(result.faceFlux));
  }
  doc["residuals"]["u"] = result.finalUResidual;
  doc["residuals"]["v"] = result.finalVResidual;
  if (threeDimensional) doc["residuals"]["w"] = result.finalWResidual;
  doc["residuals"]["p"] = result.finalPressureResidual;
  doc["residuals"]["continuity"] = result.finalContinuityResidual;
  doc["conservation"]["global_mass_imbalance"] = result.globalMassImbalance;
  // P12-MESH-006: the global mass balance of the canonical corrected face flux.
  if (threeDimensional && result.massFlux.size() == mesh.numberOfFaces()) {
    const cfd::physics::MassBalance balance =
        cfd::physics::computeMassBalance(mesh, result.massFlux);
    auto& c = doc["conservation"];
    c["inflow"] = balance.inflow;
    c["outflow"] = balance.outflow;
    c["net_boundary_flux"] = balance.net;
    c["relative_imbalance"] = balance.relativeImbalance;
    c["max_cell_imbalance"] = balance.maxCellImbalance;
    c["rms_cell_imbalance"] = balance.rmsCellImbalance;
    c["flux_scale"] = balance.fluxScale;
    c["normalized_continuity"] = balance.normalizedContinuity;
  }

  // P12-NUM-004: additive diagnostics (always present; absent from
  // pre-P12-NUM-004 files, which readers must tolerate).
  const auto& robustness = result.robustness;
  auto& r = doc["robustness"];
  r["convergence_criterion"] =
      std::string(cfd::solver::convergenceCriterionName(robustness.convergenceCriterion));
  r["status_detail"] = robustness.statusDetail;
  const auto lastOrZero = [](const std::vector<Real>& history) {
    return history.empty() ? 0.0 : history.back();
  };
  r["normalized_residuals"]["u"] = lastOrZero(robustness.uNormalizedHistory);
  r["normalized_residuals"]["v"] = lastOrZero(robustness.vNormalizedHistory);
  if (threeDimensional) r["normalized_residuals"]["w"] = lastOrZero(robustness.wNormalizedHistory);
  r["normalized_residuals"]["p"] = lastOrZero(robustness.pressureNormalizedHistory);
  r["normalized_residuals"]["continuity"] = lastOrZero(robustness.continuityNormalizedHistory);
  r["final_velocity_relaxation"] = lastOrZero(robustness.velocityRelaxationHistory);
  r["final_pressure_relaxation"] = lastOrZero(robustness.pressureRelaxationHistory);
  r["relaxation_increases"] = robustness.relaxationIncreases;
  r["relaxation_decreases"] = robustness.relaxationDecreases;
  r["linear_solver_fallbacks"] = robustness.linearSolverFallbacks;
  r["linear_solver_fallback_recoveries"] = robustness.linearSolverFallbackRecoveries;
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

  if (meshQuality.has_value()) doc["mesh_quality"] = meshQualityJson(*meshQuality);

  std::ofstream out(path);
  if (!out) {
    throw IOError("Could not open output file for writing: " + path.string());
  }
  // 2-space indentation, single trailing newline -- the one convention
  // (section 15), not left to whatever dump()'s default happens to be.
  out << doc.dump(2) << '\n';
}

}  // namespace cfd::io
