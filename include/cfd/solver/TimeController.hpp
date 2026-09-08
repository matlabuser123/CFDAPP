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
  // `startingStep` (Restart-E, TODO.md P2 -- Restart capability):
  // resuming a run from an accepted RestartSnapshot passes the
  // *original* run's own startTime/endTime/deltaT/maxSteps here
  // (never a recomputed/derived startTime -- see below) plus
  // `startingStep = snapshot.step`, so step() reports the same absolute
  // step count a single, uninterrupted run would have -- and, critically,
  // timeAtStep() (and therefore deltaT()/time()) evaluates the *exact
  // same deterministic formula at the exact same step index* a
  // continuous run's own TimeController would have, bit-for-bit -- not
  // a startTime shifted to the resume point, which would introduce a
  // different floating-point computation path (this project's own
  // PISO-I regression already found two dt sequences that were
  // mathematically equal but not bit-identical for exactly this reason).
  // Defaults to 0, so every existing caller's behavior is completely
  // unchanged.
  //
  // Throws InvalidArgumentError if:
  //   - startTime, endTime, or deltaT is not finite
  //   - deltaT <= 0
  //   - endTime < startTime
  //   - maxSteps == 0
  // startTime == endTime is accepted (a valid, immediately-finished,
  // zero-duration run) rather than rejected as degenerate. A
  // startingStep >= maxSteps is likewise accepted -- an immediately-
  // finished TimeController is meaningful (a restart resumed at or past
  // its own original step cap), not a construction-time error.
  TimeController(Real startTime, Real endTime, Real deltaT, Index maxSteps, Index startingStep = 0);

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

  // True once time() has actually reached endTime, as opposed to
  // finished() being true only because maxSteps was hit first -- lets a
  // caller (e.g. TransientSolver, TODO.md P2 section 16) distinguish
  // "ran to completion" from "hit the iteration cap" once finished(),
  // matching how SIMPLEStatus::Converged is distinct from
  // ::MaxIterations rather than one generic "stopped" flag.
  [[nodiscard]] bool reachedEndTime() const noexcept;

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
  Index step_;
  Real currentTime_;
};

}  // namespace cfd::solver
