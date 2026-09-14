// P12-NUM-005: the three-grid convergence analysis (GridConvergence.hpp) on
// exact synthetic sequences phi(h) = phi_exact + C h^p -- every expected
// value below follows from that closed form (or, for GCIKnownCase, from the
// worked example published by Celik et al. 2008, Table 1).
#include <gtest/gtest.h>

#include <cmath>
#include <limits>

#include "cfd/core/Exception.hpp"
#include "cfd/validation/GridConvergence.hpp"

using cfd::Real;
using cfd::validation::analyzeGridConvergence;
using cfd::validation::ConvergenceClass;
using cfd::validation::GridConvergenceOptions;
using cfd::validation::GridConvergenceResult;
using cfd::validation::GridConvergenceStatus;
using cfd::validation::GridLevel;

namespace {

// phi(h) = exact + C h^p on grids h1 < h2 < h3.
GridConvergenceResult synthetic(Real exact, Real c, Real p, Real h1, Real h2, Real h3,
                                const GridConvergenceOptions& options = {}) {
  const auto phi = [&](Real h) { return exact + (c * std::pow(h, p)); };
  return analyzeGridConvergence(GridLevel{h1, phi(h1)}, GridLevel{h2, phi(h2)},
                                GridLevel{h3, phi(h3)}, options);
}

void expectNoNonFinite(const GridConvergenceResult& r) {
  for (const auto& value :
       {r.convergenceRatio, r.observedOrder, r.extrapolated21, r.extrapolated32,
        r.approximateRelativeError21, r.approximateRelativeError32, r.extrapolatedRelativeError21,
        r.extrapolatedRelativeError32, r.uncertainty21, r.uncertainty32, r.gci21, r.gci32,
        r.asymptoticRatio, r.orderRatio, r.oscillationUncertainty,
        r.relativeOscillationUncertainty}) {
    if (value.has_value()) {
      EXPECT_TRUE(std::isfinite(*value));
    }
  }
  EXPECT_TRUE(std::isfinite(r.r21) || r.status == GridConvergenceStatus::Invalid);
  EXPECT_FALSE(r.diagnostic.empty());
  EXPECT_FALSE(r.gridIndependenceReason.empty());
}

}  // namespace

TEST(GridConvergenceTest, RepresentativeGridSize) {
  // 64 x 8 cells on an 8 x 1 channel: dx = dy = 0.125.
  EXPECT_DOUBLE_EQ(cfd::validation::representativeGridSize(8.0, 64 * 8), 0.125);
  // Not 1/nx on a non-square domain: 10 x 10 on 2 x 1 -> sqrt(0.2 * 0.1).
  EXPECT_DOUBLE_EQ(cfd::validation::representativeGridSize(2.0, 100), std::sqrt(0.02));
  EXPECT_DOUBLE_EQ(cfd::validation::representativeGridSize(8.0, 64, 3), 0.5);
  EXPECT_THROW((void)cfd::validation::representativeGridSize(0.0, 10), cfd::InvalidArgumentError);
  EXPECT_THROW((void)cfd::validation::representativeGridSize(1.0, 0), cfd::InvalidArgumentError);
  EXPECT_THROW((void)cfd::validation::representativeGridSize(1.0, 10, 4),
               cfd::InvalidArgumentError);
}

TEST(GridConvergenceTest, FirstOrderSynthetic) {
  const auto r = synthetic(2.0, 0.5, 1.0, 0.1, 0.2, 0.4);
  ASSERT_EQ(r.convergence, ConvergenceClass::Monotonic);
  ASSERT_TRUE(r.observedOrder.has_value());
  EXPECT_NEAR(*r.observedOrder, 1.0, 1e-10);
  EXPECT_NEAR(*r.extrapolated21, 2.0, 1e-12);
  EXPECT_DOUBLE_EQ(r.r21, 2.0);
  EXPECT_DOUBLE_EQ(r.r32, 2.0);
  EXPECT_EQ(r.status, GridConvergenceStatus::MonotonicAsymptoticRangeUnknown);
  expectNoNonFinite(r);
}

TEST(GridConvergenceTest, SecondOrderSynthetic) {
  const auto r = synthetic(1.0, -3.0, 2.0, 0.05, 0.1, 0.2);
  ASSERT_TRUE(r.observedOrder.has_value());
  EXPECT_NEAR(*r.observedOrder, 2.0, 1e-10);
  EXPECT_NEAR(*r.extrapolated21, 1.0, 1e-12);
  EXPECT_NEAR(*r.extrapolated32, 1.0, 1e-12);
  // 0 < R < 1 for monotonic convergence: R = eps21/eps32 = 1/r^p = 1/4.
  EXPECT_NEAR(*r.convergenceRatio, 0.25, 1e-12);
  expectNoNonFinite(r);
}

TEST(GridConvergenceTest, ThirdOrderSynthetic) {
  const auto r = synthetic(5.0, 7.0, 3.0, 0.1, 0.15, 0.225);  // r = 1.5
  ASSERT_TRUE(r.observedOrder.has_value());
  EXPECT_NEAR(*r.observedOrder, 3.0, 1e-9);
  EXPECT_NEAR(*r.extrapolated21, 5.0, 1e-12);
}

// Unequal ratios are solved exactly (bisection on the general equation),
// not approximated by the equal-ratio formula -- which is shown to be wrong
// here by ~0.2 in p.
TEST(GridConvergenceTest, UnequalRefinementRatios) {
  for (const Real p : {1.0, 1.37, 2.0, 2.8}) {
    const auto r = synthetic(-0.4, 2.5, p, 0.1, 0.13, 0.2);  // r21 = 1.3, r32 = 1.538
    ASSERT_TRUE(r.observedOrder.has_value()) << p;
    EXPECT_NEAR(*r.observedOrder, p, 1e-9) << p;
    EXPECT_NEAR(*r.extrapolated21, -0.4, 1e-11) << p;
    EXPECT_NEAR(r.r21, 1.3, 1e-12);
    EXPECT_NEAR(r.r32, 0.2 / 0.13, 1e-12);
    EXPECT_TRUE(r.warnings.empty());
  }
  const auto r = synthetic(0.0, 1.0, 2.0, 0.1, 0.13, 0.2);
  const Real naive = std::log(r.epsilon32 / r.epsilon21) / std::log(r.r21);
  EXPECT_GT(std::abs(naive - 2.0), 0.2) << "the equal-ratio formula is not valid here";
}

TEST(GridConvergenceTest, RichardsonExactRecovery) {
  for (const Real p : {1.0, 1.5, 2.0, 3.0}) {
    for (const Real exact : {0.0, 1.0, -123.456, 1e6}) {
      const auto r = synthetic(exact, 0.3, p, 0.02, 0.03, 0.05);
      ASSERT_TRUE(r.extrapolated21.has_value());
      EXPECT_NEAR(*r.extrapolated21, exact, 1e-12 * std::max(1.0, std::abs(exact)))
          << "p=" << p << " exact=" << exact;
      // The extrapolated value is closer to the exact one than the fine grid.
      const Real fine = exact + (0.3 * std::pow(0.02, p));
      EXPECT_LT(std::abs(*r.extrapolated21 - exact), std::abs(fine - exact));
    }
  }
}

TEST(GridConvergenceTest, GCIKnownCase) {
  // Exact hand computation: phi = 1 + h^2 on h = 1, 2, 4 -> phi 2, 5, 17;
  // eps21 = 3, eps32 = 12, p = 2, r^p - 1 = 3:
  //   U21 = 1.25*3/3 = 1.25, GCI21 = 1.25/2 = 0.625
  //   U32 = 1.25*12/3 = 5,   GCI32 = 5/5  = 1.0
  //   ea21 = 3/2 = 1.5, eext21 = |1 - 2|/1 = 1.0
  const auto exact = synthetic(1.0, 1.0, 2.0, 1.0, 2.0, 4.0);
  EXPECT_NEAR(*exact.observedOrder, 2.0, 1e-10);
  EXPECT_NEAR(*exact.uncertainty21, 1.25, 1e-10);
  EXPECT_NEAR(*exact.gci21, 0.625, 1e-10);
  EXPECT_NEAR(*exact.uncertainty32, 5.0, 1e-10);
  EXPECT_NEAR(*exact.gci32, 1.0, 1e-10);
  EXPECT_NEAR(*exact.approximateRelativeError21, 1.5, 1e-12);
  EXPECT_NEAR(*exact.extrapolatedRelativeError21, 1.0, 1e-10);

  // Published example: Celik et al., J. Fluids Eng. 130 (2008) 078001,
  // Table 1 (phi = dimensionless reattachment length; r21 = 1.5,
  // r32 = 1.333): phi1 = 6.063, phi2 = 5.972, phi3 = 5.863 -> p = 1.53,
  // phi_ext21 = 6.1685, e_a21 = 1.5 %, e_ext21 = 1.7 %, GCI_fine21 = 2.2 %
  // (all to the paper's rounding).
  const auto celik =
      analyzeGridConvergence(GridLevel{1.0, 6.063}, GridLevel{1.5, 5.972}, GridLevel{2.0, 5.863});
  ASSERT_TRUE(celik.observedOrder.has_value());
  EXPECT_NEAR(*celik.observedOrder, 1.53, 0.005);
  EXPECT_NEAR(*celik.extrapolated21, 6.1685, 0.0006);
  EXPECT_NEAR(*celik.approximateRelativeError21, 0.015, 0.0005);
  EXPECT_NEAR(*celik.extrapolatedRelativeError21, 0.017, 0.0005);
  EXPECT_NEAR(*celik.gci21, 0.022, 0.0005);
  // The safety factor is explicit.
  GridConvergenceOptions fs3;
  fs3.safetyFactor = 3.0;
  const auto three = synthetic(1.0, 1.0, 2.0, 1.0, 2.0, 4.0, fs3);
  EXPECT_NEAR(*three.gci21, 3.0 * 0.625 / 1.25, 1e-10);
}

TEST(GridConvergenceTest, AsymptoticRatio) {
  GridConvergenceOptions formal2;
  formal2.formalOrder = 2.0;
  // Sequence of exactly the formal order: ratio exactly 1 -> Asymptotic.
  const auto exact = synthetic(1.0, 3.0, 2.0, 0.1, 0.2, 0.4, formal2);
  ASSERT_TRUE(exact.asymptoticRatio.has_value());
  EXPECT_NEAR(*exact.asymptoticRatio, 1.0, 1e-12);
  EXPECT_NEAR(*exact.orderRatio, 1.0, 1e-10);
  EXPECT_EQ(exact.status, GridConvergenceStatus::Asymptotic);
  // Unequal ratios too (the absolute-uncertainty form is exactly 1).
  const auto unequal = synthetic(1.0, 3.0, 2.0, 0.1, 0.13, 0.2, formal2);
  EXPECT_NEAR(*unequal.asymptoticRatio, 1.0, 1e-9);
  // A first-order sequence judged against formal order 2: with r = 2 the
  // ratio is r^p / r^pf = 2/4 = 0.5 -> not asymptotic.
  const auto first = synthetic(1.0, 3.0, 1.0, 0.1, 0.2, 0.4, formal2);
  EXPECT_NEAR(*first.asymptoticRatio, 0.5, 1e-12);
  EXPECT_EQ(first.status, GridConvergenceStatus::MonotonicNotAsymptotic);
  // The band is configurable: a sequence with p = 2.1 against pf = 2 has
  // ratio 2^0.1 = 1.072 -- inside +/-0.1, outside +/-0.05.
  const auto near = synthetic(1.0, 3.0, 2.1, 0.1, 0.2, 0.4, formal2);
  EXPECT_NEAR(*near.asymptoticRatio, std::pow(2.0, 0.1), 1e-10);
  EXPECT_EQ(near.status, GridConvergenceStatus::Asymptotic);
  GridConvergenceOptions tight = formal2;
  tight.asymptoticTolerance = 0.05;
  EXPECT_EQ(synthetic(1.0, 3.0, 2.1, 0.1, 0.2, 0.4, tight).status,
            GridConvergenceStatus::MonotonicNotAsymptotic);
  // Without a formal order the range is not assessed (the observed-order
  // version of the ratio is identically 1 -- uninformative).
  const auto unknown = synthetic(1.0, 3.0, 1.0, 0.1, 0.2, 0.4);
  EXPECT_FALSE(unknown.asymptoticRatio.has_value());
  EXPECT_EQ(unknown.status, GridConvergenceStatus::MonotonicAsymptoticRangeUnknown);
}

TEST(GridConvergenceTest, OscillatorySequenceDetected) {
  const auto r =
      analyzeGridConvergence(GridLevel{0.1, 1.0}, GridLevel{0.2, 1.1}, GridLevel{0.4, 0.95});
  EXPECT_EQ(r.convergence, ConvergenceClass::Oscillatory);
  EXPECT_EQ(r.status, GridConvergenceStatus::Oscillatory);
  EXPECT_FALSE(r.observedOrder.has_value());
  EXPECT_FALSE(r.extrapolated21.has_value());
  EXPECT_FALSE(r.gci21.has_value());
  ASSERT_TRUE(r.oscillationUncertainty.has_value());
  EXPECT_NEAR(*r.oscillationUncertainty, 0.075, 1e-15);
  EXPECT_LT(*r.convergenceRatio, 0.0);
  EXPECT_FALSE(r.gridIndependent);
  expectNoNonFinite(r);
}

TEST(GridConvergenceTest, DivergingSequenceDetected) {
  // Equal ratios, differences growing with refinement.
  const auto grows =
      analyzeGridConvergence(GridLevel{0.1, 1.3}, GridLevel{0.2, 1.1}, GridLevel{0.4, 1.0});
  EXPECT_EQ(grows.status, GridConvergenceStatus::Divergent);
  EXPECT_FALSE(grows.observedOrder.has_value());
  expectNoNonFinite(grows);
  // Unequal ratios: eps32/eps21 = 1.2 is below the p -> 0 limit
  // ln r32 / ln r21 = ln 2 / ln 1.25 = 3.1 -> no positive order.
  const auto slow =
      analyzeGridConvergence(GridLevel{0.1, 1.0}, GridLevel{0.125, 1.1}, GridLevel{0.25, 1.22});
  EXPECT_EQ(slow.status, GridConvergenceStatus::Divergent);
  EXPECT_FALSE(slow.observedOrder.has_value());
  EXPECT_NE(slow.diagnostic.find("ln r32 / ln r21"), std::string::npos);
  // Medium/coarse identical, fine different: growing differences.
  const auto flatCoarse =
      analyzeGridConvergence(GridLevel{0.1, 1.2}, GridLevel{0.2, 1.0}, GridLevel{0.4, 1.0});
  EXPECT_EQ(flatCoarse.status, GridConvergenceStatus::Divergent);
}

TEST(GridConvergenceTest, ZeroDifferenceHandled) {
  const auto same =
      analyzeGridConvergence(GridLevel{0.1, 2.0}, GridLevel{0.2, 2.0}, GridLevel{0.4, 2.0});
  EXPECT_EQ(same.status, GridConvergenceStatus::InsufficientSeparation);
  EXPECT_FALSE(same.observedOrder.has_value());
  EXPECT_FALSE(same.convergenceRatio.has_value());
  expectNoNonFinite(same);
  const auto fineSame =
      analyzeGridConvergence(GridLevel{0.1, 2.0}, GridLevel{0.2, 2.0}, GridLevel{0.4, 2.5});
  EXPECT_EQ(fineSame.status, GridConvergenceStatus::InsufficientSeparation);
  expectNoNonFinite(fineSame);
  // Differences inside a configured noise level are zero; outside it, the
  // same (tiny but resolved) sequence is analysed normally.
  GridConvergenceOptions noisy;
  noisy.absoluteNoise = 1e-6;
  const auto nearlyConverged = synthetic(1.0, 1e-5, 2.0, 0.1, 0.2, 0.4);
  EXPECT_NEAR(*nearlyConverged.observedOrder, 2.0, 1e-6);
  EXPECT_EQ(synthetic(1.0, 1e-5, 2.0, 0.1, 0.2, 0.4, noisy).status,
            GridConvergenceStatus::InsufficientSeparation);
  // Super-convergent / coincidental: eps21 negligible -> order not trusted.
  const auto huge =
      analyzeGridConvergence(GridLevel{0.1, 1.0}, GridLevel{0.2, 1.0 + 1e-9}, GridLevel{0.4, 2.0});
  EXPECT_EQ(huge.status, GridConvergenceStatus::InsufficientSeparation);
  EXPECT_NE(huge.diagnostic.find("maximumOrder"), std::string::npos);
}

TEST(GridConvergenceTest, ZeroSolutionQuantityHasNoRelativeMetrics) {
  // phi converging to 0 with phi1 exactly 0: relative errors/GCI undefined,
  // absolute uncertainty still available -- nothing NaN.
  const auto r =
      analyzeGridConvergence(GridLevel{0.1, 0.0}, GridLevel{0.2, 0.03}, GridLevel{0.4, 0.15});
  ASSERT_TRUE(r.observedOrder.has_value());
  EXPECT_FALSE(r.gci21.has_value());
  EXPECT_FALSE(r.approximateRelativeError21.has_value());
  ASSERT_TRUE(r.uncertainty21.has_value());
  EXPECT_TRUE(r.gci32.has_value());
  expectNoNonFinite(r);
}

TEST(GridConvergenceTest, InvalidGridSpacingRejected) {
  const auto bad = [](Real h1, Real h2, Real h3) {
    return analyzeGridConvergence(GridLevel{h1, 1.0}, GridLevel{h2, 1.1}, GridLevel{h3, 1.3});
  };
  for (const auto& r : {bad(0.0, 0.2, 0.4), bad(-0.1, 0.2, 0.4), bad(0.2, 0.1, 0.4),
                        bad(0.1, 0.4, 0.2), bad(0.1, 0.1, 0.4), bad(0.1, 0.105, 0.4)}) {
    EXPECT_EQ(r.status, GridConvergenceStatus::Invalid) << r.diagnostic;
    EXPECT_FALSE(r.observedOrder.has_value());
    expectNoNonFinite(r);
  }
  EXPECT_NE(bad(0.1, 0.105, 0.4).diagnostic.find("nearly identical"), std::string::npos);
  // 1.1 <= r < 1.3: accepted with a warning.
  const auto modest = synthetic(1.0, 1.0, 2.0, 0.1, 0.12, 0.144);
  EXPECT_NE(modest.status, GridConvergenceStatus::Invalid);
  EXPECT_FALSE(modest.warnings.empty());
}

TEST(GridConvergenceTest, NonFiniteInputRejected) {
  const Real nan = std::numeric_limits<Real>::quiet_NaN();
  const Real inf = std::numeric_limits<Real>::infinity();
  for (const auto& r :
       {analyzeGridConvergence(GridLevel{0.1, nan}, GridLevel{0.2, 1.0}, GridLevel{0.4, 1.1}),
        analyzeGridConvergence(GridLevel{0.1, 1.0}, GridLevel{0.2, inf}, GridLevel{0.4, 1.1}),
        analyzeGridConvergence(GridLevel{inf, 1.0}, GridLevel{0.2, 1.0}, GridLevel{0.4, 1.1}),
        analyzeGridConvergence(GridLevel{0.1, 1.0}, GridLevel{nan, 1.0}, GridLevel{0.4, 1.1})}) {
    EXPECT_EQ(r.status, GridConvergenceStatus::Invalid);
    EXPECT_EQ(r.convergence, ConvergenceClass::Invalid);
    EXPECT_FALSE(r.gridIndependent);
    expectNoNonFinite(r);
  }
  GridConvergenceOptions badOptions;
  badOptions.formalOrder = -1.0;
  EXPECT_EQ(synthetic(1.0, 1.0, 2.0, 0.1, 0.2, 0.4, badOptions).status,
            GridConvergenceStatus::Invalid);
}

TEST(GridConvergenceTest, GridIndependenceCriterion) {
  GridConvergenceOptions options;
  options.formalOrder = 2.0;
  // Unconfigured -> never grid independent, with the reason.
  const auto none = synthetic(1.0, 0.1, 2.0, 0.01, 0.02, 0.04, options);
  EXPECT_FALSE(none.gridIndependent);
  EXPECT_NE(none.gridIndependenceReason.find("no grid-independence threshold"), std::string::npos);
  // GCI21 = 1.25 * 0.1*(0.0004-0.0001)/3 / (1.00001) ~ 1.25e-5.
  options.gridIndependenceThreshold = 1e-3;
  const auto yes = synthetic(1.0, 0.1, 2.0, 0.01, 0.02, 0.04, options);
  EXPECT_TRUE(yes.gridIndependent) << yes.gridIndependenceReason;
  options.gridIndependenceThreshold = 1e-6;
  const auto tooStrict = synthetic(1.0, 0.1, 2.0, 0.01, 0.02, 0.04, options);
  EXPECT_FALSE(tooStrict.gridIndependent);
  EXPECT_NE(tooStrict.gridIndependenceReason.find("> threshold"), std::string::npos);
  // Not asymptotic -> not grid independent however small the GCI.
  options.gridIndependenceThreshold = 1.0;
  const auto notAsymptotic = synthetic(1.0, 0.1, 1.0, 0.01, 0.02, 0.04, options);
  EXPECT_FALSE(notAsymptotic.gridIndependent);
  EXPECT_NE(notAsymptotic.gridIndependenceReason.find("not asymptotic"), std::string::npos);
}

TEST(GridConvergenceTest, ObservedOrderSolverMatchesClosedFormForEqualRatios) {
  for (const Real ratio : {1.1, 2.0, 4.0, 8.0, 100.0}) {
    const auto solution = cfd::validation::solveObservedOrder(2.0, 2.0, ratio, 20.0);
    ASSERT_TRUE(solution.order.has_value());
    EXPECT_NEAR(*solution.order, std::log(ratio) / std::log(2.0), 1e-12) << ratio;
    EXPECT_GT(solution.iterations, 0u);
    EXPECT_LE(solution.iterations, 200u);
  }
  EXPECT_FALSE(cfd::validation::solveObservedOrder(2.0, 2.0, 1.0, 20.0).order.has_value());
  EXPECT_TRUE(cfd::validation::solveObservedOrder(2.0, 2.0, 1e9, 20.0).exceedsMaximum);
}
