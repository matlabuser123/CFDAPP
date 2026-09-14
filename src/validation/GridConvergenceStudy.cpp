#include "cfd/validation/GridConvergenceStudy.hpp"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include <sstream>

#include "MarkdownText.hpp"
#include "cfd/core/Exception.hpp"

namespace cfd::validation {

using cfd::pressure_velocity::SIMPLEResult;
using cfd::pressure_velocity::SIMPLEStatus;
using Json = nlohmann::ordered_json;

namespace {

constexpr std::array<const char*, 3> kLevelNames{"coarse", "medium", "fine"};

Json optionalNumber(const std::optional<Real>& value) {
  if (!value.has_value() || !std::isfinite(*value)) return Json(nullptr);
  return Json(*value);
}

std::string cell(const std::optional<Real>& value, const char* format = "%.6g") {
  if (!value.has_value()) return "--";
  char buffer[48];
  std::snprintf(buffer, sizeof(buffer), format, *value);
  return buffer;
}

std::string cell(Real value, const char* format = "%.6g") {
  return cell(std::optional<Real>(value), format);
}

std::optional<Real> findQuantity(const GridSolveOutput& output, const std::string& name) {
  for (const auto& [key, value] : output.quantities) {
    if (key == name) return value;
  }
  return std::nullopt;
}

}  // namespace

std::string_view simpleStatusName(SIMPLEStatus status) noexcept {
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

SolveAcceptance assessSimpleSolve(const SIMPLEResult& result, Real massImbalanceTolerance) {
  SolveAcceptance acceptance;
  acceptance.solverStatus = std::string(simpleStatusName(result.status));
  if (result.status != SIMPLEStatus::Converged) {
    acceptance.reason = "solver status " + acceptance.solverStatus + " (not Converged)";
    if (!result.robustness.statusDetail.empty()) {
      acceptance.reason += ": " + result.robustness.statusDetail;
    }
    return acceptance;
  }
  for (Index i = 0; i < result.velocity.size(); ++i) {
    if (!std::isfinite(result.velocity[i].x) || !std::isfinite(result.velocity[i].y)) {
      acceptance.reason = "non-finite velocity";
      return acceptance;
    }
  }
  for (Index i = 0; i < result.pressure.size(); ++i) {
    if (!std::isfinite(result.pressure[i])) {
      acceptance.reason = "non-finite pressure";
      return acceptance;
    }
  }
  for (Index f = 0; f < result.massFlux.size(); ++f) {
    if (!std::isfinite(result.massFlux[f])) {
      acceptance.reason = "non-finite face mass flux";
      return acceptance;
    }
  }
  if (!std::isfinite(result.globalMassImbalance) ||
      !(result.globalMassImbalance <= massImbalanceTolerance)) {
    char buffer[96];
    std::snprintf(buffer, sizeof(buffer), "global mass imbalance %.3g exceeds %.3g",
                  result.globalMassImbalance, massImbalanceTolerance);
    acceptance.reason = buffer;
    return acceptance;
  }
  acceptance.accepted = true;
  return acceptance;
}

GridConvergenceStudy analyzeGridStudy(std::string name, std::string description,
                                      std::vector<GridStudyEntry> coarseToFine,
                                      const std::vector<QuantitySpec>& quantities) {
  GridConvergenceStudy study;
  study.name = std::move(name);
  study.description = std::move(description);
  study.grids = std::move(coarseToFine);
  // The specific cause (a rejected solve) takes precedence over the generic
  // "not three grids" (which follows from the runner stopping early).
  study.allSolvesAccepted = study.grids.size() == 3;
  for (const auto& grid : study.grids) {
    if (!grid.output.acceptance.accepted) {
      study.allSolvesAccepted = false;
      if (study.rejectionReason.empty()) {
        study.rejectionReason =
            "grid " + grid.spec.name + " rejected: " + grid.output.acceptance.reason;
      }
    }
  }
  if (study.grids.size() != 3 && study.rejectionReason.empty()) {
    study.rejectionReason = "a three-grid study needs exactly 3 accepted grids, got " +
                            std::to_string(study.grids.size());
  }

  for (const auto& spec : quantities) {
    QuantityStudyResult quantity;
    quantity.spec = spec;
    for (std::size_t k = 0; k < study.grids.size() && k < 3; ++k) {
      if (study.grids[k].output.acceptance.accepted) {
        quantity.values[k] = findQuantity(study.grids[k].output, spec.name);
      }
      if (quantity.values[k].has_value() && spec.reference.has_value()) {
        quantity.errorVsReference[k] = *quantity.values[k] - *spec.reference;
        if (*spec.reference != 0.0) {
          quantity.relativeErrorVsReference[k] =
              std::abs(*quantity.errorVsReference[k]) / std::abs(*spec.reference);
        }
      }
    }
    if (!study.allSolvesAccepted) {
      quantity.analysis.status = GridConvergenceStatus::Invalid;
      quantity.analysis.convergence = ConvergenceClass::Invalid;
      quantity.analysis.diagnostic = "study rejected: " + study.rejectionReason;
      quantity.analysis.gridIndependenceReason = quantity.analysis.diagnostic;
    } else if (!quantity.values[0] || !quantity.values[1] || !quantity.values[2]) {
      quantity.analysis.status = GridConvergenceStatus::Invalid;
      quantity.analysis.convergence = ConvergenceClass::Invalid;
      quantity.analysis.diagnostic = "quantity '" + spec.name + "' missing from a grid's output";
      quantity.analysis.gridIndependenceReason = quantity.analysis.diagnostic;
    } else {
      quantity.analysis =
          analyzeGridConvergence(GridLevel{study.grids[2].h, *quantity.values[2]},
                                 GridLevel{study.grids[1].h, *quantity.values[1]},
                                 GridLevel{study.grids[0].h, *quantity.values[0]}, spec.options);
      if (spec.reference.has_value() && quantity.analysis.extrapolated21.has_value()) {
        quantity.extrapolatedErrorVsReference = *quantity.analysis.extrapolated21 - *spec.reference;
      }
    }
    study.quantities.push_back(std::move(quantity));
  }
  return study;
}

GridConvergenceStudy runGridConvergenceStudy(std::string name, std::string description,
                                             const std::array<GridSpec, 3>& coarseToFine,
                                             const GridSolveFunction& solve,
                                             const std::vector<QuantitySpec>& quantities) {
  std::vector<GridStudyEntry> entries;
  for (const auto& spec : coarseToFine) {
    GridStudyEntry entry;
    entry.spec = spec;
    entry.cells = spec.nx * spec.ny;
    entry.h = representativeGridSize(spec.lengthX * spec.lengthY, entry.cells);
    const auto start = std::chrono::steady_clock::now();
    entry.output = solve(spec);
    entry.runtimeSeconds =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    const bool accepted = entry.output.acceptance.accepted;
    entries.push_back(std::move(entry));
    if (!accepted) break;  // never analyse (or pay for) grids past an invalid solve
  }
  return analyzeGridStudy(std::move(name), std::move(description), std::move(entries), quantities);
}

std::string gridConvergenceReportJson(const GridConvergenceStudy& study) {
  Json doc;
  doc["format_version"] = 1;
  doc["study"] = study.name;
  doc["description"] = study.description;
  doc["method"] = {
      {"reference", "Celik et al., J. Fluids Eng. 130 (2008) 078001; Roache (1998)"},
      {"indexing", "1 = fine, 2 = medium, 3 = coarse"},
      {"grid_size", "h = sqrt(domain area / cells)"},
      {"relative_quantity_units", "fraction"},
      {"observed_order", "p solving eps32/eps21 = r21^p (r32^p - 1)/(r21^p - 1) (bisection)"},
      {"asymptotic_ratio", "U32 / (r21^pf U21), U = |eps| / (r^pf - 1), pf = formal order"}};
  doc["all_solves_accepted"] = study.allSolvesAccepted;
  doc["rejection_reason"] = study.rejectionReason;

  Json grids = Json::array();
  for (std::size_t k = 0; k < study.grids.size(); ++k) {
    const auto& g = study.grids[k];
    Json quantities = Json::object();
    for (const auto& [key, value] : g.output.quantities) quantities[key] = value;
    grids.push_back({{"level", k < 3 ? kLevelNames[k] : "extra"},
                     {"name", g.spec.name},
                     {"nx", g.spec.nx},
                     {"ny", g.spec.ny},
                     {"cells", g.cells},
                     {"h", g.h},
                     {"runtime_seconds", g.runtimeSeconds},
                     {"solver_status", g.output.acceptance.solverStatus},
                     {"solver_iterations", g.output.solverIterations},
                     {"accepted", g.output.acceptance.accepted},
                     {"rejection_reason", g.output.acceptance.reason},
                     {"quantities", quantities}});
  }
  doc["grids"] = grids;

  Json quantities = Json::array();
  for (const auto& q : study.quantities) {
    const auto& a = q.analysis;
    Json warnings = Json::array();
    for (const auto& w : a.warnings) warnings.push_back(w);
    Json values = {{"coarse", optionalNumber(q.values[0])},
                   {"medium", optionalNumber(q.values[1])},
                   {"fine", optionalNumber(q.values[2])}};
    Json reference = Json(nullptr);
    if (q.spec.reference.has_value()) {
      reference = {{"value", *q.spec.reference},
                   {"kind", q.spec.referenceKind},
                   {"error_coarse", optionalNumber(q.errorVsReference[0])},
                   {"error_medium", optionalNumber(q.errorVsReference[1])},
                   {"error_fine", optionalNumber(q.errorVsReference[2])},
                   {"relative_error_coarse", optionalNumber(q.relativeErrorVsReference[0])},
                   {"relative_error_medium", optionalNumber(q.relativeErrorVsReference[1])},
                   {"relative_error_fine", optionalNumber(q.relativeErrorVsReference[2])},
                   {"error_extrapolated", optionalNumber(q.extrapolatedErrorVsReference)}};
    }
    quantities.push_back(
        {{"name", q.spec.name},
         {"description", q.spec.description},
         {"values", values},
         {"reference", reference},
         {"analysis",
          {{"status", std::string(gridConvergenceStatusName(a.status))},
           {"convergence", std::string(convergenceClassName(a.convergence))},
           {"diagnostic", a.diagnostic},
           {"warnings", warnings},
           {"r21", a.r21},
           {"r32", a.r32},
           {"epsilon21", a.epsilon21},
           {"epsilon32", a.epsilon32},
           {"convergence_ratio", optionalNumber(a.convergenceRatio)},
           {"observed_order", optionalNumber(a.observedOrder)},
           {"formal_order", optionalNumber(q.spec.options.formalOrder)},
           {"order_ratio", optionalNumber(a.orderRatio)},
           {"richardson_extrapolated", optionalNumber(a.extrapolated21)},
           {"richardson_extrapolated_32", optionalNumber(a.extrapolated32)},
           {"approximate_relative_error_21", optionalNumber(a.approximateRelativeError21)},
           {"approximate_relative_error_32", optionalNumber(a.approximateRelativeError32)},
           {"extrapolated_relative_error_21", optionalNumber(a.extrapolatedRelativeError21)},
           {"extrapolated_relative_error_32", optionalNumber(a.extrapolatedRelativeError32)},
           {"uncertainty_21", optionalNumber(a.uncertainty21)},
           {"uncertainty_32", optionalNumber(a.uncertainty32)},
           {"gci_fine_medium", optionalNumber(a.gci21)},
           {"gci_medium_coarse", optionalNumber(a.gci32)},
           {"safety_factor", q.spec.options.safetyFactor},
           {"asymptotic_ratio", optionalNumber(a.asymptoticRatio)},
           {"asymptotic_tolerance", q.spec.options.asymptoticTolerance},
           {"oscillation_uncertainty", optionalNumber(a.oscillationUncertainty)},
           {"grid_independence_threshold",
            optionalNumber(q.spec.options.gridIndependenceThreshold)},
           {"grid_independent", a.gridIndependent},
           {"grid_independence_reason", a.gridIndependenceReason}}}});
  }
  doc["quantities"] = quantities;
  return doc.dump(2) + "\n";
}

std::string gridConvergenceReportMarkdown(const GridConvergenceStudy& study) {
  std::ostringstream out;
  out << "### Grid convergence: " << study.name << "\n\n";
  if (!study.description.empty()) out << study.description << "\n\n";
  out << "| grid | nx x ny | cells | h | runtime (s) | solver | accepted |\n";
  out << "|---|---|---|---|---|---|---|\n";
  for (const auto& g : study.grids) {
    out << "| " << g.spec.name << " | " << g.spec.nx << " x " << g.spec.ny << " | " << g.cells
        << " | " << cell(g.h) << " | " << cell(g.runtimeSeconds, "%.1f") << " | "
        << g.output.acceptance.solverStatus << " (" << g.output.solverIterations << " it) | "
        << (g.output.acceptance.accepted ? "yes" : "no: " + g.output.acceptance.reason) << " |\n";
  }
  out << "\n";
  if (!study.allSolvesAccepted) {
    out << "**Study rejected:** " << study.rejectionReason << "\n\n";
  }
  for (const auto& q : study.quantities) {
    const auto& a = q.analysis;
    out << "**" << q.spec.name << "**"
        << (q.spec.description.empty() ? "" : " -- " + q.spec.description) << "\n\n";
    out << "| grid | value | reference | error | relative error |\n|---|---|---|---|---|\n";
    for (std::size_t k = 0; k < 3; ++k) {
      out << "| " << kLevelNames[k] << " | " << cell(q.values[k], "%.8g") << " | "
          << (q.spec.reference ? cell(*q.spec.reference, "%.8g") + " (" + q.spec.referenceKind + ")"
                               : std::string("--"))
          << " | " << cell(q.errorVsReference[k], "%.4g") << " | "
          << cell(q.relativeErrorVsReference[k], "%.4g") << " |\n";
    }
    out << "| extrapolated | " << cell(a.extrapolated21, "%.8g") << " | | "
        << cell(q.extrapolatedErrorVsReference, "%.4g") << " | |\n\n";
    out << "| r21 | r32 | p | p formal | phi_ext | U21 (abs) | GCI21 | GCI32 | asymptotic ratio | "
           "status | grid independent |\n|---|---|---|---|---|---|---|---|---|---|---|\n";
    out << "| " << cell(a.r21, "%.4g") << " | " << cell(a.r32, "%.4g") << " | "
        << cell(a.observedOrder, "%.4f") << " | " << cell(q.spec.options.formalOrder, "%.3g")
        << " | " << cell(a.extrapolated21, "%.8g") << " | " << cell(a.uncertainty21, "%.4g")
        << " | " << cell(a.gci21, "%.4g") << " | " << cell(a.gci32, "%.4g") << " | "
        << cell(a.asymptoticRatio, "%.4f") << " | " << gridConvergenceStatusName(a.status) << " | "
        << (a.gridIndependent ? "yes" : "no") << " |\n\n";
    out << "- " << a.diagnostic << "\n- grid independence: " << a.gridIndependenceReason << "\n";
    for (const auto& w : a.warnings) out << "- warning: " << w << "\n";
    out << "\n";
  }
  return detail::tidyMarkdown(out.str());
}

void writeGridConvergenceReport(const std::string& path, const GridConvergenceStudy& study) {
  const std::filesystem::path file(path);
  if (file.has_parent_path()) {
    std::error_code ec;
    std::filesystem::create_directories(file.parent_path(), ec);
  }
  std::ofstream out(path, std::ios::binary);
  if (!out) throw IOError("writeGridConvergenceReport: cannot open " + path);
  out << gridConvergenceReportJson(study);
  if (!out) throw IOError("writeGridConvergenceReport: write failed for " + path);
}

std::vector<std::string> validateGridConvergenceReport(const std::string& jsonText) {
  std::vector<std::string> problems;
  const auto fail = [&](const std::string& message) { problems.push_back(message); };

  Json doc;
  try {
    doc = Json::parse(jsonText);
  } catch (const std::exception& e) {
    fail(std::string("not valid JSON: ") + e.what());
    return problems;
  }
  if (!doc.is_object()) {
    fail("top level is not an object");
    return problems;
  }

  // A value of the wrong JSON type makes nlohmann's typed accessors throw;
  // that is reported as a problem, never propagated.
  try {
    // Key presence / type helpers; `where` prefixes every message.
    const auto has = [&](const Json& obj, const char* key, const std::string& where) {
      if (!obj.contains(key)) {
        fail(where + ": missing key '" + key + "'");
        return false;
      }
      return true;
    };
    const auto requireString = [&](const Json& obj, const char* key, const std::string& where) {
      if (has(obj, key, where) && !obj[key].is_string()) fail(where + "." + key + ": not a string");
    };
    const auto requireBool = [&](const Json& obj, const char* key, const std::string& where) {
      if (has(obj, key, where) && !obj[key].is_boolean())
        fail(where + "." + key + ": not a boolean");
    };
    // A number (finite by construction: JSON has no NaN/inf) or, if allowed, null.
    const auto requireNumber = [&](const Json& obj, const char* key, const std::string& where,
                                   bool nullable) {
      if (!has(obj, key, where)) return;
      const Json& v = obj[key];
      if (v.is_number()) return;
      if (nullable && v.is_null()) return;
      fail(where + "." + key + (nullable ? ": not a number or null" : ": not a number"));
    };
    const auto number = [](const Json& obj, const char* key) -> std::optional<Real> {
      if (obj.contains(key) && obj[key].is_number()) return obj[key].get<Real>();
      return std::nullopt;
    };

    if (!doc.contains("format_version") || doc["format_version"] != 1) {
      fail("format_version: expected 1");
    }
    requireString(doc, "study", "report");
    requireString(doc, "description", "report");
    if (has(doc, "method", "report")) {
      const Json& method = doc["method"];
      if (!method.is_object()) {
        fail("method: not an object");
      } else {
        if (method.value("indexing", "") != "1 = fine, 2 = medium, 3 = coarse") {
          fail("method.indexing: expected '1 = fine, 2 = medium, 3 = coarse'");
        }
        if (method.value("relative_quantity_units", "") != "fraction") {
          fail("method.relative_quantity_units: expected 'fraction'");
        }
      }
    }
    requireBool(doc, "all_solves_accepted", "report");
    requireString(doc, "rejection_reason", "report");
    const bool allAccepted = doc.value("all_solves_accepted", false);
    if (allAccepted && !doc.value("rejection_reason", "").empty()) {
      fail("report: all_solves_accepted is true but rejection_reason is set");
    }
    if (!allAccepted && doc.value("rejection_reason", "").empty()) {
      fail("report: all_solves_accepted is false but rejection_reason is empty");
    }

    if (has(doc, "grids", "report")) {
      const Json& grids = doc["grids"];
      if (!grids.is_array()) {
        fail("grids: not an array");
      } else {
        if (allAccepted && grids.size() != 3) {
          fail("grids: all_solves_accepted requires exactly 3 grids, got " +
               std::to_string(grids.size()));
        }
        std::optional<Real> previousH;
        for (std::size_t k = 0; k < grids.size(); ++k) {
          const Json& g = grids[k];
          const std::string where = "grids[" + std::to_string(k) + "]";
          if (!g.is_object()) {
            fail(where + ": not an object");
            continue;
          }
          if (k < 3 && g.value("level", "") != kLevelNames[k]) {
            fail(where + ".level: expected '" + kLevelNames[k] +
                 "' (run order coarse, medium, fine)");
          }
          requireString(g, "name", where);
          requireNumber(g, "nx", where, false);
          requireNumber(g, "ny", where, false);
          requireNumber(g, "cells", where, false);
          requireNumber(g, "h", where, false);
          requireNumber(g, "runtime_seconds", where, false);
          requireString(g, "solver_status", where);
          requireNumber(g, "solver_iterations", where, false);
          requireBool(g, "accepted", where);
          requireString(g, "rejection_reason", where);
          const auto nx = number(g, "nx");
          const auto ny = number(g, "ny");
          const auto cells = number(g, "cells");
          if (nx && ny && cells && *cells != *nx * *ny) fail(where + ": cells != nx * ny");
          const auto h = number(g, "h");
          if (h && !(*h > 0.0)) fail(where + ".h: must be > 0");
          if (h && previousH && !(*h < *previousH)) {
            fail(where + ".h: must be smaller than the previous (coarser) grid's h");
          }
          previousH = h;
          if (allAccepted && !g.value("accepted", false)) {
            fail(where + ": all_solves_accepted is true but this grid is not accepted");
          }
          if (has(g, "quantities", where)) {
            if (!g["quantities"].is_object()) {
              fail(where + ".quantities: not an object");
            } else {
              for (const auto& [key, value] : g["quantities"].items()) {
                if (!value.is_number()) fail(where + ".quantities." + key + ": not a number");
              }
            }
          }
        }
      }
    }

    if (has(doc, "quantities", "report")) {
      const Json& quantities = doc["quantities"];
      if (!quantities.is_array()) {
        fail("quantities: not an array");
        return problems;
      }
      constexpr std::array<GridConvergenceStatus, 7> kStatuses{
          GridConvergenceStatus::Asymptotic,
          GridConvergenceStatus::MonotonicNotAsymptotic,
          GridConvergenceStatus::MonotonicAsymptoticRangeUnknown,
          GridConvergenceStatus::Oscillatory,
          GridConvergenceStatus::Divergent,
          GridConvergenceStatus::InsufficientSeparation,
          GridConvergenceStatus::Invalid};
      for (std::size_t i = 0; i < quantities.size(); ++i) {
        const Json& q = quantities[i];
        std::string where = "quantities[" + std::to_string(i) + "]";
        if (!q.is_object()) {
          fail(where + ": not an object");
          continue;
        }
        requireString(q, "name", where);
        if (q.contains("name") && q["name"].is_string())
          where += " (" + q["name"].get<std::string>() + ")";
        requireString(q, "description", where);
        if (has(q, "values", where)) {
          if (!q["values"].is_object()) {
            fail(where + ".values: not an object");
          } else {
            for (const char* level : kLevelNames)
              requireNumber(q["values"], level, where + ".values", true);
          }
        }
        if (has(q, "reference", where) && !q["reference"].is_null()) {
          const Json& ref = q["reference"];
          if (!ref.is_object()) {
            fail(where + ".reference: not an object or null");
          } else {
            requireNumber(ref, "value", where + ".reference", false);
            requireString(ref, "kind", where + ".reference");
          }
        }
        if (!has(q, "analysis", where)) continue;
        const Json& a = q["analysis"];
        const std::string aw = where + ".analysis";
        if (!a.is_object()) {
          fail(aw + ": not an object");
          continue;
        }
        std::optional<GridConvergenceStatus> status;
        requireString(a, "status", aw);
        const std::string statusName =
            a.contains("status") && a["status"].is_string() ? a["status"].get<std::string>() : "";
        for (const auto s : kStatuses) {
          if (gridConvergenceStatusName(s) == statusName) status = s;
        }
        if (!status && a.contains("status") && a["status"].is_string()) {
          fail(aw + ".status: unknown status '" + statusName + "'");
        }
        requireString(a, "convergence", aw);
        requireString(a, "diagnostic", aw);
        if (has(a, "warnings", aw) && !a["warnings"].is_array())
          fail(aw + ".warnings: not an array");
        for (const char* key :
             {"r21", "r32", "epsilon21", "epsilon32", "safety_factor", "asymptotic_tolerance"}) {
          requireNumber(a, key, aw, false);
        }
        constexpr std::array<const char*, 17> kNullable{"convergence_ratio",
                                                        "observed_order",
                                                        "formal_order",
                                                        "order_ratio",
                                                        "richardson_extrapolated",
                                                        "richardson_extrapolated_32",
                                                        "approximate_relative_error_21",
                                                        "approximate_relative_error_32",
                                                        "extrapolated_relative_error_21",
                                                        "extrapolated_relative_error_32",
                                                        "uncertainty_21",
                                                        "uncertainty_32",
                                                        "gci_fine_medium",
                                                        "gci_medium_coarse",
                                                        "asymptotic_ratio",
                                                        "oscillation_uncertainty",
                                                        "grid_independence_threshold"};
        for (const char* key : kNullable) requireNumber(a, key, aw, true);
        requireBool(a, "grid_independent", aw);
        requireString(a, "grid_independence_reason", aw);
        if (!status) continue;

        const bool monotonic = *status == GridConvergenceStatus::Asymptotic ||
                               *status == GridConvergenceStatus::MonotonicNotAsymptotic ||
                               *status == GridConvergenceStatus::MonotonicAsymptoticRangeUnknown;
        if (monotonic) {
          for (const char* key :
               {"observed_order", "richardson_extrapolated", "uncertainty_21", "uncertainty_32"}) {
            if (!number(a, key)) fail(aw + "." + key + ": required for status " + statusName);
          }
          if (const auto p = number(a, "observed_order"); p && !(*p > 0.0)) {
            fail(aw + ".observed_order: must be > 0");
          }
          for (const char* key :
               {"gci_fine_medium", "gci_medium_coarse", "uncertainty_21", "uncertainty_32"}) {
            if (const auto v = number(a, key); v && *v < 0.0)
              fail(aw + "." + key + ": must be >= 0");
          }
        } else {
          for (const char* key : {"observed_order", "richardson_extrapolated", "gci_fine_medium",
                                  "gci_medium_coarse", "asymptotic_ratio"}) {
            if (number(a, key)) fail(aw + "." + key + ": must be null for status " + statusName);
          }
        }
        if (*status == GridConvergenceStatus::Oscillatory &&
            !number(a, "oscillation_uncertainty")) {
          fail(aw + ".oscillation_uncertainty: required for status oscillatory");
        }
        if ((*status == GridConvergenceStatus::Asymptotic ||
             *status == GridConvergenceStatus::MonotonicNotAsymptotic) &&
            !number(a, "asymptotic_ratio")) {
          fail(aw + ".asymptotic_ratio: required for status " + statusName);
        }
        if (*status == GridConvergenceStatus::Asymptotic) {
          const auto ratio = number(a, "asymptotic_ratio");
          const auto tolerance = number(a, "asymptotic_tolerance");
          if (ratio && tolerance && !(std::abs(*ratio - 1.0) <= *tolerance)) {
            fail(aw +
                 ": status asymptotic but asymptotic_ratio is outside 1 +/- asymptotic_tolerance");
          }
        }
        if (!allAccepted && *status != GridConvergenceStatus::Invalid) {
          fail(aw + ".status: must be invalid when a solve was rejected");
        }
        if (a.value("grid_independent", false)) {
          const auto gci = number(a, "gci_fine_medium");
          const auto threshold = number(a, "grid_independence_threshold");
          if (*status != GridConvergenceStatus::Asymptotic) {
            fail(aw + ".grid_independent: true requires status asymptotic");
          } else if (!gci || !threshold || !(*gci <= *threshold)) {
            fail(aw +
                 ".grid_independent: true requires gci_fine_medium <= grid_independence_threshold");
          }
        }
      }
    }
  } catch (const std::exception& e) {
    fail(std::string("schema type error: ") + e.what());
  }
  return problems;
}

std::vector<std::string> validateGridConvergenceReportFile(const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) return {"cannot open " + path};
  std::ostringstream text;
  text << in.rdbuf();
  return validateGridConvergenceReport(text.str());
}

}  // namespace cfd::validation
