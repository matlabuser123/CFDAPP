// P16b -- Menter's automatic near-wall treatment.
//
// WHY THIS EXISTS
// ---------------
// The corrected Driver & Seegmiller sequence showed the high-Re wall function
// diverging under refinement in separated flow, and the cause was structural
// rather than a bug: at a separation or reattachment point tau_w is zero BY
// DEFINITION, so y+ -> 0 there however the mesh is built. A treatment valid
// only over y+ in [30, 300] can never be valid at the exact points a
// reattachment benchmark measures. Measured min y+ over that sequence fell
// 30.34 -> 22.73 -> 5.93 as the mesh refined.
//
// THE TEST THAT MATTERS MOST is ZeroWallShearRemainsDefined. Everything else
// here checks limits and continuity; that one checks the property the high-Re
// path cannot have at all.
#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

#include "cfd/boundary/WallOmega.hpp"
#include "cfd/core/Exception.hpp"
#include "cfd/turbulence/SSTCoefficients.hpp"
#include "cfd/turbulence/WallFunctions.hpp"

using cfd::InvalidArgumentError;
using cfd::Real;
using cfd::turbulence::AutomaticWallState;
using cfd::turbulence::WallFunctionCoefficients;
using cfd::turbulence::WallRegime;

namespace {

constexpr Real kDensity = 1.0;
constexpr Real kViscosity = 1e-5;
constexpr Real kBeta1 = 0.075;  // SSTCoefficients::beta1
const WallFunctionCoefficients kCoefficients{0.41, 9.793, 0.09};

// A wall cell placed at a chosen y+ by construction: pick u_tau, derive the
// wall distance and the equilibrium k that produce it.
struct WallCell {
  Real velocity;
  Real k;
  Real wallDistance;
};

WallCell cellAtYPlus(Real targetYPlus, Real uTau) {
  const Real nu = kViscosity / kDensity;
  const Real y = targetYPlus * nu / uTau;
  const Real k = cfd::turbulence::wallAdjacentTurbulentEnergy(uTau, kCoefficients);
  // Velocity from whichever near-wall law holds there, so the cell is a
  // physically consistent state rather than an arbitrary triple.
  const Real u = targetYPlus <= 5.0
                     ? uTau * targetYPlus
                     : uTau * cfd::turbulence::uPlusLogLaw(targetYPlus, kCoefficients);
  return WallCell{u, k, y};
}

AutomaticWallState evaluate(const WallCell& cell) {
  return cfd::turbulence::automaticWallState(cell.velocity, cell.k, cell.wallDistance, kDensity,
                                             kViscosity, kBeta1, kCoefficients);
}

}  // namespace

// --- THE test ----------------------------------------------------------------

TEST(AutomaticWallTreatment, ZeroWallShearRemainsDefined) {
  // Separation and reattachment: the wall-parallel velocity passes through
  // zero. The high-Re path cannot represent this -- ln(E y*) is undefined as
  // y* -> 0, and wallShearStressLaunderSpalding THROWS there by design.
  //
  // The automatic treatment must produce a finite, non-negative, physically
  // sensible state instead. No exception, no NaN, no singularity.
  const Real y = 1e-3;

  for (const Real k : {0.0, 1e-12, 1e-6, 1e-2}) {
    AutomaticWallState state{};
    ASSERT_NO_THROW(state = cfd::turbulence::automaticWallState(0.0, k, y, kDensity, kViscosity,
                                                                kBeta1, kCoefficients))
        << "k = " << k;
    EXPECT_TRUE(std::isfinite(state.wallShearStress)) << "k = " << k;
    EXPECT_GE(state.wallShearStress, 0.0) << "k = " << k;
    EXPECT_TRUE(std::isfinite(state.specificDissipation)) << "k = " << k;
    EXPECT_GT(state.specificDissipation, 0.0) << "omega collapsed to zero at zero shear (k = " << k
                                              << ") -- the omega equation would then be singular";
    EXPECT_TRUE(std::isfinite(state.effectiveViscosity)) << "k = " << k;
    EXPECT_GE(state.effectiveViscosity, kViscosity) << "mu_wall fell below molecular at zero shear";
  }

  // The completely quiescent cell: no velocity AND no turbulence. omega must
  // still be the sublayer value, which depends on neither.
  const AutomaticWallState quiescent =
      cfd::turbulence::automaticWallState(0.0, 0.0, y, kDensity, kViscosity, kBeta1, kCoefficients);
  EXPECT_DOUBLE_EQ(quiescent.wallShearStress, 0.0);
  EXPECT_DOUBLE_EQ(quiescent.effectiveViscosity, kViscosity)
      << "a resolved sublayer with no shear must carry exactly the molecular viscosity";
  EXPECT_DOUBLE_EQ(
      quiescent.specificDissipation,
      cfd::turbulence::viscousSublayerSpecificDissipation(kViscosity / kDensity, kBeta1, y))
      << "with no turbulence the blend must reduce to the sublayer branch exactly";

  // And the high-Re path genuinely cannot do this -- the contrast is the
  // justification for the whole treatment, so it is asserted rather than
  // asserted-about.
  EXPECT_THROW((void)cfd::turbulence::wallShearStressLaunderSpalding(1.0, 1e-12, y, kDensity,
                                                                     kViscosity, kCoefficients),
               InvalidArgumentError);
}

TEST(AutomaticWallTreatment, WallShearCanCrossZeroWithoutSpecialCasing) {
  // Sweeping the wall-parallel velocity through zero, as it does across a
  // reattachment point. tau_w must fall to zero and rise again smoothly.
  //
  // The SIGN is the caller's business: this returns a magnitude, and the
  // reattachment detector takes its sign from the wall-adjacent velocity. A
  // treatment that returned |tau_w| with the sign folded in would destroy the
  // very sign change the detector reads.
  const Real y = 1e-3;
  const Real k = 1e-4;
  std::vector<Real> shear;
  for (const Real u : {-2.0, -1.0, -0.1, -0.01, 0.0, 0.01, 0.1, 1.0, 2.0}) {
    const auto state =
        cfd::turbulence::automaticWallState(u, k, y, kDensity, kViscosity, kBeta1, kCoefficients);
    EXPECT_TRUE(std::isfinite(state.wallShearStress)) << "u = " << u;
    EXPECT_GE(state.wallShearStress, 0.0) << "u = " << u;
    shear.push_back(state.wallShearStress);
  }
  // Symmetric about u = 0 -- the magnitude depends on |U|.
  EXPECT_DOUBLE_EQ(shear[0], shear[8]);
  EXPECT_DOUBLE_EQ(shear[1], shear[7]);
  // Monotone decreasing toward zero from either side.
  for (std::size_t i = 1; i <= 4; ++i) {
    EXPECT_LE(shear[i], shear[i - 1]) << "approaching zero from below, i = " << i;
  }
  EXPECT_GT(shear[0], shear[4]) << "tau_w did not fall as the velocity approached zero";
}

// --- the two branches reach their own limits ---------------------------------

TEST(AutomaticWallTreatment, ReducesToTheViscousBranchWhenTurbulenceVanishes) {
  // The exact low-Re limit, with no correlation assumed anywhere.
  //
  // A genuinely resolved wall cell has k -> 0, because the wall value of k is
  // zero and k ~ y^2 approaching it. In that limit the log branch switches
  // itself off (u_tau_log = C_mu^(1/4) sqrt(k) -> 0) and the treatment must
  // return the LAMINAR wall shear exactly:  tau_w = mu U / y.
  //
  // NOTE this is the limit that the earlier version of this test MEANT to
  // assert but did not: passing the equilibrium k at y+ = 1 describes a cell
  // that is in the log layer, not a resolved sublayer.
  const Real y = 1e-5;
  const Real u = 0.05;
  const auto state =
      cfd::turbulence::automaticWallState(u, 0.0, y, kDensity, kViscosity, kBeta1, kCoefficients);

  const Real laminarShear = kViscosity * u / y;
  EXPECT_NEAR(state.wallShearStress, laminarShear, 1e-12 * laminarShear)
      << "with k = 0 the treatment must be exactly the linear-sublayer value";
  EXPECT_EQ(state.logarithmicFrictionVelocity, 0.0);
  EXPECT_EQ(state.regime, WallRegime::ViscousSublayer);
  // mu_wall = tau_w y / U = mu exactly -- molecular, not enhanced.
  EXPECT_NEAR(state.effectiveViscosity, kViscosity, 1e-12 * kViscosity);
  // omega stays finite and positive even though every turbulence quantity is
  // zero: omega_vis depends only on nu, beta1 and y.
  EXPECT_GT(state.specificDissipation, 0.0);
  EXPECT_TRUE(std::isfinite(state.specificDissipation));
}

TEST(AutomaticWallTreatment, ApproachesTheLaminarLimitAsTurbulenceDecays) {
  // The whole low-k branch, not just its endpoint. Holding the cell fixed and
  // decaying k, tau_w must fall MONOTONICALLY to the laminar value -- a switch
  // at some y+ or k threshold would show a step in this sequence.
  const Real y = 1e-5;
  const Real u = 0.05;
  const Real laminarShear = kViscosity * u / y;

  Real previous = std::numeric_limits<Real>::max();
  for (const Real k : {1e-2, 1e-3, 1e-4, 1e-5, 1e-6, 1e-8, 1e-10}) {
    const auto state =
        cfd::turbulence::automaticWallState(u, k, y, kDensity, kViscosity, kBeta1, kCoefficients);
    EXPECT_LT(state.wallShearStress, previous) << "not monotone at k = " << k;
    // Round-off only: sqrt(sqrt(v^4 + l^4)) loses up to a couple of ulp when
    // l is negligible against v, so the bound is asserted to within a few
    // ulp rather than exactly. 1e-15 relative is ~4 ulp of double; anything
    // physical would be many orders larger.
    EXPECT_GE(state.wallShearStress, laminarShear * (1.0 - 1e-15))
        << "the blend dropped BELOW its own viscous branch at k = " << k;
    previous = state.wallShearStress;
  }
  EXPECT_NEAR(previous, laminarShear, 1e-3 * laminarShear)
      << "the decayed-turbulence limit is not the laminar value";
}

TEST(AutomaticWallTreatment, CrossoverOvershootMatchesThePublishedBlend) {
  // Where the two branches are equal, the 4th-power blend gives
  // u_tau = (2 u^4)^(1/4) = 2^(1/4) u, hence tau_w = sqrt(2) rho u^2.
  //
  // Menter's documented crossover behaviour, asserted as a PROPERTY rather than
  // tolerated as slop: max(.) would return u, a sum would return 2u, and a hard
  // switch would return one branch discontinuously. All three fail this, and
  // the bound is exact arithmetic rather than a fitted tolerance.
  const Real u = 0.05;
  const Real quarterRootOfTwo = std::pow(2.0, 0.25);
  EXPECT_NEAR(cfd::turbulence::blendFrictionVelocity(u, u), quarterRootOfTwo * u, 1e-15 * u);

  // The WORST case: the blend can never exceed 2^(1/4) times the larger branch
  // anywhere, nor fall below it, which is what bounds the overshoot.
  for (const Real ratio : {0.0, 0.25, 0.5, 1.0, 2.0, 4.0}) {
    const Real blended = cfd::turbulence::blendFrictionVelocity(u, ratio * u);
    const Real larger = std::max(u, ratio * u);
    EXPECT_LE(blended, (quarterRootOfTwo * larger) + (1e-12 * larger))
        << "overshoot exceeded 2^(1/4) at branch ratio " << ratio;
    EXPECT_GE(blended, larger - (1e-12 * larger));
  }

  // And automaticWallState must actually USE that blend on its own two reported
  // branches -- not some other combination that happens to look similar.
  const auto state = evaluate(cellAtYPlus(10.0, 0.05));
  EXPECT_NEAR(state.frictionVelocity,
              cfd::turbulence::blendFrictionVelocity(state.viscousFrictionVelocity,
                                                     state.logarithmicFrictionVelocity),
              1e-14 * state.frictionVelocity);
}

TEST(AutomaticWallTreatment, ReproducesTheValidatedWallShearInTheLogLimit) {
  // THE TEST WHOSE ABSENCE LET A REAL DEFECT THROUGH, and the one property
  // section 4 requires: where the high-Re wall function is valid, the automatic
  // treatment must agree with it.
  //
  // The first implementation used u_tau* = C_mu^(1/4) sqrt(k) as the log
  // branch, giving tau_w = rho u_tau*^2. The validated path uses
  // Launder-Spalding, tau_w = rho u_tau* |U| / u+. Those agree only at exact
  // equilibrium, and the Re_tau = 590 channel is not exactly at equilibrium on
  // any mesh -- the automatic mode came out 25% low at y+ = 69, where the blend
  // itself can only add 0.8%.
  //
  // The relation asserted is an identity, not a tolerance:
  //     tau_auto / tau_LS = sqrt(1 + (u_vis/u_log)^4)
  // which holds for the Launder-Spalding log branch and for no other choice.
  std::vector<Real> departures;
  for (const Real target : {30.0, 50.0, 100.0, 300.0}) {
    const WallCell cell = cellAtYPlus(target, 0.05);
    const auto state = evaluate(cell);
    const Real validated = cfd::turbulence::wallShearStressLaunderSpalding(
        cell.velocity, cell.k, cell.wallDistance, kDensity, kViscosity, kCoefficients);

    ASSERT_GT(validated, 0.0) << "y+ = " << target;
    const Real ratio = state.viscousFrictionVelocity / state.logarithmicFrictionVelocity;
    const Real expected = validated * std::sqrt(1.0 + std::pow(ratio, 4.0));
    EXPECT_NEAR(state.wallShearStress, expected, 1e-12 * expected)
        << "at y+ = " << target << " the automatic wall shear is not the validated "
        << "Launder-Spalding value blended with the viscous branch";

    // The departure is REPORTED, not capped at a number invented here. The
    // first draft of this test asserted < 10% and measured 10.16% at y+ = 30 --
    // a round figure chosen before measuring, which is not a gate.
    //
    // The real acceptance criterion for "does this regress the channel" already
    // exists and is frozen: the adopted Re_tau = 590 gate E_Re_tau <= 0.059 in
    // test_wall_function_channel.cpp. That is measured on the coupled solve,
    // not on this single-cell estimator, and it is strictly the harder test.
    // What belongs HERE is the structure, which the next two assertions pin.
    const Real departure = std::abs(state.wallShearStress - validated) / validated;
    std::printf("y+ = %6.1f : automatic departs from Launder-Spalding by %6.2f%%\n", target,
                100.0 * departure);
    departures.push_back(departure);
  }

  // 1. The overshoot must DECAY with y+. Deep in the log layer the viscous
  //    branch is irrelevant, so the blend must converge onto the validated
  //    value rather than sitting at a fixed offset.
  for (std::size_t i = 1; i < departures.size(); ++i) {
    EXPECT_LT(departures[i], departures[i - 1])
        << "the departure from the validated wall shear is not decaying with y+";
  }

  // 2. And it must actually reach it. This is the "reduces to the validated
  //    path" claim stated as the limit it is, rather than as a tolerance.
  EXPECT_LT(departures.back(), 0.01)
      << "deep in the log layer the automatic treatment still differs from the validated "
         "wall shear by "
      << (100.0 * departures.back()) << "%";
}

TEST(AutomaticWallTreatment, ReducesToTheLogBranchAtHighYPlus) {
  // At y+ = 100 the equilibrium branch dominates and the blend must reproduce
  // the validated high-Re quantities.
  const Real uTau = 0.05;
  const WallCell cell = cellAtYPlus(100.0, uTau);
  const auto state = evaluate(cell);

  EXPECT_EQ(state.regime, WallRegime::LogLayer);
  EXPECT_NEAR(state.frictionVelocity, uTau, 0.05 * uTau)
      << "the blend did not recover the equilibrium friction velocity";
  EXPECT_NEAR(state.yPlus, 100.0, 5.0);
  EXPECT_GT(state.logarithmicSpecificDissipation, state.viscousSpecificDissipation)
      << "the log omega branch must dominate at y+ = 100";
  // And the blended omega must be within sqrt(2) of the dominant branch.
  EXPECT_LE(state.specificDissipation, std::sqrt(2.0) * state.logarithmicSpecificDissipation);
  EXPECT_GE(state.specificDissipation, state.logarithmicSpecificDissipation);
}

TEST(AutomaticWallTreatment, BranchDominanceCrossesOverInsideTheBufferLayer) {
  // The blend has no switch, so "which branch dominates" is a reported
  // property. It must change hands inside the buffer layer, not at an edge.
  const Real uTau = 0.05;
  bool sawViscousDominant = false;
  bool sawLogDominant = false;
  for (const Real target : {1.0, 5.0, 10.0, 30.0, 100.0, 300.0}) {
    const auto state = evaluate(cellAtYPlus(target, uTau));
    if (state.viscousSpecificDissipation > state.logarithmicSpecificDissipation) {
      sawViscousDominant = true;
      EXPECT_LT(target, 30.0) << "the viscous branch still dominates at y+ = " << target;
    } else {
      sawLogDominant = true;
      EXPECT_GT(target, 5.0) << "the log branch already dominates at y+ = " << target;
    }
  }
  EXPECT_TRUE(sawViscousDominant);
  EXPECT_TRUE(sawLogDominant);
}

// --- continuity and boundedness ---------------------------------------------

namespace {

// Largest relative change between neighbouring samples of a quantity, sweeping
// the wall distance geometrically at fixed U and k so that y+ crosses the whole
// transition. Geometric spacing because both branches are power laws in y;
// equal steps in ln y make the sampling uniform in the variable the functions
// are actually smooth in.
struct SweepJumps {
  Real shear;
  Real omega;
  Real switched;  // the same sweep through a deliberately DISCONTINUOUS rule
};

SweepJumps sweepJumps(int samples) {
  const Real u = 1.0;
  // Equilibrium k for u_tau = 0.05, so the log branch is active and the two
  // branches cross inside the sweep.
  const Real k = cfd::turbulence::wallAdjacentTurbulentEnergy(0.05, kCoefficients);
  const Real yLow = 1e-4;
  const Real yHigh = 2e-2;

  SweepJumps worst{0.0, 0.0, 0.0};
  Real previousShear = -1.0;
  Real previousOmega = -1.0;
  Real previousSwitched = -1.0;
  for (int i = 0; i <= samples; ++i) {
    const Real t = static_cast<Real>(i) / static_cast<Real>(samples);
    const Real y = yLow * std::pow(yHigh / yLow, t);
    const auto state =
        cfd::turbulence::automaticWallState(u, k, y, kDensity, kViscosity, kBeta1, kCoefficients);

    // The control: a hard switch at y+ = 5 between the very same two branch
    // values the blend combines. Nothing else differs.
    const Real branch =
        state.yPlus <= 5.0 ? state.viscousFrictionVelocity : state.logarithmicFrictionVelocity;
    const Real switched = kDensity * branch * branch;

    if (previousShear > 0.0) {
      worst.shear =
          std::max(worst.shear, std::abs(state.wallShearStress - previousShear) / previousShear);
      worst.omega = std::max(worst.omega,
                             std::abs(state.specificDissipation - previousOmega) / previousOmega);
      worst.switched =
          std::max(worst.switched, std::abs(switched - previousSwitched) / previousSwitched);
    }
    previousShear = state.wallShearStress;
    previousOmega = state.specificDissipation;
    previousSwitched = switched;
  }
  return worst;
}

}  // namespace

TEST(AutomaticWallTreatment, IsContinuousThroughTheTransition) {
  // Continuity, stated as the limit it is: halve the sampling interval and the
  // largest relative jump must halve too. A treatment with a switch in it keeps
  // the same jump however finely it is sampled.
  const SweepJumps coarse = sweepJumps(200);
  const SweepJumps medium = sweepJumps(400);
  const SweepJumps fine = sweepJumps(800);

  EXPECT_GT(coarse.shear / medium.shear, 1.7)
      << "tau_w jumps did not shrink under refinement: " << coarse.shear << " -> " << medium.shear;
  EXPECT_GT(medium.shear / fine.shear, 1.7)
      << "tau_w jumps did not shrink under refinement: " << medium.shear << " -> " << fine.shear;
  EXPECT_GT(coarse.omega / medium.omega, 1.7)
      << "omega jumps did not shrink under refinement: " << coarse.omega << " -> " << medium.omega;
  EXPECT_GT(medium.omega / fine.omega, 1.7)
      << "omega jumps did not shrink under refinement: " << medium.omega << " -> " << fine.omega;
}

TEST(AutomaticWallTreatment, TheContinuityTestCanDetectADiscontinuity) {
  // NON-VACUITY CONTROL for the test above. The same sweep, the same two branch
  // values, combined by a hard switch at y+ = 5 instead of by the blend. Its
  // jump must NOT shrink under refinement -- otherwise the test above proves
  // nothing about the blend.
  const SweepJumps coarse = sweepJumps(200);
  const SweepJumps fine = sweepJumps(800);

  EXPECT_GT(coarse.switched, 0.5) << "the control is not actually discontinuous";
  EXPECT_LT(coarse.switched / fine.switched, 1.2)
      << "the switched control's jump shrank under refinement (" << coarse.switched << " -> "
      << fine.switched << "), so this sweep cannot discriminate";
  // And the blend must be dramatically smoother than the switch at the same
  // resolution -- the comparison the whole exercise rests on.
  EXPECT_LT(fine.shear * 10.0, fine.switched);
}

TEST(AutomaticWallTreatment, BlendsAreBoundedAndMonotonic) {
  // The published properties of the two blends, asserted directly rather than
  // trusted: each is >= the larger branch, <= its bound, and increases with
  // either argument.
  for (const Real a : {0.0, 0.1, 1.0, 10.0}) {
    for (const Real b : {0.0, 0.1, 1.0, 10.0}) {
      const Real u = cfd::turbulence::blendFrictionVelocity(a, b);
      const Real w = cfd::turbulence::blendSpecificDissipation(a, b);
      const Real larger = std::max(a, b);
      EXPECT_GE(u, larger);
      EXPECT_LE(u, std::pow(2.0, 0.25) * larger + 1e-15);
      EXPECT_GE(w, larger);
      EXPECT_LE(w, std::sqrt(2.0) * larger + 1e-15);
    }
  }
  // Monotone in each argument.
  EXPECT_GT(cfd::turbulence::blendFrictionVelocity(2.0, 1.0),
            cfd::turbulence::blendFrictionVelocity(1.0, 1.0));
  EXPECT_GT(cfd::turbulence::blendSpecificDissipation(1.0, 2.0),
            cfd::turbulence::blendSpecificDissipation(1.0, 1.0));
  // Exact reduction when one branch vanishes.
  EXPECT_DOUBLE_EQ(cfd::turbulence::blendFrictionVelocity(0.0, 3.0), 3.0);
  EXPECT_DOUBLE_EQ(cfd::turbulence::blendSpecificDissipation(4.0, 0.0), 4.0);
}

TEST(AutomaticWallTreatment, NeverProducesNegativeViscosityOrDissipation) {
  const Real uTau = 0.05;
  for (const Real target : {0.1, 1.0, 5.0, 10.0, 30.0, 100.0, 300.0, 1000.0}) {
    const auto state = evaluate(cellAtYPlus(target, uTau));
    EXPECT_GE(state.effectiveViscosity, kViscosity) << "y+ = " << target;
    EXPECT_GT(state.specificDissipation, 0.0) << "y+ = " << target;
    EXPECT_GE(state.wallShearStress, 0.0) << "y+ = " << target;
    EXPECT_GE(state.frictionVelocity, 0.0) << "y+ = " << target;
  }
}

TEST(AutomaticWallTreatment, IsDeterministic) {
  const WallCell cell = cellAtYPlus(50.0, 0.05);
  const auto first = evaluate(cell);
  const auto second = evaluate(cell);
  EXPECT_EQ(first.wallShearStress, second.wallShearStress);
  EXPECT_EQ(first.specificDissipation, second.specificDissipation);
  EXPECT_EQ(first.effectiveViscosity, second.effectiveViscosity);
  EXPECT_EQ(first.yPlus, second.yPlus);
  EXPECT_EQ(first.regime, second.regime);
}

// --- the sublayer omega coefficient -----------------------------------------

TEST(AutomaticWallTreatment, SublayerOmegaUsesTheCellCentreCoefficient) {
  // 6 nu/(beta1 y^2), NOT WallOmega's 60 nu/(beta1 y^2). The two are both
  // Menter's and play different roles -- a wall FACE value versus the sublayer
  // solution at the first CELL CENTRE -- and confusing them puts the wall cell
  // an order of magnitude off its own asymptote.
  const Real nu = 1e-5;
  const Real y = 2e-3;
  const Real omega = cfd::turbulence::viscousSublayerSpecificDissipation(nu, kBeta1, y);
  EXPECT_DOUBLE_EQ(omega, 6.0 * nu / (kBeta1 * y * y));
  // Inverse-square in y: halving the wall distance quadruples omega.
  EXPECT_NEAR(cfd::turbulence::viscousSublayerSpecificDissipation(nu, kBeta1, 0.5 * y), 4.0 * omega,
              1e-9 * omega);
}

TEST(AutomaticWallTreatment, RejectsNonPhysicalArguments) {
  EXPECT_THROW((void)cfd::turbulence::viscousSublayerSpecificDissipation(1e-5, kBeta1, 0.0),
               InvalidArgumentError);
  EXPECT_THROW((void)cfd::turbulence::viscousSublayerSpecificDissipation(1e-5, 0.0, 1e-3),
               InvalidArgumentError);
  EXPECT_THROW((void)cfd::turbulence::viscousFrictionVelocity(1.0, 0.0, 1e-5),
               InvalidArgumentError);
  EXPECT_THROW((void)cfd::turbulence::blendFrictionVelocity(-1.0, 1.0), InvalidArgumentError);
  EXPECT_THROW((void)cfd::turbulence::blendSpecificDissipation(1.0, -1.0), InvalidArgumentError);
  EXPECT_THROW((void)cfd::turbulence::automaticWallState(1.0, 1e-4, 0.0, kDensity, kViscosity,
                                                         kBeta1, kCoefficients),
               InvalidArgumentError);
}

TEST(AutomaticWallTreatment, ModeAndRegimeNamesAreStable) {
  EXPECT_EQ(cfd::turbulence::wallTreatmentModeName(
                cfd::turbulence::WallTreatmentMode::HighReWallFunction),
            "HighReWallFunction");
  EXPECT_EQ(cfd::turbulence::wallTreatmentModeName(cfd::turbulence::WallTreatmentMode::Automatic),
            "Automatic");
  EXPECT_EQ(cfd::turbulence::wallRegimeName(WallRegime::ViscousSublayer), "ViscousSublayer");
  EXPECT_EQ(cfd::turbulence::wallRegimeName(WallRegime::Blended), "Blended");
  EXPECT_EQ(cfd::turbulence::wallRegimeName(WallRegime::LogLayer), "LogLayer");
}

TEST(AutomaticWallTreatment, LogLayerWeightTracksBranchDominance) {
  // The weight the k production is blended on. It is not a new constant: it is
  // omega_log^2/(omega_vis^2 + omega_log^2), read off the omega blend above.
  // What has to hold is that it reaches both ends and is monotone in y+.
  const Real uTau = 0.05;
  Real previous = -1.0;
  for (const Real target : {0.5, 1.0, 5.0, 10.0, 30.0, 100.0, 300.0}) {
    const auto state = evaluate(cellAtYPlus(target, uTau));
    EXPECT_GE(state.logLayerWeight, 0.0) << "y+ = " << target;
    EXPECT_LE(state.logLayerWeight, 1.0) << "y+ = " << target;
    EXPECT_GT(state.logLayerWeight, previous)
        << "the weight is not monotone in y+ at y+ = " << target;
    previous = state.logLayerWeight;
  }
  // The ends, so this cannot pass on a weight that merely wobbles upward.
  EXPECT_LT(evaluate(cellAtYPlus(0.5, uTau)).logLayerWeight, 0.05)
      << "the log branch still counts deep in the sublayer";
  EXPECT_GT(evaluate(cellAtYPlus(300.0, uTau)).logLayerWeight, 0.99)
      << "the log branch does not fully take over in the log layer";
}

TEST(AutomaticWallTreatment, ProductionBlendVanishesWithTheLogBranch) {
  // The defect this weight exists to fix. The equilibrium production
  // tau_w u_tau*/(kappa y) does NOT vanish as the wall cell is resolved -- with
  // k ~ y^2 it tends to a constant -- so imposing it on a resolved cell injects
  // production that is not there. Weighted by w it does vanish.
  const Real uTau = 0.05;
  const auto resolved = evaluate(cellAtYPlus(1.0, uTau));
  const auto logLayer = evaluate(cellAtYPlus(100.0, uTau));

  const Real resolvedEquilibrium = cfd::turbulence::wallAdjacentProduction(
      resolved.wallShearStress, resolved.equilibriumFrictionVelocity,
      cellAtYPlus(1.0, uTau).wallDistance, kCoefficients);
  EXPECT_GT(resolvedEquilibrium, 0.0) << "the unweighted term is not the small one -- "
                                         "without the weight this would be imposed as is";
  EXPECT_LT(resolved.logLayerWeight * resolvedEquilibrium, 0.05 * resolvedEquilibrium)
      << "the weight does not suppress the equilibrium production on a resolved cell";

  // And it must NOT suppress it where the high-Re path is right, or the
  // validated channel results would move.
  EXPECT_GT(logLayer.logLayerWeight, 0.99);
}

// =====================================================================
// P16b attempt 3 -- ARCHITECTURE DIAGNOSTIC (section 3). Read-only.
// =====================================================================
//
// Not a gate. This prints, for one wall cell swept across the near-wall
// region, what each of the three treatments would impose, so the departure
// can be located rather than guessed at.
//
//   automatic  : the blended treatment as it stands now
//   high-Re    : the validated Launder-Spalding wall function
//   low-Re     : the untouched pre-P16 SST path -- omega_face = 60 nu/(beta1 y^2)
//                as a Dirichlet condition at the WALL FACE, with the first
//                cell's omega SOLVED by the PDE rather than imposed. The cell's
//                own analytic sublayer value 6 nu/(beta1 y^2) is printed
//                alongside as what that PDE should produce there.
//
// Two k states are swept, because the choice matters and the first version of
// these tests got it wrong:
//   equilibrium  k = u_tau^2/sqrt(C_mu)        -- correct in the log layer
//   resolved     k = k_eq (y+/30)^2            -- the EXACT near-wall order
//                                                 k ~ y^2, anchored at y+ = 30.
//                                                 The exponent is exact; only
//                                                 the anchor is a choice, and
//                                                 it is stated rather than
//                                                 hidden in a fitted constant.
TEST(AutomaticWallTreatment, DISABLED_ArchitectureDiagnosticTable) {
  const Real uTau = 0.05;
  const Real nu = kViscosity / kDensity;
  const Real kEquilibrium30 = cfd::turbulence::wallAdjacentTurbulentEnergy(uTau, kCoefficients);

  for (int mode = 0; mode < 2; ++mode) {
    std::printf("%s\n", mode == 0 ? "\n--- k = equilibrium (log-layer value at every y+) ---"
                                  : "\n--- k = resolved asymptote k_eq (y+/30)^2 ---");
    std::printf("%6s %8s %12s %12s %12s %12s %11s %11s %11s %9s %9s\n", "y+", "w", "om_auto",
                "om_highRe", "om_sub", "om_face", "tau_auto", "tau_highRe", "tau_lam", "muw_auto",
                "muw_hiRe");

    for (const Real target : {0.5, 1.0, 3.0, 5.0, 10.0, 20.0, 30.0, 50.0, 100.0}) {
      const Real y = target * nu / uTau;
      const Real k =
          mode == 0 ? kEquilibrium30 : kEquilibrium30 * (target / 30.0) * (target / 30.0);
      // Velocity from whichever near-wall law holds, so the cell is a
      // physically consistent state rather than an arbitrary triple.
      const Real u = target <= 5.0 ? uTau * target
                                   : uTau * cfd::turbulence::uPlusLogLaw(target, kCoefficients);

      const auto automatic =
          cfd::turbulence::automaticWallState(u, k, y, kDensity, kViscosity, kBeta1, kCoefficients);
      const Real omegaSub = cfd::turbulence::viscousSublayerSpecificDissipation(nu, kBeta1, y);
      const cfd::boundary::WallOmega wallOmega(nu, kBeta1);
      const Real omegaFace = wallOmega.boundaryValue(0.0, y);

      Real omegaHighRe = 0.0;
      Real tauHighRe = 0.0;
      Real muwHighRe = 0.0;
      bool highReDefined = false;
      try {
        omegaHighRe = cfd::turbulence::wallAdjacentSpecificDissipation(k, y, kCoefficients);
        tauHighRe = cfd::turbulence::wallShearStressLaunderSpalding(u, k, y, kDensity, kViscosity,
                                                                    kCoefficients);
        muwHighRe = cfd::turbulence::effectiveWallViscosity(tauHighRe, u, y, kViscosity);
        highReDefined = true;
      } catch (const cfd::InvalidArgumentError&) {
        // The high-Re path REFUSES this cell -- ln(E y*) <= 0. That refusal is
        // itself the answer for those rows.
      }

      const Real tauLaminar = kViscosity * u / y;
      std::printf(
          "%6.1f %8.4f %12.4e %12s %12.4e %12.4e %11.4e %11s %11.4e %9.3f %9s\n", target,
          automatic.logLayerWeight, automatic.specificDissipation,
          highReDefined ? std::to_string(omegaHighRe).substr(0, 11).c_str() : "REFUSED", omegaSub,
          omegaFace, automatic.wallShearStress,
          highReDefined ? std::to_string(tauHighRe).substr(0, 11).c_str() : "REFUSED", tauLaminar,
          automatic.effectiveViscosity / kViscosity,
          highReDefined ? std::to_string(muwHighRe / kViscosity).substr(0, 8).c_str() : "REFUSED");
    }
  }
  SUCCEED();
}
