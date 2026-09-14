#pragma once

#include <optional>
#include <vector>

#include "cfd/core/Types.hpp"
#include "cfd/fields/ScalarField.hpp"
#include "cfd/fields/SurfaceField.hpp"
#include "cfd/fields/VectorField.hpp"
#include "cfd/solver/SolverRobustness.hpp"

namespace cfd::pressure_velocity {

// TODO.md P0 -- SIMPLE section 7: evidence, not just a converged-or-not
// flag. A failure category is never masked as MaxIterations (TODO.md
// section 74: no secret fallbacks) -- a failed inner linear solve is
// always its own distinct status, never silently absorbed into an outer
// "didn't converge in time" result.
enum class SIMPLEStatus {
  Converged,
  MaxIterations,
  MomentumFailure,
  PressureCorrectionFailure,
  NonFiniteState,
  InvalidConfiguration,
  // P5-B -- GUI Solver Workflow, section 13: a solve stopped early by an
  // external cancellation request (see SIMPLE's own optional
  // SIMPLECancellationCheck constructor parameter), checked only at the
  // top of an outer iteration -- never mid-iteration -- so the fields
  // returned alongside this status are always the last *complete*
  // converged-or-not outer iterate, never a partially-updated one. Never
  // produced by any pre-P5 call site (the cancellation check defaults to
  // null, i.e. "never cancelled"), so this is purely additive: every
  // existing exhaustive switch over SIMPLEStatus needed a new case added
  // once this value existed, but no prior *behavior* changed.
  Cancelled,
  // P12-NUM-004 (appended, so every existing value keeps its number):
  // produced only when the corresponding detector is enabled in
  // SIMPLESettings::robustness (default off). Stagnated: the residuals
  // plateaued materially above convergence (no material improvement over
  // the stagnation window) -- stopped before maxIterations instead of being
  // reported as MaxIterations. Diverging: a finite but clearly diverging
  // residual history (see cfd::solver::OuterIterationMonitor for both exact
  // criteria). SIMPLEResult::robustness.statusDetail says why.
  Stagnated,
  Diverging,
};

// residualHistory[k] (all four histories, plus continuityHistory) is the
// value *after* completed SIMPLE iteration k+1 -- history.size() ==
// iterations for a normal run (TODO.md section 51).
//
// U/V residuals are each momentum linear solve's *initial* residual
// (||b - A x0||), not its final one (TODO.md section 36-37: "be
// explicit... do not invent a second incompatible residual
// definition"). SIMPLE.cpp warm-starts each solve from the previous
// SIMPLE iterate, so this measures how far that previous velocity is
// from satisfying the newly-reassembled equation -- which genuinely
// shrinks as the outer iteration approaches a steady state. The linear
// solve's own *final* residual is unusable for this: it is small by
// construction on every successful inner solve (that is what "the
// linear solve converged" means) regardless of outer-loop progress, and
// using it would falsely report SIMPLE convergence after a single
// iteration. The P residual applies the same "before this iteration's
// solve" principle to pressure correction's own reset convention (p'
// starts from 0 every outer iteration -- section 17), so it is
// ||b_p|| -- not a standalone physical pressure equation's residual
// (section 38). Continuity is computed from the corrected (not
// predictor) face flux (section 39).
//
// By default all four gates plus globalMassImbalance use plain absolute
// tolerances against SIMPLESettings (the P0 rule). P12-NUM-004: with
// SIMPLESettings::robustness.convergenceCriterion == Normalized the u/v/p
// gates are judged against their normalization references instead
// (continuity and globalMassImbalance keep their absolute gates) -- see
// cfd::solver::OuterIterationMonitor. The normalized histories are always
// reported in `robustness`.
struct SIMPLEResult {
  cfd::fields::VectorField velocity;
  cfd::fields::ScalarField pressure;
  cfd::fields::SurfaceField massFlux;

  SIMPLEStatus status{SIMPLEStatus::MaxIterations};
  Index iterations{0};

  Real finalUResidual{};
  Real finalVResidual{};
  Real finalPressureResidual{};
  Real finalContinuityResidual{};
  Real globalMassImbalance{};

  // P2-TURB-004 section 23: the active TurbulenceModel's own
  // convergenceResidual() after the final iteration -- std::nullopt for
  // laminar (or any model that does not report one), populated (and
  // gated on, via SIMPLESettings::turbulenceTolerance) once a transport-
  // equation model like KEpsilonModel is active. Not folded into
  // finalContinuityResidual or any velocity/pressure residual -- a
  // distinct physical quantity gets its own field (TODO.md P0 section 7
  // precedent).
  std::optional<Real> finalTurbulenceResidual;

  // P12-NUM-003: total number of momentum-predictor solves (each a u and a
  // v linear solve) and pressure-correction solves over the whole run --
  // iterations * max(1, SIMPLESettings::nonOrthogonalCorrections) each on
  // a completed run, the observable that pins the correction pass count.
  Index momentumPredictorPasses{0};
  Index pressureCorrectionPasses{0};

  // P12-NUM-007: total inner linear-solver iterations over the whole run --
  // every successful u and v momentum solve (non-orthogonal passes
  // included) and every pressure-correction solve, each counted as its
  // SolverResult::iterations (so linear-solver fallback attempts are
  // included). A cost observable only; nothing reads it back.
  Index momentumLinearIterations{0};
  Index pressureLinearIterations{0};

  std::vector<Real> uResidualHistory;
  std::vector<Real> vResidualHistory;
  std::vector<Real> pressureResidualHistory;
  std::vector<Real> continuityHistory;

  // P12-NUM-004: normalized residual histories and references, the
  // relaxation factors used each iteration, linear-solver fallback events,
  // and the reason for a Stagnated/Diverging/linear-failure status.
  cfd::solver::OuterIterationDiagnostics robustness;

  [[nodiscard]] bool converged() const noexcept { return status == SIMPLEStatus::Converged; }
};

}  // namespace cfd::pressure_velocity
