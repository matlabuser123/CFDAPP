// P16-TWF -- the thermal wall function, Launder-Spalding / Jayatilleke.
//
// The formulation was frozen before implementation in
// results/p16/turbulence/eddy_viscosity/thermal_wall_function.md. The test
// that matters most here is TheConventionPutsPInsideThePrandtlBracket: the
// alternative reading differs by a factor of Pr_t on the P term and would be a
// silent, plausible-looking error.
#include <gtest/gtest.h>

#include <cmath>
#include <limits>

#include "cfd/core/Exception.hpp"
#include "cfd/thermal/ThermalWallFunction.hpp"

using cfd::InvalidArgumentError;
using cfd::Real;
using cfd::thermal::jayatillekeP;
using cfd::thermal::temperaturePlus;
using cfd::thermal::thermalSublayerThickness;
using cfd::thermal::wallHeatTransferConductance;
using cfd::turbulence::WallFunctionCoefficients;

namespace {
// The SAME constants the momentum wall function uses, so both treatments
// describe one wall.
const WallFunctionCoefficients kCoefficients{0.41, 9.793, 0.09};
constexpr Real kAirPrandtl = 0.704;
constexpr Real kTurbulentPrandtl = 0.85;
}  // namespace

// --- the P-function -----------------------------------------------------------

TEST(ThermalWallFunction, JayatillekePMatchesItsClosedForm) {
  // Hand-checked for air, Pr/Pr_t = 0.828235:
  //   (Pr/Pr_t)^0.75                 = 0.868191
  //   1 + 0.28 exp(-0.007 * 0.828235) = 1.278381
  //   P = 9.24 * (0.868191 - 1) * 1.278381 = -1.556960
  //
  // The first version of this test wrote -1.5595, from a slip in the
  // intermediate ^0.75 term. The implementation was right and the hand value
  // was wrong; corrected here rather than by widening the tolerance.
  const Real p = jayatillekeP(kAirPrandtl, kTurbulentPrandtl);
  EXPECT_NEAR(p, -1.556960, 1e-5);
}

TEST(ThermalWallFunction, PIsNegativeBelowUnityPrandtlRatioAndPositiveAbove) {
  // The sign is physics, not a bug: Pr < Pr_t means the conduction sublayer
  // offers LESS resistance than the momentum sublayer. Anything that clamped P
  // to be non-negative would be wrong for every ordinary air case.
  EXPECT_LT(jayatillekeP(0.704, 0.85), 0.0) << "air";
  EXPECT_NEAR(jayatillekeP(0.85, 0.85), 0.0, 1e-12) << "Pr = Pr_t must give exactly zero";
  EXPECT_GT(jayatillekeP(7.0, 0.85), 0.0) << "water";
  // And it must grow with Pr, since a thicker conduction sublayer resists more.
  EXPECT_GT(jayatillekeP(7.0, 0.85), jayatillekeP(2.0, 0.85));
}

// --- THE convention test -------------------------------------------------------

TEST(ThermalWallFunction, TheConventionPutsPInsideThePrandtlBracket) {
  // T+ = Pr_t [ ln(E y+)/kappa + P ]   -- NOT   Pr_t ln(E y+)/kappa + P.
  //
  // The two differ by (1 - Pr_t) * P, which for air is 0.15 * (-1.56) = -0.23
  // on a T+ of about 14: a ~1.7% error. Small enough to look like noise,
  // systematic enough to bias every wall heat flux, so it is pinned exactly.
  const Real yPlus = 180.0;
  const Real p = jayatillekeP(kAirPrandtl, kTurbulentPrandtl);
  const Real logTerm = std::log(kCoefficients.E * yPlus) / kCoefficients.kappa;

  const Real frozen = kTurbulentPrandtl * (logTerm + p);
  const Real wrongReading = (kTurbulentPrandtl * logTerm) + p;

  EXPECT_NEAR(temperaturePlus(yPlus, kAirPrandtl, kTurbulentPrandtl, kCoefficients), frozen,
              1e-12 * std::abs(frozen));
  // The two readings must actually differ here, or this test proves nothing.
  EXPECT_GT(std::abs(frozen - wrongReading), 0.1);
  EXPECT_NE(temperaturePlus(yPlus, kAirPrandtl, kTurbulentPrandtl, kCoefficients), wrongReading);
}

// --- branches and continuity ----------------------------------------------------

TEST(ThermalWallFunction, ReducesToConductionInTheSublayer) {
  // T+ = Pr y+ below the crossing, exactly -- including T+ = 0 at the wall.
  const Real crossing = thermalSublayerThickness(kAirPrandtl, kTurbulentPrandtl, kCoefficients);
  ASSERT_GT(crossing, 1.0) << "the conduction sublayer is implausibly thin";
  for (const Real yPlus : {0.0, 0.5, 1.0, 2.0}) {
    ASSERT_LT(yPlus, crossing);
    EXPECT_NEAR(temperaturePlus(yPlus, kAirPrandtl, kTurbulentPrandtl, kCoefficients),
                kAirPrandtl * yPlus, 1e-12 * std::max(1.0, kAirPrandtl * yPlus));
  }
}

TEST(ThermalWallFunction, IsContinuousAtTheCrossing) {
  // The two branches meet by construction. Asserted, because a crossing solved
  // to the wrong root would leave a jump that nothing else here would catch.
  const Real crossing = thermalSublayerThickness(kAirPrandtl, kTurbulentPrandtl, kCoefficients);
  const Real below =
      temperaturePlus(crossing * (1.0 - 1e-9), kAirPrandtl, kTurbulentPrandtl, kCoefficients);
  const Real above =
      temperaturePlus(crossing * (1.0 + 1e-9), kAirPrandtl, kTurbulentPrandtl, kCoefficients);
  EXPECT_NEAR(below, above, 1e-6 * below) << "T+ jumps at the branch crossing";

  // And the crossing must genuinely satisfy its own defining equation.
  const Real p = jayatillekeP(kAirPrandtl, kTurbulentPrandtl);
  const Real logBranch =
      kTurbulentPrandtl * ((std::log(kCoefficients.E * crossing) / kCoefficients.kappa) + p);
  EXPECT_NEAR(kAirPrandtl * crossing, logBranch, 1e-6 * logBranch);
}

TEST(ThermalWallFunction, IsMonotoneAndGrowsLogarithmicallyInTheLogRegion) {
  Real previous = -1.0;
  for (const Real yPlus : {1.0, 5.0, 12.0, 30.0, 100.0, 300.0, 1000.0}) {
    const Real tPlus = temperaturePlus(yPlus, kAirPrandtl, kTurbulentPrandtl, kCoefficients);
    EXPECT_GT(tPlus, previous) << "T+ is not increasing at y+ = " << yPlus;
    previous = tPlus;
  }
  // Doubling y+ deep in the log region adds Pr_t ln(2)/kappa, a constant.
  const Real a = temperaturePlus(200.0, kAirPrandtl, kTurbulentPrandtl, kCoefficients);
  const Real b = temperaturePlus(400.0, kAirPrandtl, kTurbulentPrandtl, kCoefficients);
  EXPECT_NEAR(b - a, kTurbulentPrandtl * std::log(2.0) / kCoefficients.kappa, 1e-9);
}

TEST(ThermalWallFunction, MatchesTheHandValueAtTheBenchmarkCondition) {
  // The frozen a-priori figure from thermal_wall_function.md, recomputed here
  // so the prediction and the implementation cannot drift apart.
  //   ln(9.793*180)/0.41 = 18.2318 ; P = -1.55696
  //   T+ = 0.85 * (18.2318 - 1.55696) = 14.1728
  const Real tPlus = temperaturePlus(180.0, kAirPrandtl, kTurbulentPrandtl, kCoefficients);
  EXPECT_NEAR(tPlus, 14.1728, 1e-3);
}

// --- the conductance -----------------------------------------------------------

TEST(ThermalWallFunction, ConductanceIsTheWallFluxDividedByTemperatureDifference) {
  const Real rho = 1.225;
  const Real cp = 1005.0;
  const Real uTau = 0.54;
  const Real yPlus = 180.0;
  const Real g = wallHeatTransferConductance(rho, cp, uTau, yPlus, kAirPrandtl, kTurbulentPrandtl,
                                             kCoefficients);
  const Real tPlus = temperaturePlus(yPlus, kAirPrandtl, kTurbulentPrandtl, kCoefficients);
  EXPECT_NEAR(g, rho * cp * uTau / tPlus, 1e-12 * g);
  EXPECT_GT(g, 0.0);
}

TEST(ThermalWallFunction, IsFarSmallerThanTheRawGradientTreatmentItReplaces) {
  // The whole point of the subtask, as a number. The previous treatment was
  // equivalent to T+_effective = Pr_t/kappa; the wall law gives T+ = 14.17 at
  // y+ = 180, so the wall flux must drop by roughly that ratio.
  const Real tPlus = temperaturePlus(180.0, kAirPrandtl, kTurbulentPrandtl, kCoefficients);
  const Real rawGradientEquivalent = kTurbulentPrandtl / kCoefficients.kappa;
  const Real reduction = tPlus / rawGradientEquivalent;
  EXPECT_GT(reduction, 5.0) << "the wall law barely changes the flux, so it cannot explain the "
                               "4.3x benchmark over-prediction";
  EXPECT_LT(reduction, 10.0);
}

TEST(ThermalWallFunction, ZeroFrictionVelocityGivesExactlyZeroConductance) {
  EXPECT_EQ(wallHeatTransferConductance(1.225, 1005.0, 0.0, 180.0, kAirPrandtl, kTurbulentPrandtl,
                                        kCoefficients),
            0.0);
  EXPECT_EQ(wallHeatTransferConductance(1.225, 1005.0, 0.54, 0.0, kAirPrandtl, kTurbulentPrandtl,
                                        kCoefficients),
            0.0);
}

TEST(ThermalWallFunction, ConductanceFallsAsTurbulentPrandtlRises) {
  Real previous = std::numeric_limits<Real>::infinity();
  for (const Real prt : {0.5, 0.7, 0.85, 1.2, 2.0}) {
    const Real g =
        wallHeatTransferConductance(1.225, 1005.0, 0.54, 180.0, kAirPrandtl, prt, kCoefficients);
    EXPECT_LT(g, previous) << "G did not fall as Pr_t rose to " << prt;
    previous = g;
  }
}

TEST(ThermalWallFunction, RejectsNonPhysicalArguments) {
  EXPECT_THROW((void)jayatillekeP(0.0, 0.85), InvalidArgumentError);
  EXPECT_THROW((void)jayatillekeP(0.704, 0.0), InvalidArgumentError);
  EXPECT_THROW((void)temperaturePlus(-1.0, 0.704, 0.85, kCoefficients), InvalidArgumentError);
  EXPECT_THROW(
      (void)wallHeatTransferConductance(0.0, 1005.0, 0.54, 180.0, 0.704, 0.85, kCoefficients),
      InvalidArgumentError);
  EXPECT_THROW(
      (void)wallHeatTransferConductance(1.225, 1005.0, -1.0, 180.0, 0.704, 0.85, kCoefficients),
      InvalidArgumentError);
}
