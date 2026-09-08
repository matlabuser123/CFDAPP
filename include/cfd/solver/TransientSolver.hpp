#pragma once

#include <vector>

#include "cfd/core/Types.hpp"
#include "cfd/fields/ScalarField.hpp"
#include "cfd/fields/SurfaceField.hpp"
#include "cfd/fields/VectorField.hpp"
#include "cfd/solver/TimeController.hpp"

namespace cfd::solver {

// The physical state at one time level: velocity, pressure, and the face
// mass flux consistent with them (TODO.md P2 section 12: "current state,
// previous time-level state"). Deliberately just data -- no mesh, no
// physics, no time -- so it is equally at home as TransientSolver's
// "previous" state, a stepper's "new" state, or TransientResult's
// finalState.
struct TransientState {
  cfd::fields::VectorField velocity;
  cfd::fields::ScalarField pressure;
  cfd::fields::SurfaceField massFlux;
};

// Outcome of advancing one time step. Mirrors SIMPLEStatus's shape
// (SIMPLEResult.hpp) minus MaxIterations -- a single implicit time step
// has no outer-iteration budget of its own to exhaust, unlike SIMPLE's
// steady outer loop.
enum class TransientStepStatus {
  Converged,
  MomentumFailure,
  PressureCorrectionFailure,
  NonFiniteState,
  InvalidConfiguration,
};

// `state` is meaningful only when status == Converged; a failed step's
// TransientSolver discards it rather than accepting partial/garbage data
// (TODO.md P2 section 15: "A failed PISO step must not be silently
// accepted"). maxCFL/continuityResidual/massImbalance are the *stepper's*
// own diagnostics for this step -- it already has the mesh/density/flux
// needed to compute them (e.g. via solver::calculateCFL), so
// TransientSolver never needs CFD-specific types to report or act on
// them (see TransientStepSolver below). maxCFL is the *pre-step* Courant
// number (computed from the previous state's flux and this step's dt,
// before advancing -- section 15's "calculate pre-step CFL").
struct TransientStepResult {
  TransientState state;
  TransientStepStatus status{TransientStepStatus::InvalidConfiguration};
  Real maxCFL{};
  Real continuityResidual{};
  Real massImbalance{};
};

// Common interface for one-time-step solvers (PISO later; TODO.md P2
// section 13's PISOStepResult solveTimeStep(state, previousState, dt)
// sketch, reshaped to match this project's existing
// PressureVelocitySolver convention of an abstract interface + a
// concrete algorithm class). Deliberately owns *no* dt/state itself and
// takes no mesh/fluid/boundary-condition parameters here: those are a
// concrete implementation's own construction-time configuration (the
// same way SIMPLE is constructed with its settings once, not passed them
// on every call) -- keeping this interface, and therefore
// TransientSolver below, entirely CFD-agnostic.
class TransientStepSolver {
 public:
  virtual ~TransientStepSolver() = default;

  [[nodiscard]] virtual TransientStepResult solveTimeStep(const TransientState& previousState,
                                                          Real dt) const = 0;
};

// Overall run outcome (TODO.md P2 section 16): every distinct way a
// transient run can end gets its own value, never collapsed into a
// generic success/failure flag -- CFLViolation and MaxTimeSteps are both
// real, common outcomes, not "failures" folded into NonFiniteState.
enum class TransientStatus {
  Completed,
  MaxTimeSteps,
  MomentumFailure,
  PressureCorrectionFailure,
  NonFiniteState,
  CFLViolation,
  InvalidConfiguration,
};

// One accepted step's evidence (TODO.md P2 section 29-30) -- `time`/`step`
// are the state *after* this step (i.e. TimeController's own values right
// after the advance() that accepted it), matching SIMPLEResult's history
// convention of recording post-iteration values.
struct TimeStepRecord {
  Index step;
  Real time;
  Real deltaT;
  Real maxCFL;
  Real continuityResidual;
  Real massImbalance;
};

struct TransientResult {
  TransientStatus status{TransientStatus::InvalidConfiguration};
  std::vector<TimeStepRecord> history;
  // The last *accepted* state -- for any non-Completed/MaxTimeSteps
  // status this is the state the failing/rejected step was computed
  // *from*, never that step's own (discarded) output.
  TransientState finalState;
};

// Orchestrates a transient run: owns the time loop and the accepted
// state, invokes an injected TransientStepSolver once per step, monitors
// CFL, and records history -- deliberately does *not* contain any
// pressure-correction/momentum-assembly equation itself (TODO.md P2
// section 14). No PISO implementation exists yet; this class is exercised
// against a test-only stub TransientStepSolver until one does (P2-005).
class TransientSolver {
 public:
  // stepSolver is not owned; the caller must keep it alive for the
  // duration of solve(). Throws InvalidArgumentError if cflFailAbove is
  // not finite or <= 0.
  TransientSolver(const TransientStepSolver& stepSolver, Real cflFailAbove);

  // Runs until timeController.finished(), a step fails, or a step's
  // pre-step CFL exceeds cflFailAbove -- whichever comes first. Both
  // timeController and initialState are taken and stepped/consumed by
  // value: solve() owns its own working copies, never mutating what the
  // caller passed in (mirroring SIMPLE::solve() taking its initial
  // fields by value).
  [[nodiscard]] TransientResult solve(TransientState initialState,
                                      TimeController timeController) const;

 private:
  const TransientStepSolver& stepSolver_;
  Real cflFailAbove_;
};

}  // namespace cfd::solver
