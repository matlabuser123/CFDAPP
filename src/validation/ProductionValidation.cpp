#include "cfd/validation/ProductionValidation.hpp"

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include <sstream>

#include "MarkdownText.hpp"
#include "cfd/core/Exception.hpp"

namespace cfd::validation {

using Json = nlohmann::ordered_json;

namespace {

Json optionalNumber(const std::optional<Real>& value) {
  if (!value.has_value() || !std::isfinite(*value)) return Json(nullptr);
  return Json(*value);
}

std::string cell(const std::optional<Real>& value, const char* format) {
  if (!value.has_value() || !std::isfinite(*value)) return "--";
  char buffer[48];
  std::snprintf(buffer, sizeof(buffer), format, *value);
  return buffer;
}

Json checksJson(const std::vector<ValidationCheck>& checks) {
  Json out = Json::array();
  for (const auto& check : checks) {
    out.push_back({{"name", check.name}, {"passed", check.passed}, {"detail", check.detail}});
  }
  return out;
}

void checksMarkdown(std::ostringstream& out, const std::vector<ValidationCheck>& checks,
                    const std::string& prefix) {
  for (const auto& check : checks) {
    out << "- [" << (check.passed ? "x" : " ") << "] " << prefix << check.name << ": "
        << check.detail << "\n";
  }
}

}  // namespace

std::string ValidationRun::id() const { return caseName + "/" + level.name + "/" + scheme; }

bool ValidationRun::passed() const {
  if (!level.accepted) return false;
  for (const auto& check : checks) {
    if (!check.passed) return false;
  }
  return true;
}

bool ValidationReport::passed() const {
  if (runs.empty()) return false;
  for (const auto& r : runs) {
    if (!r.passed()) return false;
  }
  for (const auto& gate : gates) {
    if (!gate.passed) return false;
  }
  return true;
}

const ValidationRun& ValidationReport::run(const std::string& id) const {
  for (const auto& r : runs) {
    if (r.id() == id) return r;
  }
  throw InvalidArgumentError("ValidationReport::run: no run '" + id + "' in " + name);
}

ValidationRun makeSimpleValidationRun(std::string caseName, std::optional<Real> reynolds,
                                      std::string scheme, const GridSpec& grid,
                                      const pressure_velocity::SIMPLEResult& result,
                                      Real massImbalanceTolerance, double runtimeSeconds) {
  ValidationRun run;
  run.caseName = std::move(caseName);
  run.reynolds = reynolds;
  run.scheme = std::move(scheme);
  MMSLevel& level = run.level;
  level.name =
      grid.name.empty() ? std::to_string(grid.nx) + "x" + std::to_string(grid.ny) : grid.name;
  level.nx = grid.nx;
  level.ny = grid.ny;
  level.cells = grid.nx * grid.ny;
  level.h = representativeGridSize(grid.lengthX * grid.lengthY, level.cells);
  const SolveAcceptance acceptance = assessSimpleSolve(result, massImbalanceTolerance);
  level.solverStatus = acceptance.solverStatus;
  level.accepted = acceptance.accepted;
  level.rejectionReason = acceptance.reason;
  level.iterations = result.iterations;
  if (std::isfinite(result.globalMassImbalance)) level.massImbalance = result.globalMassImbalance;
  level.diagnostics = {
      {"momentum_linear_iterations", static_cast<Real>(result.momentumLinearIterations)},
      {"pressure_linear_iterations", static_cast<Real>(result.pressureLinearIterations)},
      {"linear_solver_fallbacks", static_cast<Real>(result.robustness.linearSolverFallbacks)},
      {"final_u_residual", result.finalUResidual},
      {"final_v_residual", result.finalVResidual},
      {"final_pressure_residual", result.finalPressureResidual},
      {"final_continuity_residual", result.finalContinuityResidual}};
  level.runtimeSeconds = runtimeSeconds;
  return run;
}

GridStudyEntry toGridStudyEntry(const ValidationRun& run, Real lengthX, Real lengthY,
                                const std::vector<std::string>& quantityNames) {
  GridStudyEntry entry;
  entry.spec = GridSpec{run.level.name, run.level.nx, run.level.ny, lengthX, lengthY};
  entry.cells = run.level.cells;
  entry.h = run.level.h;
  entry.runtimeSeconds = run.level.runtimeSeconds;
  entry.output.acceptance.accepted = run.level.accepted;
  entry.output.acceptance.solverStatus = run.level.solverStatus;
  entry.output.acceptance.reason = run.level.rejectionReason;
  entry.output.solverIterations = run.level.iterations;
  for (const auto& name : quantityNames) {
    for (const auto& [key, value] : run.level.diagnostics) {
      if (key == name) entry.output.quantities.emplace_back(key, value);
    }
  }
  return entry;
}

MMSOrder computeRunSequenceOrder(const std::vector<const ValidationRun*>& coarseToFine,
                                 const std::string& quantity, NormKind norm,
                                 const GridConvergenceOptions& options) {
  MMSStudy sequence;
  for (const ValidationRun* r : coarseToFine) {
    if (r == nullptr) throw InvalidArgumentError("computeRunSequenceOrder: null run");
    sequence.levels.push_back(r->level);
  }
  return computeMMSOrder(sequence, quantity, norm, options);
}

std::string validationReportJson(const ValidationReport& report) {
  Json doc;
  doc["format_version"] = 1;
  doc["kind"] = "production_validation";
  doc["study"] = report.name;
  doc["description"] = report.description;
  doc["reference"] = report.reference;
  doc["method"] = {
      {"error_norms",
       "against sample-point reference data (benchmark tables): unweighted L1 = mean|e|, "
       "L2 = sqrt(mean e^2), Linf = max|e| over the tabulated stations; against a field: "
       "volume-weighted (cfd/validation/ErrorNorms.hpp)"},
      {"solve_acceptance",
       "a run is accepted only if SIMPLE Converged, every field is finite and the global mass "
       "imbalance is within tolerance (P12-NUM-005 assessSimpleSolve)"},
      {"grid_convergence",
       "P12-NUM-005 three-grid study of this code's own solutions (observed order, Richardson "
       "extrapolation, GCI) -- separate from the error against the reference"},
      {"indexing", "runs listed in execution order; grid sequences coarse -> fine"}};
  Json coefficients = Json::object();
  for (const auto& [key, value] : report.coefficients) coefficients[key] = value;
  doc["coefficients"] = coefficients;
  Json configuration = Json::object();
  for (const auto& [key, value] : report.configuration) configuration[key] = value;
  doc["configuration"] = configuration;

  Json runs = Json::array();
  for (const auto& r : report.runs) {
    const MMSLevel& level = r.level;
    Json errors = Json::object();
    for (const auto& [name, norms] : level.errors) {
      errors[name] = {{"l1", norms.l1},
                      {"l2", norms.l2},
                      {"linf", norms.linf},
                      {"samples", norms.cells},
                      {"weight", norms.volume}};
    }
    Json diagnostics = Json::object();
    for (const auto& [name, value] : level.diagnostics) diagnostics[name] = optionalNumber(value);
    runs.push_back({{"id", r.id()},
                    {"case", r.caseName},
                    {"reynolds", optionalNumber(r.reynolds)},
                    {"mesh", level.name},
                    {"nx", level.nx},
                    {"ny", level.ny},
                    {"cells", level.cells},
                    {"h", level.h},
                    {"scheme", r.scheme},
                    {"solver_status", level.solverStatus},
                    {"accepted", level.accepted},
                    {"rejection_reason", level.rejectionReason},
                    {"iterations", level.iterations},
                    {"mass_imbalance", optionalNumber(level.massImbalance)},
                    {"errors", errors},
                    {"diagnostics", diagnostics},
                    {"checks", checksJson(r.checks)},
                    {"passed", r.passed()}});
  }
  doc["runs"] = runs;

  Json orders = Json::array();
  for (const auto& order : report.orders) {
    Json factors = Json::array();
    for (const auto& factor : order.reductionFactors) factors.push_back(optionalNumber(factor));
    Json triplets = Json::array();
    for (const auto& t : order.triplets) {
      triplets.push_back({{"status", std::string(gridConvergenceStatusName(t.status))},
                          {"observed_order", optionalNumber(t.observedOrder)},
                          {"asymptotic_ratio", optionalNumber(t.asymptoticRatio)},
                          {"extrapolated_error", optionalNumber(t.extrapolated21)},
                          {"diagnostic", t.diagnostic}});
    }
    orders.push_back({{"quantity", order.quantity},
                      {"norm", std::string(normKindName(order.norm))},
                      {"formal_order", optionalNumber(order.formalOrder)},
                      {"reduction_factors", factors},
                      {"triplets", triplets},
                      {"finest_observed_order", optionalNumber(order.finestOrder())}});
  }
  doc["orders"] = orders;

  Json studies = Json::array();
  for (const auto& study : report.gridConvergence) {
    studies.push_back(Json::parse(gridConvergenceReportJson(study)));
  }
  doc["grid_convergence"] = studies;

  doc["gates"] = checksJson(report.gates);
  Json limitations = Json::array();
  for (const auto& text : report.limitations) limitations.push_back(text);
  doc["limitations"] = limitations;
  doc["passed"] = report.passed();

  Json runtimes = Json::array();
  for (const auto& r : report.runs) {
    runtimes.push_back({{"id", r.id()}, {"seconds", r.level.runtimeSeconds}});
  }
  doc["runtime"] = {
      {"note",
       "measured wall time -- with grid_convergence[*].grids[*].runtime_seconds, the only "
       "non-deterministic part of this report"},
      {"runs", runtimes}};
  return doc.dump(2) + "\n";
}

std::string validationReportMarkdown(const ValidationReport& report) {
  std::ostringstream out;
  out << "### Validation: " << report.name << "\n\n";
  if (!report.description.empty()) out << report.description << "\n\n";
  if (!report.reference.empty()) out << "Reference: " << report.reference << "\n\n";

  out << "| run | Re | mesh | scheme | status | iterations | linear its (mom / p) | mass "
         "imbalance | runtime (s) | passed |\n";
  out << "|---|---|---|---|---|---|---|---|---|---|\n";
  const auto diagnostic = [](const MMSLevel& level, const char* name) -> std::optional<Real> {
    for (const auto& [key, value] : level.diagnostics) {
      if (key == name) return value;
    }
    return std::nullopt;
  };
  for (const auto& r : report.runs) {
    const MMSLevel& level = r.level;
    out << "| " << r.id() << " | " << cell(r.reynolds, "%.6g") << " | " << level.name << " | "
        << r.scheme << " | " << level.solverStatus << (level.accepted ? "" : " (rejected)") << " | "
        << level.iterations << " | "
        << cell(diagnostic(level, "momentum_linear_iterations"), "%.0f") << " / "
        << cell(diagnostic(level, "pressure_linear_iterations"), "%.0f") << " | "
        << cell(level.massImbalance, "%.3g") << " | "
        << cell(std::optional<Real>(level.runtimeSeconds), "%.1f") << " | "
        << (r.passed() ? "yes" : "NO") << " |\n";
  }
  out << "\n";

  // Error quantities, in first-appearance order across runs.
  std::vector<std::string> quantities;
  for (const auto& r : report.runs) {
    for (const auto& [name, norms] : r.level.errors) {
      bool known = false;
      for (const auto& q : quantities) known = known || q == name;
      if (!known) quantities.push_back(name);
    }
  }
  for (const auto& quantity : quantities) {
    out << "**" << quantity << "**\n\n| run | L1 | L2 | Linf |\n|---|---|---|---|\n";
    for (const auto& r : report.runs) {
      for (const auto& [name, norms] : r.level.errors) {
        if (name != quantity) continue;
        out << "| " << r.id() << " | " << cell(norms.l1, "%.4e") << " | " << cell(norms.l2, "%.4e")
            << " | " << cell(norms.linf, "%.4e") << " |\n";
      }
    }
    out << "\n";
  }

  if (!report.orders.empty()) {
    out << "| quantity | norm | formal | reduction factors | observed p (per triplet) | status "
           "(finest) |\n";
    out << "|---|---|---|---|---|---|\n";
    for (const auto& order : report.orders) {
      std::string factors;
      for (const auto& f : order.reductionFactors)
        factors += (factors.empty() ? "" : ", ") + cell(f, "%.3f");
      std::string ps;
      for (const auto& t : order.triplets)
        ps += (ps.empty() ? "" : ", ") + cell(t.observedOrder, "%.3f");
      out << "| " << order.quantity << " | " << normKindName(order.norm) << " | "
          << cell(order.formalOrder, "%.3g") << " | " << factors << " | " << ps << " | "
          << (order.triplets.empty()
                  ? std::string("--")
                  : std::string(gridConvergenceStatusName(order.triplets.back().status)))
          << " |\n";
    }
    out << "\n";
  }

  for (const auto& study : report.gridConvergence) {
    out << gridConvergenceReportMarkdown(study) << "\n";
  }

  for (const auto& r : report.runs) checksMarkdown(out, r.checks, r.id() + ": ");
  checksMarkdown(out, report.gates, "");
  if (!report.limitations.empty()) {
    out << "\nLimitations:\n\n";
    for (const auto& text : report.limitations) out << "- " << text << "\n";
  }
  out << "\nOverall: " << (report.passed() ? "PASSED" : "FAILED") << "\n\n";
  return detail::tidyMarkdown(out.str());
}

void writeValidationReport(const std::string& path, const ValidationReport& report) {
  const std::filesystem::path file(path);
  if (file.has_parent_path()) {
    std::error_code ec;
    std::filesystem::create_directories(file.parent_path(), ec);
  }
  std::ofstream out(path, std::ios::binary);
  if (!out) throw IOError("writeValidationReport: cannot open " + path);
  out << validationReportJson(report);
  if (!out) throw IOError("writeValidationReport: write failed for " + path);
}

}  // namespace cfd::validation
