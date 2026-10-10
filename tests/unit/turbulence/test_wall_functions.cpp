// P16 -- the near-wall treatment: friction velocity, y+, and the standard
// high-Re wall functions.
//
// WHAT MAKES THESE TESTS MORE THAN SELF-CONSISTENCY
// -------------------------------------------------
// Most of these formulas are short enough that a test restating them would
// prove only that the expression was typed twice. So the central tests here are
// ROUND TRIPS against the log law, which is the thing the wall function is
// supposed to reproduce and is derived independently of the code:
//
//   * feed the Launder-Spalding wall function a velocity taken FROM the log law
//     at a chosen y+, with the equilibrium k for that u_tau, and it must return
//     exactly the tau_w that generated it (LogLawRoundTripIsExact);
//   * the k-omega wall value must equal the k-epsilon one through
//     omega = epsilon/(C_mu k) (SpecificDissipationAgreesWithDissipation);
//   * the effective wall viscosity must reproduce tau_w through the LAMINAR
//     wall-shear expression the momentum assembly actually uses
//     (EffectiveViscosityReproducesTheWallShear).
//
// Each of those would fail for a wrong constant, a wrong power, or a dropped
// factor, none of which a restatement test would catch.
#include <gtest/gtest.h>

#include <cmath>

#include "cfd/core/Exception.hpp"
#include "cfd/turbulence/WallFunctions.hpp"

using cfd::InvalidArgumentError;
using cfd::Real;
using cfd::turbulence::WallFunctionCoefficients;
using cfd::turbulence::WallResolution;

namespace {
constexpr WallFunctionCoefficients kCoefficients{};  // kappa 0.41, E 9.793, cMu 0.09
}

// --- the log law itself ------------------------------------------------------

TEST(WallFunctions, LogLawInterceptIsDerivedFromKappaAndE) {
  // B = ln(E)/kappa. Not stored separately, so it cannot disagree with the pair
  // it comes from.
  const Real expected = std::log(9.793) / 0.41;
  EXPECT_DOUBLE_EQ(cfd::turbulence::logLawIntercept(kCoefficients), expected);
  EXPECT_NEAR(expected, 5.565, 1e-3) << "the standard intercept for this pairing";
}

TEST(WallFunctions, LogLawInterceptTracksItsCoefficients) {
  // Changing E must move B. A stored constant would not.
  WallFunctionCoefficients other = kCoefficients;
  other.E = 9.0;
  EXPECT_NE(cfd::turbulence::logLawIntercept(other),
            cfd::turbulence::logLawIntercept(kCoefficients));
  EXPECT_DOUBLE_EQ(cfd::turbulence::logLawIntercept(other), std::log(9.0) / 0.41);
}

TEST(WallFunctions, TheTwoLawsCrossWhereTheyAgree) {
  const Real crossing = cfd::turbulence::viscousLogLayerIntersection(kCoefficients);

  // The defining property, asserted directly rather than against a remembered
  // 11.25 -- that number is the root for one particular (kappa, E) pair.
  EXPECT_NEAR(cfd::turbulence::uPlusViscousSublayer(crossing),
              cfd::turbulence::uPlusLogLaw(crossing, kCoefficients), 1e-9);
  EXPECT_GT(crossing, 5.0);
  EXPECT_LT(crossing, 30.0) << "the crossing must lie inside the buffer layer";
}

// --- friction velocity -------------------------------------------------------

TEST(WallFunctions, FrictionVelocityFromWallShearIsTheDefinition) {
  // tau_w = 2, rho = 0.5 -> u_tau = sqrt(4) = 2.
  EXPECT_DOUBLE_EQ(cfd::turbulence::frictionVelocityFromWallShear(2.0, 0.5), 2.0);
  EXPECT_DOUBLE_EQ(cfd::turbulence::frictionVelocityFromWallShear(0.0, 1.0), 0.0)
      << "a stationary wall carries no friction velocity";
}

TEST(WallFunctions, FrictionVelocityFromTurbulentEnergyUsesTheQuarterPower) {
  // C_mu^(1/4) sqrt(k) with k = 1 is C_mu^(1/4) = 0.09^0.25.
  EXPECT_DOUBLE_EQ(cfd::turbulence::frictionVelocityFromTurbulentEnergy(1.0, 0.09),
                   std::pow(0.09, 0.25));
  // Scales as sqrt(k): quadrupling k doubles u_tau*.
  const Real one = cfd::turbulence::frictionVelocityFromTurbulentEnergy(1.0, 0.09);
  const Real four = cfd::turbulence::frictionVelocityFromTurbulentEnergy(4.0, 0.09);
  EXPECT_NEAR(four, 2.0 * one, 1e-14);
}

TEST(WallFunctions, TheTwoFrictionVelocitiesAgreeOnlyInEquilibrium) {
  // They are different quantities. Given the equilibrium k for a chosen u_tau,
  // they coincide -- and that is the ONLY case in which they do, which is why
  // the two are named apart.
  const Real uTau = 0.05;
  const Real kEquilibrium = cfd::turbulence::wallAdjacentTurbulentEnergy(uTau, kCoefficients);
  EXPECT_NEAR(cfd::turbulence::frictionVelocityFromTurbulentEnergy(kEquilibrium, kCoefficients.cMu),
              uTau, 1e-14);

  // Away from equilibrium they differ, and the code must not paper over it.
  const Real kOffEquilibrium = 4.0 * kEquilibrium;
  EXPECT_GT(
      cfd::turbulence::frictionVelocityFromTurbulentEnergy(kOffEquilibrium, kCoefficients.cMu),
      uTau);
}

// --- wall coordinates --------------------------------------------------------

TEST(WallFunctions, YPlusIsTheDefinition) {
  // rho=2, u_tau=3, y=5, mu=6 -> 2*3*5/6 = 5.
  EXPECT_DOUBLE_EQ(cfd::turbulence::yPlus(2.0, 3.0, 5.0, 6.0), 5.0);
}

TEST(WallFunctions, UPlusIsTheDefinition) {
  EXPECT_DOUBLE_EQ(cfd::turbulence::uPlus(10.0, 4.0), 2.5);
}

// --- THE round trip ----------------------------------------------------------

TEST(WallFunctions, LogLawRoundTripIsExact) {
  // The test that actually proves the wall function IS the log law.
  //
  // Construct a wall cell that is by definition on the log law: pick u_tau and
  // y_P, derive y+ from them, take U_P straight from u+ = ln(y+)/kappa + B, and
  // give the cell the equilibrium k for that u_tau. The Launder-Spalding
  // expression must then return exactly tau_w = rho u_tau^2 -- the shear that
  // generated the velocity it was handed.
  //
  // Algebraically the round trip is exact, not approximate: with k at
  // equilibrium, u_tau* = u_tau and y* = y+, so
  //   tau_w = rho u_tau kappa U_P / ln(E y+)
  //         = rho u_tau^2 kappa (ln(y+) + ln E)/kappa / ln(E y+)
  //         = rho u_tau^2.
  // A wrong kappa, a wrong E, a missing C_mu^(1/4) or a dropped rho all break
  // it. The tolerance is floating-point noise, not physical slack.
  const Real density = 1.2;
  const Real dynamicViscosity = 1.8e-5;
  const Real uTau = 0.4;
  const Real wallDistance = 1.5e-3;

  const Real yPlusValue = cfd::turbulence::yPlus(density, uTau, wallDistance, dynamicViscosity);
  ASSERT_TRUE(cfd::turbulence::isWallFunctionResolutionValid(yPlusValue))
      << "the round trip must be run where the wall function is valid; y+ = " << yPlusValue;

  const Real velocity = uTau * cfd::turbulence::uPlusLogLaw(yPlusValue, kCoefficients);
  const Real kEquilibrium = cfd::turbulence::wallAdjacentTurbulentEnergy(uTau, kCoefficients);

  const Real tauW = cfd::turbulence::wallShearStressLaunderSpalding(
      velocity, kEquilibrium, wallDistance, density, dynamicViscosity, kCoefficients);

  EXPECT_NEAR(tauW, density * uTau * uTau, 1e-14 * density * uTau * uTau);
  EXPECT_NEAR(cfd::turbulence::frictionVelocityFromWallShear(tauW, density), uTau, 1e-12);
}

TEST(WallFunctions, RoundTripHoldsAcrossTheWholeValidBand) {
  // Not one lucky point: the identity must hold everywhere the wall function
  // claims to be valid.
  const Real density = 1.0;
  const Real dynamicViscosity = 1e-5;
  const Real uTau = 0.3;

  for (const Real targetYPlus : {30.0, 50.0, 100.0, 200.0, 300.0}) {
    const Real wallDistance = targetYPlus * dynamicViscosity / (density * uTau);
    const Real velocity = uTau * cfd::turbulence::uPlusLogLaw(targetYPlus, kCoefficients);
    const Real kEquilibrium = cfd::turbulence::wallAdjacentTurbulentEnergy(uTau, kCoefficients);

    const Real tauW = cfd::turbulence::wallShearStressLaunderSpalding(
        velocity, kEquilibrium, wallDistance, density, dynamicViscosity, kCoefficients);
    EXPECT_NEAR(tauW, density * uTau * uTau, 1e-13) << "y+ = " << targetYPlus;
  }
}

TEST(WallFunctions, LaminarWallShearIsNotWhatTheWallFunctionReturns) {
  // The non-vacuity of the whole exercise. The pre-P16 code effectively applied
  // the LAMINAR wall shear mu*U_P/y_P at a turbulent wall. If the wall function
  // returned something close to that, it would not be doing anything.
  const Real density = 1.0;
  const Real dynamicViscosity = 1e-5;
  const Real uTau = 0.3;
  const Real targetYPlus = 100.0;
  const Real wallDistance = targetYPlus * dynamicViscosity / (density * uTau);
  const Real velocity = uTau * cfd::turbulence::uPlusLogLaw(targetYPlus, kCoefficients);
  const Real kEquilibrium = cfd::turbulence::wallAdjacentTurbulentEnergy(uTau, kCoefficients);

  const Real tauWallFunction = cfd::turbulence::wallShearStressLaunderSpalding(
      velocity, kEquilibrium, wallDistance, density, dynamicViscosity, kCoefficients);
  const Real tauLaminar = dynamicViscosity * velocity / wallDistance;

  // At y+ = 100 the turbulent wall shear is an order of magnitude above the
  // laminar estimate. That gap is the entire content of a wall function.
  EXPECT_GT(tauWallFunction, 5.0 * tauLaminar)
      << "wall function " << tauWallFunction << " vs laminar " << tauLaminar;
}

// --- wall-cell turbulence values ---------------------------------------------

TEST(WallFunctions, DissipationIsTheEquilibriumExpression) {
  // eps = u_tau*^3/(kappa y).
  const Real k = 0.01;
  const Real y = 2e-3;
  const Real uTauStar = cfd::turbulence::frictionVelocityFromTurbulentEnergy(k, kCoefficients.cMu);
  EXPECT_NEAR(cfd::turbulence::wallAdjacentDissipation(k, y, kCoefficients),
              uTauStar * uTauStar * uTauStar / (kCoefficients.kappa * y), 1e-18);
}

TEST(WallFunctions, SpecificDissipationAgreesWithDissipation) {
  // The consistency that makes a k-omega wall cell and a k-epsilon wall cell
  // describe the SAME near-wall state:
  //     omega = epsilon / (C_mu k)
  // Derived independently on each side, so agreement is evidence rather than a
  // restatement.
  const Real k = 0.02;
  const Real y = 1.2e-3;

  const Real epsilon = cfd::turbulence::wallAdjacentDissipation(k, y, kCoefficients);
  const Real omega = cfd::turbulence::wallAdjacentSpecificDissipation(k, y, kCoefficients);

  EXPECT_NEAR(omega, epsilon / (kCoefficients.cMu * k), 1e-10 * omega);
}

TEST(WallFunctions, TurbulentEnergyInvertsTheFrictionVelocity) {
  const Real uTau = 0.37;
  const Real k = cfd::turbulence::wallAdjacentTurbulentEnergy(uTau, kCoefficients);
  EXPECT_NEAR(cfd::turbulence::frictionVelocityFromTurbulentEnergy(k, kCoefficients.cMu), uTau,
              1e-14);
}

TEST(WallFunctions, ProductionBalancesDissipationInEquilibrium) {
  // The defining property of an equilibrium wall cell: P_k = epsilon (per unit
  // volume, both in the dynamic-mu convention). If the two expressions did not
  // balance, the wall cell would be a net source or sink of turbulent energy
  // and the log layer would not be sustained.
  const Real density = 1.0;
  const Real uTau = 0.25;
  const Real y = 1e-3;
  const Real k = cfd::turbulence::wallAdjacentTurbulentEnergy(uTau, kCoefficients);
  const Real tauW = density * uTau * uTau;
  const Real uTauStar = cfd::turbulence::frictionVelocityFromTurbulentEnergy(k, kCoefficients.cMu);

  const Real production = cfd::turbulence::wallAdjacentProduction(tauW, uTauStar, y, kCoefficients);
  const Real dissipation = density * cfd::turbulence::wallAdjacentDissipation(k, y, kCoefficients);

  EXPECT_NEAR(production, dissipation, 1e-12 * production);
}

// --- effective wall viscosity ------------------------------------------------

TEST(WallFunctions, EffectiveViscosityReproducesTheWallShear) {
  // The bridge to the momentum equation. The assembly computes a wall shear as
  // mu * U_P / y_P; handing it this mu must give back the wall-function tau_w
  // exactly, because that is the only reason to compute it.
  const Real tauW = 0.05;
  const Real velocity = 2.0;
  const Real y = 1e-3;
  const Real mu = 1e-5;

  const Real muWall = cfd::turbulence::effectiveWallViscosity(tauW, velocity, y, mu);
  EXPECT_NEAR(muWall * velocity / y, tauW, 1e-15);
  EXPECT_GT(muWall, mu) << "a turbulent wall must be more dissipative than a laminar one";
}

TEST(WallFunctions, EffectiveViscosityIsMolecularWithoutVelocity) {
  EXPECT_DOUBLE_EQ(cfd::turbulence::effectiveWallViscosity(0.0, 0.0, 1e-3, 1e-5), 1e-5);
}

TEST(WallFunctions, EffectiveViscosityFloorsAtMolecularAndSaysSo) {
  // mu_wall is mu + mu_t, so mu_t >= 0 floors it at mu -- the same positivity
  // rule validateTurbulentViscosityField enforces everywhere else. It is a
  // physical constraint, not a convenience clamp.
  //
  // What makes it acceptable is that it is REPORTED. An earlier version threw
  // here instead; that turned a diagnosable buffer-layer mesh into an exception
  // thrown from deep inside the treatment, so the caller never received the
  // message explaining why. The floor is now observable and the caller decides.
  bool floored = false;
  const Real mu = 1e-5;
  EXPECT_DOUBLE_EQ(cfd::turbulence::effectiveWallViscosity(1e-12, 2.0, 1e-3, mu, &floored), mu);
  EXPECT_TRUE(floored);

  // And it does NOT fire for a genuine turbulent wall cell.
  floored = true;
  EXPECT_GT(cfd::turbulence::effectiveWallViscosity(0.05, 2.0, 1e-3, mu, &floored), mu);
  EXPECT_FALSE(floored);
}

// --- resolution classification -----------------------------------------------

TEST(WallFunctions, ResolutionBandsAreClassifiedAtTheirEdges) {
  using cfd::turbulence::classifyWallResolution;
  EXPECT_EQ(classifyWallResolution(0.0), WallResolution::ViscousSublayer);
  EXPECT_EQ(classifyWallResolution(5.0), WallResolution::ViscousSublayer);
  EXPECT_EQ(classifyWallResolution(5.0001), WallResolution::BufferLayer);
  EXPECT_EQ(classifyWallResolution(29.999), WallResolution::BufferLayer);
  EXPECT_EQ(classifyWallResolution(30.0), WallResolution::LogLayer);
  EXPECT_EQ(classifyWallResolution(300.0), WallResolution::LogLayer);
  EXPECT_EQ(classifyWallResolution(300.001), WallResolution::TooCoarse);
}

TEST(WallFunctions, ThePreP16BaselineResolutionIsRejected) {
  // The measured first-cell y+ of every pre-P16 channel run, preserved in
  // results/p16/turbulence/pre_p16_baseline/. Both land in the buffer layer,
  // where NEITHER near-wall law holds -- which is why the models were out by
  // 27-33% in Re_tau. This test pins that the new code recognises that
  // configuration as invalid rather than quietly running it.
  for (const Real baselineYPlus : {7.50, 8.21}) {
    EXPECT_EQ(cfd::turbulence::classifyWallResolution(baselineYPlus), WallResolution::BufferLayer)
        << "y+ = " << baselineYPlus;
    EXPECT_FALSE(cfd::turbulence::isWallFunctionResolutionValid(baselineYPlus));
  }
}

TEST(WallFunctions, ResolutionNamesAreStable) {
  EXPECT_EQ(cfd::turbulence::wallResolutionName(WallResolution::LogLayer), "LogLayer");
  EXPECT_EQ(cfd::turbulence::wallResolutionName(WallResolution::BufferLayer), "BufferLayer");
}

// --- rejection ---------------------------------------------------------------

TEST(WallFunctions, RejectsNonPhysicalArguments) {
  EXPECT_THROW((void)cfd::turbulence::frictionVelocityFromWallShear(-1.0, 1.0),
               InvalidArgumentError);
  EXPECT_THROW((void)cfd::turbulence::frictionVelocityFromWallShear(1.0, 0.0),
               InvalidArgumentError);
  EXPECT_THROW((void)cfd::turbulence::frictionVelocityFromTurbulentEnergy(-1.0, 0.09),
               InvalidArgumentError);
  EXPECT_THROW((void)cfd::turbulence::yPlus(1.0, 1.0, 1.0, 0.0), InvalidArgumentError);
  EXPECT_THROW((void)cfd::turbulence::uPlus(1.0, 0.0), InvalidArgumentError);
  EXPECT_THROW((void)cfd::turbulence::uPlusLogLaw(0.0, kCoefficients), InvalidArgumentError);
  EXPECT_THROW((void)cfd::turbulence::wallAdjacentDissipation(1.0, 0.0, kCoefficients),
               InvalidArgumentError);
  EXPECT_THROW((void)cfd::turbulence::wallAdjacentSpecificDissipation(1.0, -1.0, kCoefficients),
               InvalidArgumentError);
  EXPECT_THROW((void)cfd::turbulence::classifyWallResolution(std::nan("")), InvalidArgumentError);
}

TEST(WallFunctions, RejectsCoefficientsThatAreNotAPair) {
  WallFunctionCoefficients broken = kCoefficients;
  broken.kappa = 0.0;
  EXPECT_THROW((void)cfd::turbulence::logLawIntercept(broken), InvalidArgumentError);
  broken = kCoefficients;
  broken.E = -1.0;
  EXPECT_THROW((void)cfd::turbulence::uPlusLogLaw(50.0, broken), InvalidArgumentError);
}

TEST(WallFunctions, RefusesAWallCellBelowTheLogLawsValidity) {
  // ln(E y*) <= 0 means y* < 1/E -- deep inside the viscous sublayer, where the
  // high-Re expression is not merely inaccurate but has the wrong sign in its
  // denominator. Refused explicitly rather than returning a negative shear.
  const Real density = 1.0;
  const Real dynamicViscosity = 1.0;  // deliberately huge, to force y* tiny
  const Real wallDistance = 1e-6;
  const Real k = 1e-12;
  EXPECT_THROW((void)cfd::turbulence::wallShearStressLaunderSpalding(
                   1.0, k, wallDistance, density, dynamicViscosity, kCoefficients),
               InvalidArgumentError);
}
