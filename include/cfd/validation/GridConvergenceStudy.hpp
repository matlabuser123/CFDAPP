#pragma once

#include <array>
#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "cfd/core/Types.hpp"
#include "cfd/pressure_velocity/SIMPLEResult.hpp"
#include "cfd/validation/GridConvergence.hpp"

// P12-NUM-005 -- the automated three-grid study around analyzeGridConvergence:
// run coarse / medium / fine through a caller-supplied solve function, gate
// every grid on the validity of its CFD solve, analyse each quantity of
// interest, and emit deterministic JSON and Markdown reports.
namespace cfd::validation {

// One structured 2D grid of the study. h is derived, not assumed:
// representativeGridSize(lengthX * lengthY, nx * ny).
struct GridSpec {
  std::string name;  // e.g. "coarse", "medium", "fine"
  Index nx{0};
  Index ny{0};
  Real lengthX{0.0};
  Real lengthY{0.0};
};

// Solver-status gate for one grid (P12-NUM-004 statuses included).
struct SolveAcceptance {
  bool accepted{false};
  std::string solverStatus;
  std::string reason;  // empty when accepted
};

// A SIMPLE solve may take part in a grid study only if it Converged (not
// MaxIterations, Stagnated, Diverging, a linear-solver failure, ...), every
// velocity/pressure/face-flux value is finite, and the global mass imbalance
// is finite and <= massImbalanceTolerance.
[[nodiscard]] SolveAcceptance assessSimpleSolve(const pressure_velocity::SIMPLEResult& result,
                                                Real massImbalanceTolerance);

[[nodiscard]] std::string_view simpleStatusName(pressure_velocity::SIMPLEStatus status) noexcept;

// What a grid's solve function returns: the gate and the quantities.
struct GridSolveOutput {
  SolveAcceptance acceptance;
  std::vector<std::pair<std::string, Real>> quantities;  // name -> value
  Index solverIterations{0};
};

// One grid of a completed study (the runner fills cells, h and runtime).
struct GridStudyEntry {
  GridSpec spec;
  Index cells{0};
  Real h{0.0};
  double runtimeSeconds{0.0};
  GridSolveOutput output;
};

// A quantity of interest to analyse. `reference` is optional: an exact
// analytical value ("analytical") or published benchmark data ("benchmark").
// The grid-convergence analysis never uses it -- it is reported separately
// as the per-grid error against the reference.
struct QuantitySpec {
  std::string name;
  std::string description;
  std::optional<Real> reference;
  std::string referenceKind;  // "analytical" | "benchmark" | ""
  GridConvergenceOptions options;
};

struct QuantityStudyResult {
  QuantitySpec spec;
  std::array<std::optional<Real>, 3> values;  // coarse, medium, fine (run order)
  GridConvergenceResult analysis;
  // Error of each grid against the reference (coarse, medium, fine), when
  // a reference exists: value - reference, and |value - reference| / |reference|.
  std::array<std::optional<Real>, 3> errorVsReference;
  std::array<std::optional<Real>, 3> relativeErrorVsReference;
  // The extrapolated value's error against the reference.
  std::optional<Real> extrapolatedErrorVsReference;
};

struct GridConvergenceStudy {
  std::string name;
  std::string description;
  std::vector<GridStudyEntry> grids;  // run order: coarse, medium, fine
  bool allSolvesAccepted{false};
  std::string rejectionReason;  // first rejected grid, if any
  std::vector<QuantityStudyResult> quantities;
};

using GridSolveFunction = std::function<GridSolveOutput(const GridSpec&)>;

// Runs grids[0] (coarse), grids[1] (medium), grids[2] (fine) in that order,
// timing each. Stops at the first grid whose acceptance fails: no
// statistics are ever computed from an invalid CFD solve -- every quantity
// is then reported with status Invalid and the rejection reason. A
// quantity missing from any grid's output is likewise Invalid.
[[nodiscard]] GridConvergenceStudy runGridConvergenceStudy(
    std::string name, std::string description, const std::array<GridSpec, 3>& coarseToFine,
    const GridSolveFunction& solve, const std::vector<QuantitySpec>& quantities);

// Builds the study from already-computed grid entries (no solving) --
// used by tests and by callers that obtained the three solves elsewhere.
[[nodiscard]] GridConvergenceStudy analyzeGridStudy(std::string name, std::string description,
                                                    std::vector<GridStudyEntry> coarseToFine,
                                                    const std::vector<QuantitySpec>& quantities);

// Deterministic reports: the same study always gives byte-identical text
// (fixed key order, shortest round-trip number formatting; unset optional
// values are JSON null). JSON schema: see docs/validation/grid_convergence.md.
[[nodiscard]] std::string gridConvergenceReportJson(const GridConvergenceStudy& study);
[[nodiscard]] std::string gridConvergenceReportMarkdown(const GridConvergenceStudy& study);

// Writes gridConvergenceReportJson to `path` (parent directories created).
// Throws cfd::IOError on failure.
void writeGridConvergenceReport(const std::string& path, const GridConvergenceStudy& study);

// Validates a grid-convergence report (the JSON text, or a file holding
// it) against the format_version 1 schema and its internal consistency
// rules: required keys and types, three coarse -> fine grids with h
// strictly decreasing and cells = nx * ny when all solves were accepted,
// numeric fields finite or null, known status names, no order /
// extrapolation / GCI for oscillatory, divergent, insufficient-separation
// or invalid quantities, every quantity invalid when a solve was rejected,
// and grid_independent only for an asymptotic quantity whose GCI21 is
// within its threshold. Returns the problems found (empty = valid).
[[nodiscard]] std::vector<std::string> validateGridConvergenceReport(const std::string& jsonText);
[[nodiscard]] std::vector<std::string> validateGridConvergenceReportFile(const std::string& path);

}  // namespace cfd::validation
