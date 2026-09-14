#include "cfd/solver/SolverRobustness.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>
#include <string>
#include <utility>

#include "cfd/core/Exception.hpp"

namespace cfd::solver {

namespace {

constexpr Real kInfinity = std::numeric_limits<Real>::infinity();

[[noreturn]] void invalid(const std::string& message) {
  throw InvalidArgumentError("SolverRobustnessSettings: " + message);
}

void requireOpenUnitInterval(Real value, const char* name) {
  if (!std::isfinite(value) || !(value > 0.0) || !(value < 1.0)) {
    invalid(std::string(name) + " must be finite and in (0, 1)");
  }
}

void requireWindow(Index window, const char* name) {
  if (window < 2 || window > kMaxDetectionWindow) {
    invalid(std::string(name) + " must be in [2, " + std::to_string(kMaxDetectionWindow) + "]");
  }
}

void requireRelaxationBounds(Real minimum, Real maximum, const char* minName, const char* maxName) {
  if (!std::isfinite(minimum) || !(minimum > 0.0) || minimum > 1.0) {
    invalid(std::string(minName) + " must be finite and in (0, 1]");
  }
  if (!std::isfinite(maximum) || !(maximum > 0.0) || maximum > 1.0) {
    invalid(std::string(maxName) + " must be finite and in (0, 1]");
  }
  if (minimum > maximum) {
    invalid(std::string(minName) + " must be <= " + maxName);
  }
}

std::string formatReal(Real value) {
  char buffer[32];
  std::snprintf(buffer, sizeof(buffer), "%.4g", value);
  return buffer;
}

}  // namespace

std::string_view convergenceCriterionName(ConvergenceCriterion criterion) noexcept {
  switch (criterion) {
    case ConvergenceCriterion::Absolute:
      return "absolute";
    case ConvergenceCriterion::Normalized:
      return "normalized";
  }
  return "unknown";
}

void validateSolverRobustnessSettings(const SolverRobustnessSettings& settings,
                                      Real initialVelocityRelaxation,
                                      Real initialPressureRelaxation) {
  if (settings.normalization.referenceIterations == 0) {
    invalid("normalization.referenceIterations must be >= 1");
  }
  requireOpenUnitInterval(settings.normalization.velocityTolerance,
                          "normalization.velocityTolerance");
  requireOpenUnitInterval(settings.normalization.pressureTolerance,
                          "normalization.pressureTolerance");

  requireWindow(settings.stagnation.window, "stagnation.window");
  requireOpenUnitInterval(settings.stagnation.minRelativeImprovement,
                          "stagnation.minRelativeImprovement");

  requireWindow(settings.divergence.window, "divergence.window");
  if (!std::isfinite(settings.divergence.growthFactor) ||
      !(settings.divergence.growthFactor > 1.0)) {
    invalid("divergence.growthFactor must be finite and > 1");
  }

  const AdaptiveRelaxationSettings& adaptive = settings.adaptiveRelaxation;
  requireRelaxationBounds(adaptive.minVelocity, adaptive.maxVelocity,
                          "adaptiveRelaxation.minVelocity", "adaptiveRelaxation.maxVelocity");
  requireRelaxationBounds(adaptive.minPressure, adaptive.maxPressure,
                          "adaptiveRelaxation.minPressure", "adaptiveRelaxation.maxPressure");
  if (adaptive.enabled) {
    if (!(initialVelocityRelaxation >= adaptive.minVelocity &&
          initialVelocityRelaxation <= adaptive.maxVelocity)) {
      invalid("the velocity relaxation (" + formatReal(initialVelocityRelaxation) +
              ") must lie within [adaptiveRelaxation.minVelocity, maxVelocity] when adaptive "
              "relaxation is enabled");
    }
    if (!(initialPressureRelaxation >= adaptive.minPressure &&
          initialPressureRelaxation <= adaptive.maxPressure)) {
      invalid("the pressure relaxation (" + formatReal(initialPressureRelaxation) +
              ") must lie within [adaptiveRelaxation.minPressure, maxPressure] when adaptive "
              "relaxation is enabled");
    }
  }

  cfd::algebra::validateLinearSolverFallbackSettings(settings.linearSolverFallback);
}

// ---------------------------------------------------------------------------
// ResidualTracker
// ---------------------------------------------------------------------------

ResidualTracker::ResidualTracker(Real floor, Index referenceIterations, Index windowCapacity,
                                 Index bestFrom)
    : floor_(floor),
      referenceIterations_(referenceIterations),
      capacity_(windowCapacity),
      bestFrom_(std::max<Index>(bestFrom, 1)),
      best_(kInfinity),
      bestBeforeWindow_(kInfinity),
      ring_(windowCapacity, 0.0),
      ringIndex_(windowCapacity, 0) {
  if (!std::isfinite(floor_) || !(floor_ > 0.0)) {
    throw InvalidArgumentError("ResidualTracker: floor must be finite and > 0");
  }
  if (capacity_ == 0) {
    throw InvalidArgumentError("ResidualTracker: windowCapacity must be >= 1");
  }
}

void ResidualTracker::push(Real value) {
  ++count_;
  latest_ = value;
  if (!std::isfinite(value)) {
    nonFiniteSeen_ = true;
    return;
  }
  ++finiteCount_;
  if (count_ <= referenceIterations_) {
    reference_ = std::max(reference_, value);
  }
  best_ = std::min(best_, value);

  if (size_ == capacity_) {
    // The oldest value leaves the window.
    if (ringIndex_[head_] >= bestFrom_) {
      bestBeforeWindow_ = std::min(bestBeforeWindow_, ring_[head_]);
    }
  } else {
    ++size_;
  }
  ring_[head_] = value;
  ringIndex_[head_] = count_;
  head_ = (head_ + 1) % capacity_;
}

bool ResidualTracker::latestFinite() const noexcept { return std::isfinite(latest_); }

Real ResidualTracker::effectiveReference() const noexcept { return std::max(reference_, floor_); }

Real ResidualTracker::normalized() const noexcept { return normalize(latest_, reference_, floor_); }

Real ResidualTracker::normalize(Real value, Real reference, Real floor) noexcept {
  const Real ratio = value / std::max(reference, floor);
  // A finite value whose ratio overflows (e.g. 1e300 / 1e-12) saturates at
  // the largest finite double: finite input never yields a non-finite
  // normalized residual. Non-finite input propagates (it is flagged).
  if (std::isfinite(value) && !std::isfinite(ratio)) {
    return std::copysign(std::numeric_limits<Real>::max(), ratio);
  }
  return ratio;
}

Real ResidualTracker::valueAgo(Index ago) const {
  if (ago >= size_) {
    throw InvalidArgumentError("ResidualTracker::valueAgo: index outside the window");
  }
  return ring_[(head_ + capacity_ - 1 - ago) % capacity_];
}

Real ResidualTracker::windowMin() const {
  if (size_ == 0) throw InvalidArgumentError("ResidualTracker::windowMin: empty window");
  Real result = kInfinity;
  for (Index k = 0; k < size_; ++k) result = std::min(result, valueAgo(k));
  return result;
}

Real ResidualTracker::windowMax() const {
  if (size_ == 0) throw InvalidArgumentError("ResidualTracker::windowMax: empty window");
  Real result = -kInfinity;
  for (Index k = 0; k < size_; ++k) result = std::max(result, valueAgo(k));
  return result;
}

Real ResidualTracker::windowMean() const {
  if (size_ == 0) throw InvalidArgumentError("ResidualTracker::windowMean: empty window");
  Real sum = 0.0;
  for (Index k = 0; k < size_; ++k) sum += valueAgo(k);
  return sum / static_cast<Real>(size_);
}

// ---------------------------------------------------------------------------
// AdaptiveRelaxationController
// ---------------------------------------------------------------------------

AdaptiveRelaxationController::AdaptiveRelaxationController(AdaptiveRelaxationSettings settings,
                                                           Real initialVelocity,
                                                           Real initialPressure)
    : settings_(settings),
      factors_{initialVelocity, initialPressure},
      ceiling_{settings.maxVelocity, settings.maxPressure} {}

RelaxationAction AdaptiveRelaxationController::update(Real measure) {
  if (!settings_.enabled || !std::isfinite(measure)) {
    return RelaxationAction::Hold;
  }
  constexpr Index kSize = kOscillationRatios + 1;
  if (count_ < kSize) {
    recent_[count_] = measure;
  } else {
    for (Index i = 0; i + 1 < kSize; ++i) recent_[i] = recent_[i + 1];
    recent_[kSize - 1] = measure;
  }
  ++count_;
  if (count_ == 1) {
    return RelaxationAction::Hold;
  }
  const Index last = std::min<Index>(count_, kSize) - 1;  // slot of `measure`
  const Real previous = recent_[last - 1];

  const auto decrease = [&]() {
    ceiling_.velocity = std::min(
        ceiling_.velocity, std::max(settings_.minVelocity, kCeilingFactor * factors_.velocity));
    ceiling_.pressure = std::min(
        ceiling_.pressure, std::max(settings_.minPressure, kCeilingFactor * factors_.pressure));
    factors_.velocity = std::max(settings_.minVelocity, kDecreaseFactor * factors_.velocity);
    factors_.pressure = std::max(settings_.minPressure, kDecreaseFactor * factors_.pressure);
    ++decreases_;
    streak_ = 0;
    return RelaxationAction::Decrease;
  };

  const bool growth = (previous > 0.0) ? (measure > kGrowthRatio * previous) : (measure > 0.0);
  if (growth) {
    return decrease();
  }

  if (count_ >= kSize) {
    bool alternating = true;
    int previousSign = 0;
    for (Index i = 0; i + 1 < kSize; ++i) {
      const Real step = recent_[i + 1] - recent_[i];
      const int sign = (step > 0.0) ? 1 : ((step < 0.0) ? -1 : 0);
      if (sign == 0 || sign == previousSign) {
        alternating = false;
        break;
      }
      previousSign = sign;
    }
    if (alternating && measure >= recent_[last - 2]) {
      return decrease();
    }
  }

  if (measure < previous) {
    ++streak_;
    if (streak_ >= kImprovementStreak) {
      streak_ = 0;
      const RelaxationFactors before = factors_;
      factors_.velocity =
          std::min({settings_.maxVelocity, ceiling_.velocity,
                    std::max(factors_.velocity, kIncreaseFactor * factors_.velocity)});
      factors_.pressure =
          std::min({settings_.maxPressure, ceiling_.pressure,
                    std::max(factors_.pressure, kIncreaseFactor * factors_.pressure)});
      // Already at the bound/ceiling: nothing changed, so it is a Hold.
      if (factors_.velocity == before.velocity && factors_.pressure == before.pressure) {
        return RelaxationAction::Hold;
      }
      ++increases_;
      return RelaxationAction::Increase;
    }
    return RelaxationAction::Hold;
  }
  streak_ = 0;
  return RelaxationAction::Hold;
}

// ---------------------------------------------------------------------------
// Diagnostics
// ---------------------------------------------------------------------------

void recordLinearSolverFallback(OuterIterationDiagnostics& diagnostics, Index iteration,
                                std::string_view equation,
                                const cfd::algebra::LinearSolverFallbackReport& report) {
  if (!report.attempted) return;
  ++diagnostics.linearSolverFallbacks;
  if (report.recovered()) ++diagnostics.linearSolverFallbackRecoveries;
  if (diagnostics.fallbackEvents.size() < kMaxRecordedFallbackEvents) {
    diagnostics.fallbackEvents.push_back(
        LinearSolverFallbackEvent{iteration, std::string(equation), report});
  }
}

std::string describeLinearSolveFailure(std::string_view equation,
                                       cfd::algebra::LinearSolverType primaryType,
                                       const cfd::algebra::SolverResult& result) {
  using cfd::algebra::linearSolverTypeName;
  using cfd::algebra::solverStatusName;
  std::string text = std::string(equation) + " linear solve failed: ";
  if (!result.fallback.attempted) {
    text += std::string(linearSolverTypeName(primaryType)) + " " +
            std::string(solverStatusName(result.status)) + " after " +
            std::to_string(result.iterations) + " iterations";
    return text;
  }
  text += "primary " + std::string(linearSolverTypeName(result.fallback.primaryType)) + " " +
          std::string(solverStatusName(result.fallback.primaryStatus)) + " after " +
          std::to_string(result.fallback.primaryIterations) + " iterations";
  for (const auto& attempt : result.fallback.attempts) {
    text += "; fallback " + std::string(linearSolverTypeName(attempt.type)) + " " +
            std::string(solverStatusName(attempt.status)) + " after " +
            std::to_string(attempt.iterations) + " iterations";
  }
  return text;
}

// ---------------------------------------------------------------------------
// OuterIterationMonitor
// ---------------------------------------------------------------------------

OuterIterationMonitor::OuterIterationMonitor(const SolverRobustnessSettings& settings,
                                             OuterConvergenceTolerances tolerances,
                                             Real initialVelocityRelaxation,
                                             Real initialPressureRelaxation)
    : settings_(settings),
      tolerances_(tolerances),
      u_(tolerances.velocity, settings.normalization.referenceIterations,
         settings.divergence.window, settings.divergence.startIteration),
      v_(tolerances.velocity, settings.normalization.referenceIterations,
         settings.divergence.window, settings.divergence.startIteration),
      p_(tolerances.pressure, settings.normalization.referenceIterations,
         settings.divergence.window, settings.divergence.startIteration),
      c_(tolerances.continuity, settings.normalization.referenceIterations,
         settings.divergence.window, settings.divergence.startIteration),
      distanceTracker_(1.0, 1, settings.stagnation.window, 1),
      controller_(settings.adaptiveRelaxation, initialVelocityRelaxation,
                  initialPressureRelaxation) {
  diagnostics_.convergenceCriterion = settings.convergenceCriterion;
}

Real OuterIterationMonitor::threshold(const ResidualTracker& tracker,
                                      Real normalizedTolerance) const {
  if (settings_.convergenceCriterion == ConvergenceCriterion::Absolute) {
    return tracker.floor();
  }
  return std::max(normalizedTolerance * tracker.reference(), tracker.floor());
}

bool OuterIterationMonitor::isConverged(const OuterResidualSample& sample) const {
  const bool turbulenceConverged =
      !sample.turbulence.has_value() || (*sample.turbulence <= tolerances_.turbulence);
  if (settings_.convergenceCriterion == ConvergenceCriterion::Absolute) {
    // Exactly the pre-P12-NUM-004 gate (same comparisons, same operands).
    return (sample.u <= tolerances_.velocity) && (sample.v <= tolerances_.velocity) &&
           (sample.pressure <= tolerances_.pressure) &&
           (sample.continuity <= tolerances_.continuity) &&
           (sample.globalImbalance <= tolerances_.continuity) && turbulenceConverged;
  }
  const bool finite = std::isfinite(sample.u) && std::isfinite(sample.v) &&
                      std::isfinite(sample.pressure) && std::isfinite(sample.continuity) &&
                      std::isfinite(sample.globalImbalance);
  return finite && (sample.u <= threshold(u_, settings_.normalization.velocityTolerance)) &&
         (sample.v <= threshold(v_, settings_.normalization.velocityTolerance)) &&
         (sample.pressure <= threshold(p_, settings_.normalization.pressureTolerance)) &&
         (sample.continuity <= tolerances_.continuity) &&
         (sample.globalImbalance <= tolerances_.continuity) && turbulenceConverged;
}

bool OuterIterationMonitor::diverging(const ResidualTracker& tracker, std::string_view name) {
  const Index window = tracker.windowCapacity();
  if (tracker.windowSize() < window) return false;
  const Real growth = settings_.divergence.growthFactor;
  const Real base = std::max(tracker.floor(), tracker.bestBeforeWindow());
  const Real windowMin = tracker.windowMin();
  if (std::isfinite(base) && windowMin >= growth * base) {
    diagnostics_.statusDetail =
        "divergence: every one of the last " + std::to_string(window) + " " + std::string(name) +
        " residuals is >= " + formatReal(growth) + " x its best value since iteration " +
        std::to_string(std::max<Index>(settings_.divergence.startIteration, 1)) + " (" +
        formatReal(base) + "); window minimum " + formatReal(windowMin) + ", latest " +
        formatReal(tracker.latest());
    return true;
  }
  bool increasing = true;
  for (Index ago = window - 1; ago >= 1; --ago) {
    if (!(tracker.valueAgo(ago - 1) > tracker.valueAgo(ago))) {
      increasing = false;
      break;
    }
  }
  const Real oldest = tracker.valueAgo(window - 1);
  if (increasing && tracker.valueAgo(0) >= growth * oldest) {
    diagnostics_.statusDetail =
        "divergence: the " + std::string(name) + " residual increased at each of the last " +
        std::to_string(window - 1) + " iterations, " + formatReal(oldest) + " -> " +
        formatReal(tracker.valueAgo(0)) + " (>= " + formatReal(growth) + "x)";
    return true;
  }
  return false;
}

OuterIterationVerdict OuterIterationMonitor::record(const OuterResidualSample& sample) {
  const RelaxationFactors used = controller_.current();
  diagnostics_.velocityRelaxationHistory.push_back(used.velocity);
  diagnostics_.pressureRelaxationHistory.push_back(used.pressure);

  u_.push(sample.u);
  v_.push(sample.v);
  p_.push(sample.pressure);
  c_.push(sample.continuity);
  diagnostics_.uNormalizedHistory.push_back(u_.normalized());
  diagnostics_.vNormalizedHistory.push_back(v_.normalized());
  diagnostics_.pressureNormalizedHistory.push_back(p_.normalized());
  diagnostics_.continuityNormalizedHistory.push_back(c_.normalized());
  diagnostics_.uReference = u_.effectiveReference();
  diagnostics_.vReference = v_.effectiveReference();
  diagnostics_.pressureReference = p_.effectiveReference();
  diagnostics_.continuityReference = c_.effectiveReference();

  const auto ratio = [](Real value, Real limit) {
    const Real r = value / limit;
    return std::isfinite(r) ? r : kInfinity;
  };
  distance_ =
      std::max({ratio(sample.u, threshold(u_, settings_.normalization.velocityTolerance)),
                ratio(sample.v, threshold(v_, settings_.normalization.velocityTolerance)),
                ratio(sample.pressure, threshold(p_, settings_.normalization.pressureTolerance)),
                ratio(sample.continuity, tolerances_.continuity),
                ratio(sample.globalImbalance, tolerances_.continuity)});
  if (sample.turbulence.has_value()) {
    distance_ = std::max(distance_, ratio(*sample.turbulence, tolerances_.turbulence));
  }
  diagnostics_.convergenceDistanceHistory.push_back(distance_);
  distanceTracker_.push(distance_);

  if (isConverged(sample)) {
    return OuterIterationVerdict::Converged;
  }

  const Index n = u_.count();
  // A residual NORM that overflowed while the fields stayed finite (they are
  // checked separately by the solvers -> NonFiniteState) is the extreme end
  // of a finite runaway; the windowed criteria below never see it (trackers
  // exclude non-finite values), so it is Diverging at once -- when enabled.
  if (settings_.divergence.enabled &&
      !(std::isfinite(sample.u) && std::isfinite(sample.v) && std::isfinite(sample.pressure) &&
        std::isfinite(sample.continuity) && std::isfinite(sample.globalImbalance))) {
    diagnostics_.statusDetail =
        "divergence: a residual norm overflowed to a non-finite value at iteration " +
        std::to_string(n) + " (u " + formatReal(sample.u) + ", v " + formatReal(sample.v) + ", p " +
        formatReal(sample.pressure) + ", continuity " + formatReal(sample.continuity) + ")";
    return OuterIterationVerdict::Diverging;
  }
  if (settings_.divergence.enabled &&
      n >= std::max<Index>(settings_.divergence.startIteration, 1) + settings_.divergence.window) {
    if (diverging(u_, "u") || diverging(v_, "v") || diverging(p_, "pressure") ||
        diverging(c_, "continuity")) {
      return OuterIterationVerdict::Diverging;
    }
  }

  if (settings_.stagnation.enabled && n >= settings_.stagnation.startIteration &&
      distanceTracker_.windowSize() == distanceTracker_.windowCapacity()) {
    const Real bestOld = distanceTracker_.bestBeforeWindow();
    const Real bestNew = distanceTracker_.windowMin();
    if (std::isfinite(bestOld) && bestOld > 0.0) {
      const Real improvement = (bestOld - bestNew) / bestOld;
      if (improvement < settings_.stagnation.minRelativeImprovement) {
        diagnostics_.statusDetail =
            "stagnation: the best convergence distance improved by " +
            formatReal(100.0 * improvement) + "% over the last " +
            std::to_string(settings_.stagnation.window) + " iterations (" + formatReal(bestOld) +
            " -> " + formatReal(bestNew) + "; threshold " +
            formatReal(100.0 * settings_.stagnation.minRelativeImprovement) + "%, converged at 1)";
        return OuterIterationVerdict::Stagnated;
      }
    }
  }

  lastAction_ = controller_.update(std::max({u_.normalized(), v_.normalized(), p_.normalized()}));
  diagnostics_.relaxationIncreases = controller_.increases();
  diagnostics_.relaxationDecreases = controller_.decreases();
  return OuterIterationVerdict::Continue;
}

OuterIterationDiagnostics OuterIterationMonitor::takeDiagnostics() {
  return std::move(diagnostics_);
}

}  // namespace cfd::solver
