#pragma once

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

  std::vector<Real> uResidualHistory;
  std::vector<Real> vResidualHistory;
  std::vector<Real> pressureResidualHistory;
  std::vector<Real> continuityHistory;

  [[nodiscard]] bool converged() const noexcept { return status == SIMPLEStatus::Converged; }
};

}  // namespace cfd::pressure_velocity
