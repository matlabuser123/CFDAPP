#pragma once

#include "cfd/core/Types.hpp"

namespace cfd::solver {

// Owns physical time for a transient run: start/current/end time, the
// configured (nominal) time-step size, and the step index -- nothing
// about *what* is being time-stepped. TODO.md P2 section 2: "Do not let
// PISO independently manage physical time" -- a future TransientSolver/
// PISO query deltaT()/time() and call advance() once per accepted step;
// neither mutates time state directly.
//
// Deterministic termination (P2 sections 3-4): time is never accumulated
// by repeated floating-point addition (startTime + deltaT + deltaT + ...
// drifts after enough steps -- e.g. 0.1 is not exactly representable in
// binary floating point). Every query instead recomputes time from the
// step index directly (see TimeController.cpp's timeAtStep), and the
// final step is explicitly snapped to `endTime` once within a tiny
// tolerance of it, rather than relying on exact floating-point equality
// (`currentTime == endTime`) to detect "are we done" -- see .cpp for why
// a tolerance is needed at all even with the step-index approach.
class TimeController {
 public:
  // Throws InvalidArgumentError if:
  //   - startTime, endTime, or deltaT is not finite
  //   - deltaT <= 0
  //   - endTime < startTime
  //   - maxSteps == 0
  // startTime == endTime is accepted (a valid, immediately-finished,
  // zero-duration run) rather than rejected as degenerate.
  TimeController(Real startTime, Real endTime, Real deltaT, Index maxSteps);

  [[nodiscard]] Real time() const noexcept;

  // The delta the *next* call to advance() will apply -- the nominal
  // configured deltaT for every step except the last, which is shortened
  // so time() lands exactly on endTime rather than overshooting it.
  // Returns 0.0 once finished() (there is no "next" step).
  [[nodiscard]] Real deltaT() const noexcept;

  [[nodiscard]] Index step() const noexcept;

  // True once either endTime has been reached (time() == endTime exactly,
  // guaranteed by the final step's snap, not floating-point luck) or
  // maxSteps have been taken, whichever comes first.
  [[nodiscard]] bool finished() const noexcept;

  // Applies deltaT() to advance one time step and increments step().
  // Throws InvalidArgumentError if already finished() -- callers are
  // expected to check finished() first (the natural `while (!finished())`
  // loop shape), so reaching here already finished is a caller logic
  // error worth failing loudly on, not a recoverable runtime condition to
  // silently no-op.
  void advance();

 private:
  [[nodiscard]] Real timeAtStep(Index step) const noexcept;

  Real startTime_;
  Real endTime_;
  Real nominalDeltaT_;
  Index maxSteps_;
  Index step_{0};
  Real currentTime_;
};

}  // namespace cfd::solver
