#pragma once

#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "cfd/core/Types.hpp"
#include "cfd/validation/ErrorNorms.hpp"
#include "cfd/validation/GridConvergence.hpp"

// P12-NUM-006 -- the record of a system-level method-of-manufactured-
// solutions (MMS) grid-refinement study and its deterministic reports.
// The study itself (analytical fields, forcing, solves) is the caller's;
// this holds what was measured, derives observed orders with the shared
// P12-NUM-005 three-grid analysis (analyzeGridConvergence -- no second
// order calculator), and writes JSON / Markdown.
namespace cfd::validation {

// One grid level of an MMS study.
struct MMSLevel {
  std::string name;  // e.g. "16x16"
  Index nx{0};
  Index ny{0};
  Index cells{0};
  Real h{0.0};  // representativeGridSize(area, cells)
  std::string solverStatus;
  bool accepted{false};  // the solve passed its validity gate
  std::string rejectionReason;
  Index iterations{0};
  std::optional<Real> massImbalance;
  // Measured errors, by name ("u", "u_interior", "p", "continuity", ...).
  std::vector<std::pair<std::string, ErrorNorms>> errors;
  // Further deterministic per-level scalars (e.g. "global_mass_imbalance").
  std::vector<std::pair<std::string, Real>> diagnostics;
  // Wall time; reported in the report's separate "runtime" block only.
  double runtimeSeconds{0.0};
};

enum class NormKind { L1, L2, Linf };
[[nodiscard]] std::string_view normKindName(NormKind kind) noexcept;

// Observed order of one error norm of one quantity. For every consecutive
// triplet of levels (coarse, medium, fine) the error values are analysed
// with analyzeGridConvergence -- the error E(h) is the "solution" whose
// grid-converged limit is 0 for a consistent scheme, so the reported
// extrapolated error doubles as a consistency check. reductionFactors[k] is
// E(level k) / E(level k+1) (a plain ratio, not an order).
struct MMSOrder {
  std::string quantity;
  NormKind norm{NormKind::L2};
  std::optional<Real> formalOrder;
  std::vector<std::optional<Real>> reductionFactors;
  std::vector<GridConvergenceResult> triplets;  // (0,1,2), (1,2,3), ...
  // The finest triplet's observed order, if it has one.
  [[nodiscard]] std::optional<Real> finestOrder() const;
};

struct MMSGate {
  std::string name;
  bool passed{false};
  std::string detail;
};

struct MMSStudy {
  std::string name;
  std::string description;
  std::string manufacturedSolution;                                // identifier + closed form
  std::vector<std::pair<std::string, Real>> coefficients;          // physical coefficients
  std::vector<std::pair<std::string, std::string>> configuration;  // schemes, BCs, gauge...
  std::vector<MMSLevel> levels;                                    // coarse -> fine
  std::vector<MMSOrder> orders;
  std::vector<MMSGate> gates;

  [[nodiscard]] bool allGatesPassed() const;
  // The named error of a level; throws InvalidArgumentError if absent.
  [[nodiscard]] const ErrorNorms& error(std::size_t level, const std::string& quantity) const;
};

// Computes (and returns) the MMSOrder of `quantity`/`norm` over the
// study's levels. Every level must be accepted and carry the quantity,
// and there must be >= 3 levels -- otherwise InvalidArgumentError (no
// order is ever derived from a rejected solve). `options` is forwarded to
// analyzeGridConvergence (its formalOrder drives the asymptotic check).
[[nodiscard]] MMSOrder computeMMSOrder(const MMSStudy& study, const std::string& quantity,
                                       NormKind norm, const GridConvergenceOptions& options = {});

// Deterministic reports: identical text for an identical study, except the
// separate "runtime" block (JSON) / runtime column (Markdown). Unset
// values are JSON null, never NaN.
[[nodiscard]] std::string mmsReportJson(const MMSStudy& study);
[[nodiscard]] std::string mmsReportMarkdown(const MMSStudy& study);
// Writes mmsReportJson (parent directories created); throws IOError.
void writeMMSReport(const std::string& path, const MMSStudy& study);

}  // namespace cfd::validation
