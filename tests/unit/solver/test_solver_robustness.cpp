// P12-NUM-004: the shared outer-iteration robustness infrastructure
// (cfd/solver/SolverRobustness.hpp) on controlled, fully deterministic
// residual sequences -- exact expected values where the algorithm defines
// them, bounds where it defines bounds.
#include <gtest/gtest.h>

#include <chrono>
#include <cmath>
#include <cstdio>
#include <limits>
#include <vector>

#include "cfd/core/Exception.hpp"
#include "cfd/solver/SolverRobustness.hpp"

using cfd::Index;
using cfd::Real;
using cfd::solver::AdaptiveRelaxationController;
using cfd::solver::AdaptiveRelaxationSettings;
using cfd::solver::ConvergenceCriterion;
using cfd::solver::OuterConvergenceTolerances;
using cfd::solver::OuterIterationMonitor;
using cfd::solver::OuterIterationVerdict;
using cfd::solver::OuterResidualSample;
using cfd::solver::RelaxationAction;
using cfd::solver::ResidualTracker;
using cfd::solver::SolverRobustnessSettings;

namespace {

constexpr Real kNaN = std::numeric_limits<Real>::quiet_NaN();
constexpr Real kInf = std::numeric_limits<Real>::infinity();

// Same residual r for u, v, p and continuity (global imbalance 0).
OuterResidualSample uniform(Real r) { return OuterResidualSample{r, r, r, r, 0.0, std::nullopt}; }

OuterConvergenceTolerances tolerances(Real t = 1e-6) {
  return OuterConvergenceTolerances{t, t, t, 1e-6};
}

// Feeds `values` until the monitor stops; returns {verdict, 1-based iteration}.
std::pair<OuterIterationVerdict, Index> run(OuterIterationMonitor& monitor,
                                            const std::vector<Real>& values) {
  for (Index k = 0; k < values.size(); ++k) {
    const auto verdict = monitor.record(uniform(values[k]));
    if (verdict != OuterIterationVerdict::Continue) return {verdict, k + 1};
  }
  return {OuterIterationVerdict::Continue, values.size()};
}

AdaptiveRelaxationSettings adaptive(Real minV = 0.1, Real maxV = 0.9, Real minP = 0.05,
                                    Real maxP = 0.7) {
  return AdaptiveRelaxationSettings{true, minV, maxV, minP, maxP};
}

}  // namespace

// ===========================================================================
// ResidualNormalization
// ===========================================================================

TEST(ResidualNormalization, NonzeroBaseline) {
  ResidualTracker tracker(1e-8, 3, 10);
  for (const Real r : {0.5, 1.0, 0.8, 0.1, 0.02}) tracker.push(r);
  EXPECT_EQ(tracker.reference(), 1.0);  // max of the first 3
  EXPECT_EQ(tracker.effectiveReference(), 1.0);
  EXPECT_DOUBLE_EQ(tracker.normalized(), 0.02);
  EXPECT_EQ(tracker.best(), 0.02);
  // Values after the reference window never change the reference.
  tracker.push(5.0);
  EXPECT_EQ(tracker.reference(), 1.0);
  EXPECT_DOUBLE_EQ(tracker.normalized(), 5.0);
}

TEST(ResidualNormalization, ZeroBaseline) {
  // An equation that starts at exactly 0 (e.g. v of a cavity at rest): the
  // reference is 0, floored at the absolute tolerance -- no division by
  // zero, no infinity.
  ResidualTracker tracker(1e-8, 3, 10);
  for (int k = 0; k < 3; ++k) tracker.push(0.0);
  EXPECT_EQ(tracker.reference(), 0.0);
  EXPECT_EQ(tracker.effectiveReference(), 1e-8);
  EXPECT_EQ(tracker.normalized(), 0.0);
  tracker.push(1e-6);
  EXPECT_TRUE(std::isfinite(tracker.normalized()));
  EXPECT_DOUBLE_EQ(tracker.normalized(), 100.0);  // 1e-6 / 1e-8
  EXPECT_EQ(ResidualTracker::normalize(0.0, 0.0, 1e-8), 0.0);
}

TEST(ResidualNormalization, SmallBaseline) {
  // A reference far below the absolute tolerance would give a meaningless
  // enormous normalized value; the floor caps the denominator from below.
  ResidualTracker tracker(1e-8, 2, 10);
  tracker.push(1e-20);
  tracker.push(2e-20);
  EXPECT_EQ(tracker.reference(), 2e-20);
  EXPECT_EQ(tracker.effectiveReference(), 1e-8);
  tracker.push(1e-9);
  EXPECT_DOUBLE_EQ(tracker.normalized(), 0.1);  // not 5e10
}

TEST(ResidualNormalization, DecreasingIncreasingAndConstantSequences) {
  ResidualTracker decreasing(1e-10, 1, 4);
  ResidualTracker increasing(1e-10, 1, 4);
  ResidualTracker constant(1e-10, 1, 4);
  for (int k = 0; k < 6; ++k) {
    decreasing.push(std::pow(0.5, k));
    increasing.push(std::pow(2.0, k));
    constant.push(0.25);
  }
  EXPECT_DOUBLE_EQ(decreasing.normalized(), std::pow(0.5, 5));
  EXPECT_DOUBLE_EQ(increasing.normalized(), 32.0);
  EXPECT_DOUBLE_EQ(constant.normalized(), 1.0);
  // Window statistics over the last 4 values.
  EXPECT_DOUBLE_EQ(decreasing.windowMin(), std::pow(0.5, 5));
  EXPECT_DOUBLE_EQ(decreasing.windowMax(), std::pow(0.5, 2));
  EXPECT_DOUBLE_EQ(increasing.valueAgo(0), 32.0);
  EXPECT_DOUBLE_EQ(increasing.valueAgo(3), 4.0);
  EXPECT_DOUBLE_EQ(constant.windowMean(), 0.25);
  // Values 1 and 2 (1-based) left the window: bestBeforeWindow is their min.
  EXPECT_DOUBLE_EQ(decreasing.bestBeforeWindow(), 0.5);
  EXPECT_DOUBLE_EQ(increasing.bestBeforeWindow(), 1.0);
  EXPECT_EQ(decreasing.windowSize(), 4u);
  EXPECT_EQ(decreasing.count(), 6u);
}

TEST(ResidualNormalization, NonFiniteInput) {
  ResidualTracker tracker(1e-8, 5, 4);
  tracker.push(0.5);
  tracker.push(kNaN);
  EXPECT_FALSE(tracker.latestFinite());
  EXPECT_TRUE(tracker.nonFiniteSeen());
  EXPECT_TRUE(std::isnan(tracker.normalized()));  // flagged, not hidden
  EXPECT_EQ(tracker.reference(), 0.5);            // not contaminated
  EXPECT_EQ(tracker.best(), 0.5);
  EXPECT_EQ(tracker.windowSize(), 1u);
  tracker.push(kInf);
  EXPECT_EQ(tracker.reference(), 0.5);
  tracker.push(0.25);
  EXPECT_TRUE(tracker.latestFinite());
  EXPECT_DOUBLE_EQ(tracker.normalized(), 0.5);
  EXPECT_EQ(tracker.windowSize(), 2u);
  // For finite input the normalized value is always finite.
  for (const Real r : {0.0, 1e-300, 1e300}) {
    EXPECT_TRUE(std::isfinite(ResidualTracker::normalize(r, 0.0, 1e-12)));
  }
  EXPECT_THROW(ResidualTracker(0.0, 1, 1), cfd::InvalidArgumentError);
  EXPECT_THROW(ResidualTracker(1e-8, 1, 0), cfd::InvalidArgumentError);
}

// ===========================================================================
// ResidualTrend (stagnation / divergence through OuterIterationMonitor)
// ===========================================================================

namespace {

SolverRobustnessSettings detection() {
  SolverRobustnessSettings settings;
  settings.stagnation.enabled = true;
  settings.stagnation.window = 20;
  settings.stagnation.minRelativeImprovement = 0.01;
  settings.stagnation.startIteration = 30;
  settings.divergence.enabled = true;
  settings.divergence.window = 5;
  settings.divergence.growthFactor = 10.0;
  settings.divergence.startIteration = 10;
  return settings;
}

}  // namespace

TEST(ResidualTrend, Converging) {
  OuterIterationMonitor monitor(detection(), tolerances(), 0.7, 0.3);
  std::vector<Real> values;
  for (int k = 0; k < 2000; ++k) values.push_back(std::pow(0.97, k));
  const auto [verdict, iteration] = run(monitor, values);
  EXPECT_EQ(verdict, OuterIterationVerdict::Converged);
  // First k with 0.97^k <= 1e-6: k = ceil(ln(1e-6)/ln(0.97)) = 454 -> iteration 455.
  EXPECT_EQ(iteration, 455u);
}

// Slow but steady convergence is not stagnation: 0.9999 per iteration gives
// 1 - 0.9999^20 = 0.2% improvement per 20-iteration window (< 1%) ->
// stagnated; 0.999 per iteration gives 2.0% (> 1%) -> keeps going.
TEST(ResidualTrend, SlowConvergenceIsNotStagnation) {
  {
    OuterIterationMonitor monitor(detection(), tolerances(), 0.7, 0.3);
    std::vector<Real> values;
    for (int k = 0; k < 3000; ++k) values.push_back(std::pow(0.999, k));
    const auto [verdict, iteration] = run(monitor, values);
    EXPECT_EQ(verdict, OuterIterationVerdict::Continue) << "stopped at " << iteration;
  }
  {
    OuterIterationMonitor monitor(detection(), tolerances(), 0.7, 0.3);
    std::vector<Real> values;
    for (int k = 0; k < 3000; ++k) values.push_back(std::pow(0.9999, k));
    const auto [verdict, iteration] = run(monitor, values);
    EXPECT_EQ(verdict, OuterIterationVerdict::Stagnated);
    // Earliest possible: iteration >= startIteration (30) with a full window
    // and at least one value before it (n > 20) -> 30.
    EXPECT_EQ(iteration, 30u);
  }
}

TEST(ResidualTrend, Stagnating) {
  // Converges 0.8^k for 60 iterations, then plateaus exactly.
  std::vector<Real> values;
  for (int k = 0; k < 60; ++k) values.push_back(std::pow(0.8, k));
  const Real plateau = values.back();
  for (int k = 0; k < 500; ++k) values.push_back(plateau);
  OuterIterationMonitor monitor(detection(), tolerances(), 0.7, 0.3);
  const auto [verdict, iteration] = run(monitor, values);
  EXPECT_EQ(verdict, OuterIterationVerdict::Stagnated);
  // The window (20) must contain only plateau values: the last decreasing
  // value is #60, so the first window of plateau-only improvement 0 ends at
  // iteration 60 + 20 = 80.
  EXPECT_EQ(iteration, 80u);
  EXPECT_NE(monitor.diagnostics().statusDetail.find("stagnation"), std::string::npos);
}

TEST(ResidualTrend, Diverging) {
  // Smooth decay, then a finite runaway x1.5 per iteration from #20.
  std::vector<Real> values;
  for (int k = 0; k < 20; ++k) values.push_back(std::pow(0.9, k));
  for (int k = 1; k <= 60; ++k) values.push_back(values[19] * std::pow(1.5, k));
  OuterIterationMonitor monitor(detection(), tolerances(), 0.7, 0.3);
  const auto [verdict, iteration] = run(monitor, values);
  EXPECT_EQ(verdict, OuterIterationVerdict::Diverging);
  // Sustained runaway: 5 strictly increasing values with newest >= 10x the
  // oldest: 1.5^4 = 5.06 < 10 so the runaway rule needs the persistent-
  // excursion rule instead: every one of the last 5 values >= 10 x best
  // (value #20 = 0.9^19): 1.5^k >= 10 from k = 6 (#26), so the 5-window
  // #26..#30 first qualifies at iteration 30.
  EXPECT_EQ(iteration, 30u);
  EXPECT_TRUE(std::isfinite(values[iteration - 1]));
}

TEST(ResidualTrend, SustainedRunawayFromAPlateau) {
  // A flat plateau followed by x3 growth: the runaway rule (5 strictly
  // increasing values, newest >= 10x oldest: 3^4 = 81) fires at the 5th
  // growth value, independent of the best-value base.
  std::vector<Real> values(40, 1e-3);
  for (int k = 1; k <= 10; ++k) values.push_back(1e-3 * std::pow(3.0, k));
  SolverRobustnessSettings settings = detection();
  settings.stagnation.enabled = false;
  OuterIterationMonitor monitor(settings, tolerances(), 0.7, 0.3);
  const auto [verdict, iteration] = run(monitor, values);
  EXPECT_EQ(verdict, OuterIterationVerdict::Diverging);
  // Values #41..#44 are 3e-3..8.1e-2; the persistent rule (>= 10 x 1e-3 over
  // the whole window #40..#44) fails at #44 (value #40 = 1e-3), while the
  // runaway rule over #40..#44 holds: increasing and 8.1e-2 >= 10 x 1e-3.
  EXPECT_EQ(iteration, 44u);
}

TEST(ResidualTrend, SingleSpikeDoesNotTriggerDivergence) {
  // One enormous (finite) spike, and a burst of window - 1 = 4 high values:
  // neither is `window` consecutive iterations of growth.
  std::vector<Real> values;
  for (int k = 0; k < 100; ++k) values.push_back(1e-3 * std::pow(0.99, k));
  values[40] = 1e6;
  for (std::size_t k = 60; k < 64; ++k) values[k] = 1.0;
  SolverRobustnessSettings settings = detection();
  settings.stagnation.enabled = false;
  OuterIterationMonitor monitor(settings, tolerances(), 0.7, 0.3);
  const auto [verdict, iteration] = run(monitor, values);
  EXPECT_EQ(verdict, OuterIterationVerdict::Continue) << "stopped at " << iteration;
}

TEST(ResidualTrend, NonFiniteResidualIsDivergingWhenEnabled) {
  SolverRobustnessSettings settings = detection();
  OuterIterationMonitor monitor(settings, tolerances(), 0.7, 0.3);
  for (int k = 0; k < 3; ++k)
    EXPECT_EQ(monitor.record(uniform(1e-2)), OuterIterationVerdict::Continue);
  EXPECT_EQ(monitor.record(OuterResidualSample{kInf, 1e-2, 1e-2, 1e-2, 0.0, std::nullopt}),
            OuterIterationVerdict::Diverging);
  // Disabled: the pre-P12-NUM-004 behavior (not converged, keep iterating).
  OuterIterationMonitor quiet(SolverRobustnessSettings{}, tolerances(), 0.7, 0.3);
  EXPECT_EQ(quiet.record(OuterResidualSample{kInf, 1e-2, 1e-2, 1e-2, 0.0, std::nullopt}),
            OuterIterationVerdict::Continue);
}

TEST(ResidualTrend, DisabledDetectionNeverStops) {
  OuterIterationMonitor monitor(SolverRobustnessSettings{}, tolerances(), 0.7, 0.3);
  std::vector<Real> values(300, 1e-3);
  for (int k = 0; k < 100; ++k) values.push_back(std::pow(2.0, k));
  const auto [verdict, iteration] = run(monitor, values);
  EXPECT_EQ(verdict, OuterIterationVerdict::Continue);
  EXPECT_EQ(iteration, values.size());
}

// ===========================================================================
// Convergence policy (OuterIterationMonitor)
// ===========================================================================

TEST(ConvergencePolicy, DefaultIsExactlyTheAbsoluteGate) {
  OuterIterationMonitor atTolerance(SolverRobustnessSettings{}, tolerances(1e-6), 0.7, 0.3);
  EXPECT_EQ(atTolerance.record(OuterResidualSample{1e-6, 1e-6, 1e-6, 1e-6, 1e-6, std::nullopt}),
            OuterIterationVerdict::Converged);
  OuterIterationMonitor justAbove(SolverRobustnessSettings{}, tolerances(1e-6), 0.7, 0.3);
  const Real above = std::nextafter(1e-6, 1.0);
  EXPECT_EQ(justAbove.record(OuterResidualSample{1e-6, above, 1e-6, 1e-6, 1e-6, std::nullopt}),
            OuterIterationVerdict::Continue);
  OuterIterationMonitor turbulence(SolverRobustnessSettings{},
                                   OuterConvergenceTolerances{1e-6, 1e-6, 1e-6, 1e-4}, 0.7, 0.3);
  EXPECT_EQ(turbulence.record(OuterResidualSample{1e-7, 1e-7, 1e-7, 1e-7, 0.0, 2e-4}),
            OuterIterationVerdict::Continue);
  EXPECT_EQ(turbulence.record(OuterResidualSample{1e-7, 1e-7, 1e-7, 1e-7, 0.0, 1e-4}),
            OuterIterationVerdict::Converged);
}

// No hidden convergence: tiny NORMALIZED momentum/pressure residuals never
// converge a solve whose continuity / global mass imbalance is above its
// absolute tolerance, or whose residuals are non-finite.
TEST(ConvergencePolicy, NoHiddenConvergence) {
  SolverRobustnessSettings settings;
  settings.convergenceCriterion = ConvergenceCriterion::Normalized;
  const auto fresh = [&]() {
    OuterIterationMonitor monitor(settings, tolerances(1e-6), 0.7, 0.3);
    monitor.record(OuterResidualSample{1.0, 1.0, 1.0, 1e-9, 0.0, std::nullopt});  // reference 1.0
    return monitor;
  };
  {  // normalized u, v, p = 1e-5 < 1e-4, conservation fine -> converged
    auto monitor = fresh();
    EXPECT_EQ(monitor.record(OuterResidualSample{1e-5, 1e-5, 1e-5, 1e-9, 1e-9, std::nullopt}),
              OuterIterationVerdict::Converged);
  }
  {  // same, but continuity 1e-3 > 1e-6
    auto monitor = fresh();
    EXPECT_EQ(monitor.record(OuterResidualSample{1e-5, 1e-5, 1e-5, 1e-3, 1e-9, std::nullopt}),
              OuterIterationVerdict::Continue);
  }
  {  // same, but global mass imbalance 1e-3 > 1e-6
    auto monitor = fresh();
    EXPECT_EQ(monitor.record(OuterResidualSample{1e-5, 1e-5, 1e-5, 1e-9, 1e-3, std::nullopt}),
              OuterIterationVerdict::Continue);
  }
  {  // a non-finite residual never converges
    auto monitor = fresh();
    EXPECT_EQ(monitor.record(OuterResidualSample{1e-5, kNaN, 1e-5, 1e-9, 1e-9, std::nullopt}),
              OuterIterationVerdict::Continue);
  }
  {  // a zero-reference equation is judged against its absolute tolerance
    SolverRobustnessSettings s = settings;
    OuterIterationMonitor monitor(s, tolerances(1e-6), 0.7, 0.3);
    monitor.record(OuterResidualSample{1.0, 0.0, 1.0, 1e-9, 0.0, std::nullopt});
    // v reference 0 -> floored at 1e-6: v = 5e-6 is NOT converged even though
    // 5e-6 / tiny would be meaningless.
    EXPECT_EQ(monitor.record(OuterResidualSample{1e-5, 5e-6, 1e-5, 1e-9, 0.0, std::nullopt}),
              OuterIterationVerdict::Continue);
    EXPECT_EQ(monitor.record(OuterResidualSample{1e-5, 1e-6, 1e-5, 1e-9, 0.0, std::nullopt}),
              OuterIterationVerdict::Converged);
  }
}

// Normalization avoids false non-convergence caused only by an arbitrary
// absolute scale: the same history scaled by 1e6 converges at the same
// iteration with the normalized criterion, but never with the absolute one.
TEST(ConvergencePolicy, NormalizationRemovesArbitraryScale) {
  SolverRobustnessSettings normalized;
  normalized.convergenceCriterion = ConvergenceCriterion::Normalized;
  const auto history = [](Real scale) {
    std::vector<Real> v;
    for (int k = 0; k < 400; ++k) v.push_back(scale * std::pow(0.95, k));
    return v;
  };
  const auto feed = [&](const SolverRobustnessSettings& s, Real scale) {
    OuterIterationMonitor monitor(s, tolerances(1e-6), 0.7, 0.3);
    for (Index k = 0; k < 400; ++k) {
      const Real r = history(scale)[k];
      if (monitor.record(OuterResidualSample{r, r, r, 1e-9, 0.0, std::nullopt}) ==
          OuterIterationVerdict::Converged) {
        return k + 1;
      }
    }
    return Index{0};
  };
  const Index unit = feed(normalized, 1.0);
  const Index scaled = feed(normalized, 1e6);
  // 0.95^k <= 1e-4 (normalized tolerance, reference = first value = scale):
  // k = ceil(ln(1e-4)/ln(0.95)) = 180 -> iteration 181, for BOTH scales.
  EXPECT_EQ(unit, 181u);
  EXPECT_EQ(scaled, 181u);
  EXPECT_EQ(feed(SolverRobustnessSettings{}, 1e6), 0u);  // absolute: never
}

TEST(ConvergencePolicy, NormalizedHistoriesAreReportedAndFinite) {
  OuterIterationMonitor monitor(SolverRobustnessSettings{}, tolerances(1e-6), 0.7, 0.3);
  monitor.record(OuterResidualSample{0.1, 0.0, 0.2, 1e-9, 0.0, std::nullopt});
  monitor.record(OuterResidualSample{0.05, 1e-3, 0.1, 1e-10, 0.0, std::nullopt});
  const auto& d = monitor.diagnostics();
  ASSERT_EQ(d.uNormalizedHistory.size(), 2u);
  EXPECT_DOUBLE_EQ(d.uNormalizedHistory[1], 0.5);
  EXPECT_DOUBLE_EQ(d.vNormalizedHistory[0], 0.0);  // zero baseline
  EXPECT_DOUBLE_EQ(d.vNormalizedHistory[1], 1.0);  // reference grows during the first 5
  EXPECT_DOUBLE_EQ(d.pressureNormalizedHistory[1], 0.5);
  for (const auto* h : {&d.uNormalizedHistory, &d.vNormalizedHistory, &d.pressureNormalizedHistory,
                        &d.continuityNormalizedHistory, &d.convergenceDistanceHistory}) {
    for (const Real x : *h) EXPECT_TRUE(std::isfinite(x));
  }
  ASSERT_EQ(d.velocityRelaxationHistory.size(), 2u);
  EXPECT_EQ(d.velocityRelaxationHistory[0], 0.7);
  EXPECT_EQ(d.pressureRelaxationHistory[1], 0.3);
}

// ===========================================================================
// AdaptiveRelaxation
// ===========================================================================

TEST(AdaptiveRelaxation, ImprovingIncreasesGradually) {
  AdaptiveRelaxationController controller(adaptive(), 0.5, 0.2);
  std::vector<Real> velocity;
  for (int k = 0; k < 21; ++k) {
    controller.update(std::pow(0.9, k));
    velocity.push_back(controller.current().velocity);
  }
  // Update 1: Hold; improvements at updates 2..21: +5% after every 5
  // consecutive improvements (updates 6, 11, 16, 21).
  EXPECT_DOUBLE_EQ(velocity[4], 0.5);
  EXPECT_DOUBLE_EQ(velocity[5], 0.5 * 1.05);
  EXPECT_DOUBLE_EQ(velocity[9], 0.5 * 1.05);
  EXPECT_DOUBLE_EQ(velocity[10], 0.5 * 1.05 * 1.05);
  EXPECT_DOUBLE_EQ(velocity[20], 0.5 * std::pow(1.05, 4));
  EXPECT_DOUBLE_EQ(controller.current().pressure, 0.2 * std::pow(1.05, 4));
  EXPECT_EQ(controller.increases(), 4u);
  EXPECT_EQ(controller.decreases(), 0u);
}

TEST(AdaptiveRelaxation, GrowthReducesAlpha) {
  AdaptiveRelaxationController controller(adaptive(), 0.8, 0.5);
  controller.update(1.0);
  EXPECT_EQ(controller.update(1.1), RelaxationAction::Hold);      // +10%: mild, held
  EXPECT_EQ(controller.update(1.5), RelaxationAction::Decrease);  // +36%
  EXPECT_DOUBLE_EQ(controller.current().velocity, 0.8 * 0.7);
  EXPECT_DOUBLE_EQ(controller.current().pressure, 0.5 * 0.7);
  EXPECT_EQ(controller.decreases(), 1u);
}

TEST(AdaptiveRelaxation, PlateauHolds) {
  AdaptiveRelaxationController controller(adaptive(), 0.6, 0.3);
  for (int k = 0; k < 100; ++k) EXPECT_EQ(controller.update(0.01), RelaxationAction::Hold);
  EXPECT_EQ(controller.current().velocity, 0.6);
  EXPECT_EQ(controller.current().pressure, 0.3);
}

TEST(AdaptiveRelaxation, GrowingOscillationReducesAlphaDecayingZigzagDoesNot) {
  {  // up/down alternating with a non-decaying envelope (1, 1.1, 1.0, 1.15, 1.05)
    AdaptiveRelaxationController controller(adaptive(), 0.6, 0.3);
    RelaxationAction last = RelaxationAction::Hold;
    for (const Real m : {1.0, 1.1, 1.0, 1.15, 1.05}) last = controller.update(m);
    EXPECT_EQ(last, RelaxationAction::Decrease);
  }
  {  // a decaying zigzag (1, 1.05, 0.8, 0.84, 0.6): no decrease
    AdaptiveRelaxationController controller(adaptive(), 0.6, 0.3);
    for (const Real m : {1.0, 1.05, 0.8, 0.84, 0.6}) {
      EXPECT_NE(controller.update(m), RelaxationAction::Decrease);
    }
    EXPECT_EQ(controller.decreases(), 0u);
  }
}

TEST(AdaptiveRelaxation, RespectsBounds) {
  AdaptiveRelaxationController shrinking(adaptive(0.1, 0.9, 0.05, 0.7), 0.5, 0.3);
  Real m = 1.0;
  for (int k = 0; k < 50; ++k) {
    shrinking.update(m);
    m *= 2.0;  // relentless growth
    EXPECT_GE(shrinking.current().velocity, 0.1);
    EXPECT_GE(shrinking.current().pressure, 0.05);
  }
  EXPECT_DOUBLE_EQ(shrinking.current().velocity, 0.1);
  EXPECT_DOUBLE_EQ(shrinking.current().pressure, 0.05);
  AdaptiveRelaxationController growing(adaptive(0.1, 0.9, 0.05, 0.7), 0.5, 0.3);
  m = 1.0;
  for (int k = 0; k < 500; ++k) {
    growing.update(m);
    m *= 0.9;  // relentless improvement
    EXPECT_LE(growing.current().velocity, 0.9);
    EXPECT_LE(growing.current().pressure, 0.7);
    EXPECT_GT(growing.current().velocity, 0.0);
  }
  EXPECT_DOUBLE_EQ(growing.current().velocity, 0.9);
  EXPECT_DOUBLE_EQ(growing.current().pressure, 0.7);
}

// After growth at alpha, the controller recovers upward but never beyond
// kCeilingFactor x the factor that proved unstable (no hunting).
TEST(AdaptiveRelaxation, RecoveryAfterGrowthStopsBelowTheUnstableFactor) {
  AdaptiveRelaxationController controller(adaptive(0.1, 0.95, 0.05, 0.95), 0.9, 0.9);
  controller.update(1.0);
  controller.update(2.0);  // growth at 0.9 -> 0.63, ceiling 0.81
  EXPECT_DOUBLE_EQ(controller.current().velocity, 0.9 * 0.7);
  Real m = 2.0;
  for (int k = 0; k < 400; ++k) {
    m *= 0.95;
    controller.update(m);
  }
  EXPECT_DOUBLE_EQ(controller.current().velocity, 0.9 * 0.9);  // the ceiling, not 0.95
  EXPECT_DOUBLE_EQ(controller.current().pressure, 0.9 * 0.9);
}

TEST(AdaptiveRelaxation, DisabledPreservesFixedAlpha) {
  AdaptiveRelaxationSettings off = adaptive();
  off.enabled = false;
  AdaptiveRelaxationController controller(off, 0.73, 0.27);
  Real m = 1.0;
  for (int k = 0; k < 200; ++k) {
    EXPECT_EQ(controller.update(m), RelaxationAction::Hold);
    m *= (k % 2 == 0) ? 3.0 : 0.1;
  }
  EXPECT_EQ(controller.current().velocity, 0.73);
  EXPECT_EQ(controller.current().pressure, 0.27);
  // Through the monitor with default settings too.
  OuterIterationMonitor monitor(SolverRobustnessSettings{}, tolerances(), 0.73, 0.27);
  for (int k = 0; k < 50; ++k) monitor.record(uniform(std::pow(1.3, k % 7)));
  for (const Real a : monitor.diagnostics().velocityRelaxationHistory) EXPECT_EQ(a, 0.73);
  for (const Real a : monitor.diagnostics().pressureRelaxationHistory) EXPECT_EQ(a, 0.27);
}

TEST(AdaptiveRelaxation, Deterministic) {
  std::vector<Real> measures;
  Real m = 1.0;
  for (int k = 0; k < 300; ++k) {
    // Improvements, a mild rise every 7th step, a strong growth every 23rd.
    m *= (k % 23 == 0) ? 1.6 : ((k % 7 == 0) ? 1.04 : 0.93);
    measures.push_back(m);
  }
  AdaptiveRelaxationController a(adaptive(), 0.7, 0.3);
  AdaptiveRelaxationController b(adaptive(), 0.7, 0.3);
  for (const Real x : measures) {
    EXPECT_EQ(a.update(x), b.update(x));
    EXPECT_EQ(a.current().velocity, b.current().velocity);
    EXPECT_EQ(a.current().pressure, b.current().pressure);
  }
  EXPECT_GT(a.decreases(), 0u);
  EXPECT_GT(a.increases(), 0u);
}

// ===========================================================================
// Validation
// ===========================================================================

TEST(SolverRobustnessSettingsTest, DefaultsAreValidAndDisabled) {
  const SolverRobustnessSettings settings;
  EXPECT_NO_THROW(cfd::solver::validateSolverRobustnessSettings(settings, 0.7, 0.3));
  EXPECT_EQ(settings.convergenceCriterion, ConvergenceCriterion::Absolute);
  EXPECT_FALSE(settings.stagnation.enabled);
  EXPECT_FALSE(settings.divergence.enabled);
  EXPECT_FALSE(settings.adaptiveRelaxation.enabled);
  EXPECT_FALSE(settings.linearSolverFallback.enabled);
}

TEST(SolverRobustnessSettingsTest, InvalidValuesAreRejected) {
  using Mutator = void (*)(SolverRobustnessSettings&);
  const Mutator invalid[] = {
      [](SolverRobustnessSettings& s) { s.normalization.referenceIterations = 0; },
      [](SolverRobustnessSettings& s) { s.normalization.velocityTolerance = 0.0; },
      [](SolverRobustnessSettings& s) { s.normalization.pressureTolerance = 1.0; },
      [](SolverRobustnessSettings& s) { s.stagnation.window = 0; },
      [](SolverRobustnessSettings& s) { s.stagnation.window = 1; },
      [](SolverRobustnessSettings& s) {
        s.stagnation.window = cfd::solver::kMaxDetectionWindow + 1;
      },
      [](SolverRobustnessSettings& s) { s.stagnation.minRelativeImprovement = 0.0; },
      [](SolverRobustnessSettings& s) { s.divergence.window = 1; },
      [](SolverRobustnessSettings& s) { s.divergence.growthFactor = 1.0; },
      [](SolverRobustnessSettings& s) { s.divergence.growthFactor = kNaN; },
      [](SolverRobustnessSettings& s) { s.adaptiveRelaxation.minVelocity = 0.0; },
      [](SolverRobustnessSettings& s) { s.adaptiveRelaxation.maxVelocity = 1.1; },
      [](SolverRobustnessSettings& s) { s.adaptiveRelaxation.minPressure = 0.8; },  // > max 0.7
      [](SolverRobustnessSettings& s) { s.linearSolverFallback.maxAttempts = 4; },
      [](SolverRobustnessSettings& s) {  // initial velocity 0.7 outside [0.8, 0.9]
        s.adaptiveRelaxation.enabled = true;
        s.adaptiveRelaxation.minVelocity = 0.8;
      },
  };
  for (const Mutator mutate : invalid) {
    SolverRobustnessSettings settings;
    mutate(settings);
    EXPECT_THROW(cfd::solver::validateSolverRobustnessSettings(settings, 0.7, 0.3),
                 cfd::InvalidArgumentError);
  }
}

// ===========================================================================
// Performance: bookkeeping is O(window) per iteration with fixed memory.
// Printed measurement only (no timing assertion -- wall time is machine
// dependent); the asserted property is the bounded window storage.
// ===========================================================================

TEST(SolverRobustnessPerformance, BookkeepingIsFixedWindowCost) {
  // Every detector evaluated every iteration, none firing: a steady 0.5%
  // per-iteration decay (22% per 50-iteration window) that stays far above
  // the (tiny) tolerance for all updates.
  SolverRobustnessSettings settings = detection();
  settings.stagnation.window = 50;
  settings.stagnation.startIteration = 60;
  settings.adaptiveRelaxation = adaptive();
  settings.adaptiveRelaxation.enabled = true;
  OuterIterationMonitor monitor(settings, tolerances(1e-300), 0.7, 0.3);
  const Index updates = 100000;
  const auto start = std::chrono::steady_clock::now();
  Real r = 1e10;
  for (Index k = 0; k < updates; ++k) {
    ASSERT_EQ(monitor.record(OuterResidualSample{r, r, r, r, 0.0, std::nullopt}),
              OuterIterationVerdict::Continue);
    r *= 0.995;
  }
  const double seconds =
      std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
  std::printf(
      "\nOuterIterationMonitor: %llu updates (stagnation window 50, divergence window 5, "
      "adaptive on): %.3f s total, %.3f us per outer iteration\n",
      static_cast<unsigned long long>(updates), seconds,
      1e6 * seconds / static_cast<double>(updates));
  EXPECT_EQ(monitor.u().windowCapacity(), 5u);
  EXPECT_EQ(monitor.u().windowSize(), 5u);
}
