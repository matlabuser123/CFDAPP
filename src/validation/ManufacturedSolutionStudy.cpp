#include "cfd/validation/ManufacturedSolutionStudy.hpp"

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

Real normValue(const ErrorNorms& norms, NormKind kind) {
  switch (kind) {
    case NormKind::L1:
      return norms.l1;
    case NormKind::L2:
      return norms.l2;
    case NormKind::Linf:
      return norms.linf;
  }
  return norms.l2;
}

std::string cell(const std::optional<Real>& value, const char* format) {
  if (!value.has_value() || !std::isfinite(*value)) return "--";
  char buffer[48];
  std::snprintf(buffer, sizeof(buffer), format, *value);
  return buffer;
}

}  // namespace

std::string_view normKindName(NormKind kind) noexcept {
  switch (kind) {
    case NormKind::L1:
      return "l1";
    case NormKind::L2:
      return "l2";
    case NormKind::Linf:
      return "linf";
  }
  return "l2";
}

std::optional<Real> MMSOrder::finestOrder() const {
  if (triplets.empty()) return std::nullopt;
  return triplets.back().observedOrder;
}

bool MMSStudy::allGatesPassed() const {
  if (gates.empty()) return false;
  for (const auto& gate : gates) {
    if (!gate.passed) return false;
  }
  return true;
}

const ErrorNorms& MMSStudy::error(std::size_t level, const std::string& quantity) const {
  if (level >= levels.size()) {
    throw InvalidArgumentError("MMSStudy::error: level out of range");
  }
  for (const auto& [errorName, norms] : levels[level].errors) {
    if (errorName == quantity) return norms;
  }
  throw InvalidArgumentError("MMSStudy::error: level " + levels[level].name + " has no error '" +
                             quantity + "'");
}

MMSOrder computeMMSOrder(const MMSStudy& study, const std::string& quantity, NormKind norm,
                         const GridConvergenceOptions& options) {
  if (study.levels.size() < 3) {
    throw InvalidArgumentError("computeMMSOrder: at least 3 levels are required");
  }
  for (const auto& level : study.levels) {
    if (!level.accepted) {
      throw InvalidArgumentError("computeMMSOrder: level " + level.name +
                                 " was rejected -- no order from an invalid solve (" +
                                 level.rejectionReason + ")");
    }
  }
  MMSOrder order;
  order.quantity = quantity;
  order.norm = norm;
  order.formalOrder = options.formalOrder;
  std::vector<Real> values;
  for (std::size_t k = 0; k < study.levels.size(); ++k) {
    values.push_back(normValue(study.error(k, quantity), norm));
  }
  for (std::size_t k = 0; k + 1 < values.size(); ++k) {
    order.reductionFactors.push_back(
        values[k + 1] > 0.0 ? std::optional<Real>(values[k] / values[k + 1]) : std::nullopt);
  }
  for (std::size_t k = 0; k + 2 < values.size(); ++k) {
    order.triplets.push_back(analyzeGridConvergence(GridLevel{study.levels[k + 2].h, values[k + 2]},
                                                    GridLevel{study.levels[k + 1].h, values[k + 1]},
                                                    GridLevel{study.levels[k].h, values[k]},
                                                    options));
  }
  return order;
}

std::string mmsReportJson(const MMSStudy& study) {
  Json doc;
  doc["format_version"] = 1;
  doc["study"] = study.name;
  doc["description"] = study.description;
  doc["manufactured_solution"] = study.manufacturedSolution;
  doc["method"] = {
      {"error_norms",
       "volume-weighted over the selected cells: L1 = sum|e|V/sumV, L2 = sqrt(sum e^2 V/sumV), "
       "Linf = max|e| (cfd/validation/ErrorNorms.hpp)"},
      {"pressure_gauge",
       "numerical and exact pressure each shifted to zero volume-weighted mean over all cells "
       "before comparison"},
      {"observed_order",
       "P12-NUM-005 analyzeGridConvergence on each consecutive triplet of error values (exact "
       "limit 0); reduction factor = E_coarse/E_fine"},
      {"indexing", "levels listed coarse -> fine"}};
  Json coefficients = Json::object();
  for (const auto& [key, value] : study.coefficients) coefficients[key] = value;
  doc["coefficients"] = coefficients;
  Json configuration = Json::object();
  for (const auto& [key, value] : study.configuration) configuration[key] = value;
  doc["configuration"] = configuration;

  Json levels = Json::array();
  for (const auto& level : study.levels) {
    Json errors = Json::object();
    for (const auto& [name, norms] : level.errors) {
      errors[name] = {{"l1", norms.l1},
                      {"l2", norms.l2},
                      {"linf", norms.linf},
                      {"cells", norms.cells},
                      {"volume", norms.volume}};
    }
    Json diagnostics = Json::object();
    for (const auto& [name, value] : level.diagnostics) diagnostics[name] = optionalNumber(value);
    levels.push_back({{"name", level.name},
                      {"nx", level.nx},
                      {"ny", level.ny},
                      {"cells", level.cells},
                      {"h", level.h},
                      {"solver_status", level.solverStatus},
                      {"accepted", level.accepted},
                      {"rejection_reason", level.rejectionReason},
                      {"iterations", level.iterations},
                      {"mass_imbalance", optionalNumber(level.massImbalance)},
                      {"errors", errors},
                      {"diagnostics", diagnostics}});
  }
  doc["levels"] = levels;

  Json orders = Json::array();
  for (const auto& order : study.orders) {
    Json factors = Json::array();
    for (const auto& factor : order.reductionFactors) factors.push_back(optionalNumber(factor));
    Json triplets = Json::array();
    for (std::size_t k = 0; k < order.triplets.size(); ++k) {
      const auto& t = order.triplets[k];
      Json names = Json::array();
      for (std::size_t j = k; j < k + 3 && j < study.levels.size(); ++j) {
        names.push_back(study.levels[j].name);
      }
      triplets.push_back({{"levels", names},
                          {"status", std::string(gridConvergenceStatusName(t.status))},
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

  Json gates = Json::array();
  for (const auto& gate : study.gates) {
    gates.push_back({{"name", gate.name}, {"passed", gate.passed}, {"detail", gate.detail}});
  }
  doc["gates"] = gates;
  doc["all_gates_passed"] = study.allGatesPassed();

  Json runtimes = Json::array();
  for (const auto& level : study.levels) {
    runtimes.push_back({{"name", level.name}, {"seconds", level.runtimeSeconds}});
  }
  doc["runtime"] = {
      {"note", "measured wall time -- the only non-deterministic part of this report"},
      {"levels", runtimes}};
  return doc.dump(2) + "\n";
}

std::string mmsReportMarkdown(const MMSStudy& study) {
  std::ostringstream out;
  out << "### MMS: " << study.name << "\n\n";
  if (!study.description.empty()) out << study.description << "\n\n";
  if (!study.manufacturedSolution.empty())
    out << "Manufactured solution: " << study.manufacturedSolution << "\n\n";

  // Quantity names in first-level order.
  std::vector<std::string> quantities;
  if (!study.levels.empty()) {
    for (const auto& [name, norms] : study.levels.front().errors) quantities.push_back(name);
  }
  out << "| grid | cells | h | status | iterations | mass imbalance | runtime (s) |\n";
  out << "|---|---|---|---|---|---|---|\n";
  for (const auto& level : study.levels) {
    out << "| " << level.name << " | " << level.cells << " | " << cell(level.h, "%.5g") << " | "
        << level.solverStatus << (level.accepted ? "" : " (rejected)") << " | " << level.iterations
        << " | " << cell(level.massImbalance, "%.3g") << " | "
        << cell(std::optional<Real>(level.runtimeSeconds), "%.1f") << " |\n";
  }
  out << "\n";
  for (const auto& quantity : quantities) {
    out << "**" << quantity << "**\n\n| grid | L1 | L2 | Linf |\n|---|---|---|---|\n";
    for (std::size_t k = 0; k < study.levels.size(); ++k) {
      const ErrorNorms* norms = nullptr;
      for (const auto& [name, value] : study.levels[k].errors) {
        if (name == quantity) norms = &value;
      }
      if (norms == nullptr) continue;
      out << "| " << study.levels[k].name << " | " << cell(norms->l1, "%.4e") << " | "
          << cell(norms->l2, "%.4e") << " | " << cell(norms->linf, "%.4e") << " |\n";
    }
    out << "\n";
  }
  if (!study.orders.empty()) {
    out << "| quantity | norm | formal | reduction factors | observed p (per triplet) | status "
           "(finest) |\n";
    out << "|---|---|---|---|---|---|\n";
    for (const auto& order : study.orders) {
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
  for (const auto& gate : study.gates) {
    out << "- [" << (gate.passed ? "x" : " ") << "] " << gate.name << ": " << gate.detail << "\n";
  }
  out << "\n";
  return detail::tidyMarkdown(out.str());
}

void writeMMSReport(const std::string& path, const MMSStudy& study) {
  const std::filesystem::path file(path);
  if (file.has_parent_path()) {
    std::error_code ec;
    std::filesystem::create_directories(file.parent_path(), ec);
  }
  std::ofstream out(path, std::ios::binary);
  if (!out) throw IOError("writeMMSReport: cannot open " + path);
  out << mmsReportJson(study);
  if (!out) throw IOError("writeMMSReport: write failed for " + path);
}

}  // namespace cfd::validation
