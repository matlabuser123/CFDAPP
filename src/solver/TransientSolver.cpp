#include "cfd/solver/TransientSolver.hpp"

#include <cmath>
#include <utility>

#include "cfd/core/Exception.hpp"

namespace cfd::solver {

namespace {

bool anyNonFinite(const TransientState& state) {
  for (cfd::Index i = 0; i < state.velocity.size(); ++i) {
    if (!std::isfinite(state.velocity[i].x) || !std::isfinite(state.velocity[i].y)) return true;
  }
  for (cfd::Index i = 0; i < state.pressure.size(); ++i) {
    if (!std::isfinite(state.pressure[i])) return true;
  }
  for (cfd::Index i = 0; i < state.massFlux.size(); ++i) {
    if (!std::isfinite(state.massFlux[i])) return true;
  }
  return false;
}

TransientStatus toTransientStatus(TransientStepStatus stepStatus) {
  switch (stepStatus) {
    case TransientStepStatus::Converged:
      // Never reached: callers only map a *non*-Converged step status.
      return TransientStatus::InvalidConfiguration;
    case TransientStepStatus::MomentumFailure:
      return TransientStatus::MomentumFailure;
    case TransientStepStatus::PressureCorrectionFailure:
      return TransientStatus::PressureCorrectionFailure;
    case TransientStepStatus::NonFiniteState:
      return TransientStatus::NonFiniteState;
    case TransientStepStatus::InvalidConfiguration:
      return TransientStatus::InvalidConfiguration;
  }
  return TransientStatus::InvalidConfiguration;
}

}  // namespace

TransientSolver::TransientSolver(const TransientStepSolver& stepSolver, Real cflFailAbove)
    : stepSolver_(stepSolver), cflFailAbove_(cflFailAbove) {
  if (!std::isfinite(cflFailAbove_) || !(cflFailAbove_ > 0.0)) {
    throw InvalidArgumentError("TransientSolver: cflFailAbove must be finite and > 0");
  }
}

TransientResult TransientSolver::solve(TransientState initialState,
                                       TimeController timeController) const {
  TransientState state = std::move(initialState);
  std::vector<TimeStepRecord> history;

  while (!timeController.finished()) {
    const Real dt = timeController.deltaT();
    const TransientStepResult stepResult = stepSolver_.solveTimeStep(state, dt);

    // A failed step's own state is never accepted -- `state` (the last
    // accepted state) becomes the returned finalState unchanged.
    if (stepResult.status != TransientStepStatus::Converged) {
      return TransientResult{toTransientStatus(stepResult.status), std::move(history),
                             std::move(state)};
    }
    // Defense in depth: trust the stepper's Converged status, but verify
    // it anyway (TODO.md P2 section 15/51 -- "verify finite state" is its
    // own line in the loop, not folded into "perform PISO time step").
    if (anyNonFinite(stepResult.state)) {
      return TransientResult{TransientStatus::NonFiniteState, std::move(history), std::move(state)};
    }
    if (stepResult.maxCFL > cflFailAbove_) {
      // Reject, same as a failed step -- "must not be silently accepted"
      // applies equally to a numerically successful step whose CFL is
      // outside the caller's declared safe range.
      return TransientResult{TransientStatus::CFLViolation, std::move(history), std::move(state)};
    }

    state = stepResult.state;
    timeController.advance();
    history.push_back(TimeStepRecord{timeController.step(), timeController.time(), dt,
                                     stepResult.maxCFL, stepResult.continuityResidual,
                                     stepResult.massImbalance});
  }

  const TransientStatus status =
      timeController.reachedEndTime() ? TransientStatus::Completed : TransientStatus::MaxTimeSteps;
  return TransientResult{status, std::move(history), std::move(state)};
}

}  // namespace cfd::solver
