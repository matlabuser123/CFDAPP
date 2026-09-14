#pragma once

#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "cfd/core/Types.hpp"
#include "cfd/pressure_velocity/SIMPLEResult.hpp"
#include "cfd/validation/GridConvergenceStudy.hpp"
#include "cfd/validation/ManufacturedSolutionStudy.hpp"

// P12-NUM-007 -- the record of a production validation (a physical case
// compared with published benchmark data or an exact solution) and its
// deterministic reports. It is the same measurement record as the
// P12-NUM-006 MMS studies -- one MMSLevel per solve (mesh, solver status,
// iterations, mass imbalance, error norms, diagnostics, runtime) and
// MMSGate pass/fail checks -- with the case identity added, and it embeds
// the P12-NUM-005 GridConvergenceStudy of the same case. No second
// norm / order / grid-convergence implementation lives here.
//
// Verification hierarchy (docs/validation/production_validation.md):
//   MMS (NUM-006)             -- is the discretisation implemented right?
//   grid convergence (NUM-005) -- how far is this solution from its own
//                                grid-converged limit?
//   production validation     -- how far is the (converging) solution from
//   (NUM-007)                    the physics a benchmark documents?
namespace cfd::validation {

// A pass/fail criterion (same shape as an MMS gate).
using ValidationCheck = MMSGate;

// One production-validation solve: a named case at one Reynolds number, on
// one mesh, with one convection scheme.
struct ValidationRun {
  std::string caseName;          // e.g. "cavity_re1000"
  std::optional<Real> reynolds;  // the case's defining Reynolds number
  std::string scheme;            // convection scheme name, e.g. "quick"
  MMSLevel level;                // mesh, status, iterations, errors, runtime
  std::vector<ValidationCheck> checks;

  // "<case>/<mesh>/<scheme>" -- unique within a report.
  [[nodiscard]] std::string id() const;
  // The solve was accepted and every check passed.
  [[nodiscard]] bool passed() const;
};

struct ValidationReport {
  std::string name;
  std::string description;
  // What the errors are measured against, with its source (citation /
  // closed form); required -- a validation without a reference is not one.
  std::string reference;
  std::vector<std::pair<std::string, Real>> coefficients;          // physical parameters
  std::vector<std::pair<std::string, std::string>> configuration;  // BCs, settings, build...
  std::vector<ValidationRun> runs;
  // Observed order of an error norm across a run sequence (when a
  // benchmark comparison is meaningful as a convergence sequence).
  std::vector<MMSOrder> orders;
  // The P12-NUM-005 grid-convergence studies of this case (solution
  // convergence, analysed from our own solutions only).
  std::vector<GridConvergenceStudy> gridConvergence;
  std::vector<ValidationCheck> gates;  // report-level checks
  std::vector<std::string> limitations;

  // At least one run, every run passed, every report-level gate passed.
  [[nodiscard]] bool passed() const;
  // The run with this id; throws InvalidArgumentError if absent.
  [[nodiscard]] const ValidationRun& run(const std::string& id) const;
};

// A run whose level is filled from a SIMPLE solve: status name and
// acceptance from assessSimpleSolve(result, massImbalanceTolerance),
// iterations, mass imbalance, h = representativeGridSize(area, cells), and
// the cost / residual diagnostics ("momentum_linear_iterations",
// "pressure_linear_iterations", "linear_solver_fallbacks",
// "final_u_residual", "final_v_residual", "final_pressure_residual",
// "final_continuity_residual"). Errors and checks are the caller's.
[[nodiscard]] ValidationRun makeSimpleValidationRun(std::string caseName,
                                                    std::optional<Real> reynolds,
                                                    std::string scheme, const GridSpec& grid,
                                                    const pressure_velocity::SIMPLEResult& result,
                                                    Real massImbalanceTolerance,
                                                    double runtimeSeconds);

// The P12-NUM-005 grid-study entry of an already-solved run, so a report
// analyses the grid convergence of the very solves it validated (no
// re-solve): spec {mesh name, nx, ny, lengthX, lengthY}, cells / h /
// runtime / iterations from the level, acceptance from the level's solver
// gate, and as quantities the named entries of level.diagnostics (a name
// absent from the diagnostics is simply absent -- analyzeGridStudy then
// reports that quantity Invalid).
[[nodiscard]] GridStudyEntry toGridStudyEntry(const ValidationRun& run, Real lengthX, Real lengthY,
                                              const std::vector<std::string>& quantityNames);

// MMSOrder of `quantity`/`norm` over the given runs (coarse -> fine, >= 3,
// all accepted) -- computeMMSOrder on their levels.
[[nodiscard]] MMSOrder computeRunSequenceOrder(
    const std::vector<const ValidationRun*>& coarseToFine, const std::string& quantity,
    NormKind norm, const GridConvergenceOptions& options = {});

// Deterministic reports: identical text for an identical report, except
// run times -- the JSON "runtime" block and the embedded grid-convergence
// studies' runtime_seconds / the Markdown runtime columns. Unset values
// are JSON null, never NaN.
[[nodiscard]] std::string validationReportJson(const ValidationReport& report);
[[nodiscard]] std::string validationReportMarkdown(const ValidationReport& report);
// Writes validationReportJson (parent directories created); throws IOError.
void writeValidationReport(const std::string& path, const ValidationReport& report);

}  // namespace cfd::validation
