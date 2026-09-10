#pragma once

// P5-B -- GUI Solver Workflow: the minimal, additive integration hook
// SIMPLE needs to support live progress reporting (section 14) and safe
// cooperative cancellation (section 13) from a caller running solve() on
// a worker thread -- e.g. cfd::app::ProjectRunner, shared unchanged by
// the CLI and the GUI (section 0's "ONE SOLVER BACKEND"). This is
// integration plumbing, not a numerics change (TODO.md P5 section 1):
// both callbacks default to empty/unset, and when unset SIMPLE::solve()
// behaves exactly as before this header existed -- see
// SIMPLETest.ProgressAndCancellationDefaultToNoOpAndDoNotChangeTheResult.

#include <functional>

#include "cfd/core/Types.hpp"

namespace cfd::pressure_velocity {

// Reported once per *completed* outer iteration (never mid-iteration),
// using the exact same residual definitions SIMPLEResult's own history
// vectors already use (SIMPLEResult.hpp's own header comment) -- a GUI
// consuming this is reading the solver's canonical residuals, not a
// second independently-computed set (TODO.md P5 section 27's explicit
// "do not calculate a second set of GUI residuals").
struct SIMPLEIterationProgress {
  Index iteration{};
  Index maxIterations{};
  Real uResidual{};
  Real vResidual{};
  Real pressureResidual{};
  Real continuityResidual{};
  Real globalMassImbalance{};
};

using SIMPLEProgressCallback = std::function<void(const SIMPLEIterationProgress&)>;

// Checked once at the top of every outer iteration, before that
// iteration does any assembly/solve work (TODO.md P5 section 13: "check
// a cancellation flag at safe points such as outer iterations"). A
// caller running solve() on a worker thread can set an
// std::atomic<bool> and have this simply return its value -- SIMPLE
// itself has no threading of its own and never calls this from more
// than one thread.
using SIMPLECancellationCheck = std::function<bool()>;

}  // namespace cfd::pressure_velocity
