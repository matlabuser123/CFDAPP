#pragma once

#include <optional>
#include <vector>

#include "cfd/core/Types.hpp"
#include "cfd/fields/ScalarField.hpp"
#include "cfd/fields/SurfaceField.hpp"
#include "cfd/fields/VectorField.hpp"

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
// All four gates plus globalMassImbalance use plain absolute tolerances
// against SIMPLESettings -- no baseline normalization in this phase
// (TODO.md section 44 explicitly allows this for early strict P0
// regression testing).
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

  std::vector<Real> uResidualHistory;
  std::vector<Real> vResidualHistory;
  std::vector<Real> pressureResidualHistory;
  std::vector<Real> continuityHistory;

  [[nodiscard]] bool converged() const noexcept { return status == SIMPLEStatus::Converged; }
};

}  // namespace cfd::pressure_velocity
