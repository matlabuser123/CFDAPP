#include "cfd/solver/TimeController.hpp"

#include <cmath>

#include "cfd/core/Exception.hpp"

namespace cfd::solver {

TimeController::TimeController(Real startTime, Real endTime, Real deltaT, Index maxSteps)
    : startTime_(startTime), endTime_(endTime), nominalDeltaT_(deltaT), maxSteps_(maxSteps) {
  if (!std::isfinite(startTime_)) {
    throw InvalidArgumentError("TimeController: startTime must be finite");
  }
  if (!std::isfinite(endTime_)) {
    throw InvalidArgumentError("TimeController: endTime must be finite");
  }
  if (!std::isfinite(nominalDeltaT_) || !(nominalDeltaT_ > 0.0)) {
    throw InvalidArgumentError("TimeController: deltaT must be finite and > 0");
  }
  if (endTime_ < startTime_) {
    throw InvalidArgumentError("TimeController: endTime must be >= startTime");
  }
  if (maxSteps_ == 0) {
    throw InvalidArgumentError("TimeController: maxSteps must be > 0");
  }
  currentTime_ = timeAtStep(0);
}

Real TimeController::timeAtStep(Index step) const noexcept {
  const Real raw = startTime_ + (static_cast<Real>(step) * nominalDeltaT_);

  // Snap to endTime_ once within a tiny tolerance of it, rather than
  // requiring raw >= endTime_ exactly. deltaT is not always exactly
  // representable in binary floating point (e.g. 0.1) -- an evenly-
  // dividing case like start=0/end=1/deltaT=0.1 would otherwise compute
  // 10*0.1 == 0.9999999999999999 (one ULP short of 1.0) at what should be
  // the final step, and need an extra, practically-zero-length 11th step
  // to actually cross endTime_. The tolerance is scaled by nominalDeltaT_
  // (the run's own characteristic time scale) rather than a fixed
  // absolute epsilon, so it stays meaningful for very large or very small
  // deltaT alike, and stays many orders of magnitude smaller than any
  // *genuinely* shortened final step (which is never anywhere near this
  // small relative to nominalDeltaT_, short of a maxSteps/deltaT
  // combination the caller chose specifically to land exactly on
  // endTime_ already).
  const Real tolerance = nominalDeltaT_ * 1e-9;
  return (raw >= endTime_ - tolerance) ? endTime_ : raw;
}

Real TimeController::time() const noexcept { return currentTime_; }

Index TimeController::step() const noexcept { return step_; }

bool TimeController::finished() const noexcept {
  return (step_ >= maxSteps_) || (currentTime_ >= endTime_);
}

bool TimeController::reachedEndTime() const noexcept { return currentTime_ >= endTime_; }

Real TimeController::deltaT() const noexcept {
  if (finished()) {
    return 0.0;
  }
  return timeAtStep(step_ + 1) - currentTime_;
}

void TimeController::advance() {
  if (finished()) {
    throw InvalidArgumentError(
        "TimeController::advance: already finished (check finished() first)");
  }
  ++step_;
  currentTime_ = timeAtStep(step_);
}

}  // namespace cfd::solver
