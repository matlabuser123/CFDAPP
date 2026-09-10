// P2-TURB-006: SSTEquation.hpp's own formulas -- alpha derivation,
// coefficient blending, cross-diffusion, F1/F2, the eddy-viscosity
// limiter (both branches), and the production limiter (both branches).
// Every mandatory hand-derived test from sections 14/17/18/21/40/42/44
// lives here.
#include <gtest/gtest.h>

#include <cmath>

#include "cfd/core/Exception.hpp"
#include "cfd/turbulence/SSTEquation.hpp"

using cfd::InvalidArgumentError;
using cfd::Real;
using cfd::turbulence::blendSSTCoefficient;
using cfd::turbulence::computeCrossDiffusionCoefficient;
using cfd::turbulence::computeCrossDiffusionSource;
using cfd::turbulence::computeF1;
using cfd::turbulence::computeF1Argument;
using cfd::turbulence::computeF2;
using cfd::turbulence::computeF2Argument;
using cfd::turbulence::computeSSTAlpha;
using cfd::turbulence::computeSSTTurbulentViscosity;
using cfd::turbulence::limitProduction;

// --- alpha derivation --------------------------------------------------

TEST(SSTEquationTest, Alpha1MatchesHandDerivedValue) {
  // beta1=0.075, sigmaOmega1=0.5, betaStar=0.09, kappa=0.41 ->
  // alpha1 = 0.075/0.09 - 0.5*0.41^2/sqrt(0.09) = 0.833333... - 0.280166...
  //        = 0.553166666...
  const Real alpha1 = computeSSTAlpha(0.075, 0.5, 0.09, 0.41);
  EXPECT_NEAR(alpha1, 0.553166666667, 1e-9);
}

TEST(SSTEquationTest, Alpha2MatchesHandDerivedValue) {
  // beta2=0.0828, sigmaOmega2=0.856, betaStar=0.09, kappa=0.41 ->
  // alpha2 = 0.0828/0.09 - 0.856*0.41^2/sqrt(0.09) = 0.92 - 0.479645333...
  //        = 0.440354666...
  const Real alpha2 = computeSSTAlpha(0.0828, 0.856, 0.09, 0.41);
  EXPECT_NEAR(alpha2, 0.440354666667, 1e-9);
}

TEST(SSTEquationTest, AlphaRejectsInvalidBetaStar) {
  EXPECT_THROW((void)computeSSTAlpha(0.075, 0.5, 0.0, 0.41), InvalidArgumentError);
  EXPECT_THROW((void)computeSSTAlpha(0.075, 0.5, -1.0, 0.41), InvalidArgumentError);
}

// --- coefficient blending (section 21/40, mandatory) ------------------

TEST(SSTEquationTest, BlendAtF1EqualsOneUsesSetOneExactly) {
  EXPECT_DOUBLE_EQ(blendSSTCoefficient(1.0, 4.0, 8.0), 4.0);
}

TEST(SSTEquationTest, BlendAtF1EqualsZeroUsesSetTwoExactly) {
  EXPECT_DOUBLE_EQ(blendSSTCoefficient(0.0, 4.0, 8.0), 8.0);
}

TEST(SSTEquationTest, BlendAtQuarterWeightingMatchesHandDerivedValue) {
  // phi = 0.25*4 + 0.75*8 = 1 + 6 = 7.
  EXPECT_DOUBLE_EQ(blendSSTCoefficient(0.25, 4.0, 8.0), 7.0);
}

TEST(SSTEquationTest, BlendAtHalfWeightingIsTheMidpoint) {
  EXPECT_DOUBLE_EQ(blendSSTCoefficient(0.5, 4.0, 8.0), 6.0);
}

TEST(SSTEquationTest, BlendingIsExactForMultipleCoefficients) {
  // sigmaK1=0.85/sigmaK2=1.0, sigmaOmega1=0.5/sigmaOmega2=0.856,
  // beta1=0.075/beta2=0.0828 -- not just one coefficient (section 21).
  EXPECT_DOUBLE_EQ(blendSSTCoefficient(0.25, 0.85, 1.0), 0.9625);
  EXPECT_DOUBLE_EQ(blendSSTCoefficient(0.25, 0.5, 0.856), 0.767);
  // 0.25*0.075 + 0.75*0.0828 = 0.01875 + 0.0621 = 0.08085.
  EXPECT_NEAR(blendSSTCoefficient(0.25, 0.075, 0.0828), 0.08085, 1e-12);
}

// --- cross-diffusion (section 14, mandatory) ----------------------------

TEST(SSTEquationTest, CrossDiffusionCoefficientMatchesHandDerivedExample) {
  // k=k0+2x, omega=omega0+3x -> grad(k).grad(omega) = 2*3 = 6 (the
  // gradient-reconstruction half of this test lives in
  // SSTModelTest.CrossDiffusionUsesRealGradients below; this is the
  // coefficient-formula half). rho=1, sigmaOmega2=0.856, omega=5 ->
  // CDkw = max(2*1*0.856*6/5, 1e-10) = max(2.0544, 1e-10) = 2.0544.
  const Real cdkw = computeCrossDiffusionCoefficient(1.0, 0.856, 5.0, 6.0);
  EXPECT_NEAR(cdkw, 2.0544, 1e-12);
}

TEST(SSTEquationTest, CrossDiffusionCoefficientClampsNegativeDotProduct) {
  // grad(k).grad(omega) < 0 -> raw value negative -> clamped to the
  // minimum protection value, never negative or zero (F1's own division
  // by this must never see a non-positive denominator).
  const Real cdkw = computeCrossDiffusionCoefficient(1.0, 0.856, 5.0, -6.0);
  EXPECT_GT(cdkw, 0.0);
  EXPECT_NEAR(cdkw, 1e-10, 1e-15);
}

TEST(SSTEquationTest, CrossDiffusionSourceVanishesAtF1EqualsOne) {
  // Near a wall (F1=1), the outer-region cross-diffusion source must be
  // exactly zero, regardless of grad(k).grad(omega).
  EXPECT_DOUBLE_EQ(computeCrossDiffusionSource(1.0, 0.856, 5.0, 6.0, /*F1=*/1.0), 0.0);
}

TEST(SSTEquationTest, CrossDiffusionSourceIsFullWeightAtF1EqualsZero) {
  // Far from a wall (F1=0): 2*1*1*0.856*6/5 = 2.0544, the same value as
  // the (positive) clamped coefficient above -- consistent since the raw
  // value was already positive here.
  const Real source = computeCrossDiffusionSource(1.0, 0.856, 5.0, 6.0, /*F1=*/0.0);
  EXPECT_NEAR(source, 2.0544, 1e-12);
}

TEST(SSTEquationTest, CrossDiffusionSourceIsHalfWeightAtF1EqualsHalf) {
  const Real source = computeCrossDiffusionSource(1.0, 0.856, 5.0, 6.0, /*F1=*/0.5);
  EXPECT_NEAR(source, 1.0272, 1e-12);
}

TEST(SSTEquationTest, CrossDiffusionSourceCanBeNegative) {
  // Deliberately NOT clamped, unlike the coefficient above.
  const Real source = computeCrossDiffusionSource(1.0, 0.856, 5.0, -6.0, /*F1=*/0.0);
  EXPECT_LT(source, 0.0);
  EXPECT_NEAR(source, -2.0544, 1e-12);
}

// --- F1/F2 bounds (sections 11-12, 38-39, mandatory) --------------------

namespace {
struct SSTState {
  Real k;
  Real omega;
  Real y;
  Real nu;
};
}  // namespace

TEST(SSTEquationTest, F1RemainsBoundedAcrossRepresentativeStates) {
  const Real rho = 1.0;
  const Real betaStar = 0.09;
  const Real sigmaOmega2 = 0.856;
  const SSTState states[] = {
      {0.01, 10.0, 0.001, 1.5e-5},  // near wall
      {0.01, 0.1, 10.0, 1.5e-5},    // far field
      {1e-8, 5.0, 0.1, 1.5e-5},     // small k
      {0.5, 1e6, 0.1, 1.5e-5},      // large omega
      {0.5, 5.0, 1e6, 1.5e-5},      // large wall distance
  };
  for (const auto& s : states) {
    const Real cdkw = computeCrossDiffusionCoefficient(rho, sigmaOmega2, s.omega, 1.0);
    const Real arg1 =
        computeF1Argument(s.k, s.omega, s.y, s.nu, rho, betaStar, sigmaOmega2, cdkw);
    ASSERT_TRUE(std::isfinite(arg1));
    const Real f1 = computeF1(arg1);
    EXPECT_TRUE(std::isfinite(f1));
    EXPECT_GE(f1, 0.0);
    EXPECT_LE(f1, 1.0);
  }
}

TEST(SSTEquationTest, F2RemainsBoundedAcrossRepresentativeStates) {
  const Real betaStar = 0.09;
  const SSTState states[] = {
      {0.01, 10.0, 0.001, 1.5e-5}, {0.01, 0.1, 10.0, 1.5e-5}, {1e-8, 5.0, 0.1, 1.5e-5},
      {0.5, 1e6, 0.1, 1.5e-5},     {0.5, 5.0, 1e6, 1.5e-5},
  };
  for (const auto& s : states) {
    const Real arg2 = computeF2Argument(s.k, s.omega, s.y, s.nu, betaStar);
    ASSERT_TRUE(std::isfinite(arg2));
    const Real f2 = computeF2(arg2);
    EXPECT_TRUE(std::isfinite(f2));
    EXPECT_GE(f2, 0.0);
    EXPECT_LE(f2, 1.0);
  }
}

TEST(SSTEquationTest, F1ApproachesOneVeryNearAWall) {
  // Very small wall distance -> arg1's first term dominates and is huge
  // -> tanh(arg1^4) saturates to (numerically) exactly 1.0.
  const Real cdkw = computeCrossDiffusionCoefficient(1.0, 0.856, 5.0, 1.0);
  const Real arg1 = computeF1Argument(0.5, 5.0, /*y=*/1e-8, 1.5e-5, 1.0, 0.09, 0.856, cdkw);
  EXPECT_DOUBLE_EQ(computeF1(arg1), 1.0);
}

TEST(SSTEquationTest, F1ApproachesZeroFarFromAWallWithLowViscosityRatio) {
  // Large wall distance and a large omega (so sqrt(k)/(betaStar*omega*y)
  // and 500*nu/(y^2*omega) are both tiny) -> arg1 -> 0 -> F1 -> 0.
  const Real cdkw = computeCrossDiffusionCoefficient(1.0, 0.856, 1000.0, 1.0);
  const Real arg1 = computeF1Argument(0.01, 1000.0, /*y=*/1000.0, 1.5e-5, 1.0, 0.09, 0.856, cdkw);
  EXPECT_NEAR(computeF1(arg1), 0.0, 1e-6);
}

// --- k/omega diffusion (sections 24-25, mandatory) -----------------------
//
// SST's diffusivity is Gamma = mu + sigma*mu_t (the same *linear* form
// as standard k-omega -- see KOmegaEquation.hpp's own header comment on
// why that form is distinct from k-epsilon's reciprocal one), but with a
// *per-cell blended* sigma rather than one uniform constant, so
// KOmegaEquation.hpp's own computeLinearEffectiveDiffusivity (which
// takes one scalar sigma for the whole field) does not directly apply
// here -- SSTModel.cpp instead composes mu + blendSSTCoefficient(F1,
// sigma1, sigma2)*mu_t inline, per cell. These tests hand-verify exactly
// that composition.

TEST(SSTEquationTest, KDiffusivityMatchesHandDerivedBlendedValue) {
  // mu=1, mu_t=3, F1=0.25, sigmaK1=0.85, sigmaK2=1.0 -> blended sigmaK =
  // 0.25*0.85 + 0.75*1.0 = 0.9625 -> Gamma_k = 1 + 0.9625*3 = 3.8875.
  const Real mu = 1.0;
  const Real muT = 3.0;
  const Real blendedSigmaK = blendSSTCoefficient(0.25, 0.85, 1.0);
  EXPECT_DOUBLE_EQ(blendedSigmaK, 0.9625);
  const Real gammaK = mu + (blendedSigmaK * muT);
  EXPECT_DOUBLE_EQ(gammaK, 3.8875);
}

TEST(SSTEquationTest, OmegaDiffusivityMatchesHandDerivedBlendedValue) {
  // mu=1, mu_t=3, F1=0.25, sigmaOmega1=0.5, sigmaOmega2=0.856 -> blended
  // sigmaOmega = 0.25*0.5 + 0.75*0.856 = 0.767 -> Gamma_omega =
  // 1 + 0.767*3 = 3.301.
  const Real mu = 1.0;
  const Real muT = 3.0;
  const Real blendedSigmaOmega = blendSSTCoefficient(0.25, 0.5, 0.856);
  EXPECT_NEAR(blendedSigmaOmega, 0.767, 1e-12);
  const Real gammaOmega = mu + (blendedSigmaOmega * muT);
  EXPECT_NEAR(gammaOmega, 3.301, 1e-12);
}

// --- eddy-viscosity limiter (sections 15, 17-18, 42, mandatory) --------

TEST(SSTEquationTest, TurbulentViscosityOmegaDominatedBranch) {
  // a1*omega=0.31*10=3.1 > S*F2=1*1=1 -> denominator=a1*omega ->
  // mu_t = rho*a1*k/(a1*omega) = rho*k/omega (same as standard k-omega).
  const Real muT = computeSSTTurbulentViscosity(/*rho=*/1.0, /*a1=*/0.31, /*k=*/2.0,
                                                /*omega=*/10.0, /*S=*/1.0, /*F2=*/1.0);
  EXPECT_DOUBLE_EQ(muT, 1.0 * 2.0 / 10.0);  // = 0.2.
  EXPECT_NEAR(muT, 0.2, 1e-12);
}

TEST(SSTEquationTest, TurbulentViscosityStrainDominatedBranch) {
  // S*F2=100*1=100 > a1*omega=0.31*10=3.1 -> denominator=S*F2 ->
  // mu_t = rho*a1*k/(S*F2) = 1*0.31*2/100 = 0.0062.
  const Real muT = computeSSTTurbulentViscosity(/*rho=*/1.0, /*a1=*/0.31, /*k=*/2.0,
                                                /*omega=*/10.0, /*S=*/100.0, /*F2=*/1.0);
  EXPECT_NEAR(muT, 0.0062, 1e-12);
  // Independently proves the two branches actually give different
  // values for the same k/omega/a1/rho (the limiter is genuinely active).
  const Real omegaDominated = computeSSTTurbulentViscosity(1.0, 0.31, 2.0, 10.0, 1.0, 1.0);
  EXPECT_NE(muT, omegaDominated);
}

TEST(SSTEquationTest, TurbulentViscosityRejectsInvalidInputs) {
  EXPECT_THROW((void)computeSSTTurbulentViscosity(1.0, 0.0, 2.0, 10.0, 1.0, 1.0),
              InvalidArgumentError);
  EXPECT_THROW((void)computeSSTTurbulentViscosity(1.0, 0.31, 2.0, 0.0, 1.0, 1.0),
              InvalidArgumentError);
  EXPECT_THROW((void)computeSSTTurbulentViscosity(1.0, 0.31, 2.0, 10.0, -1.0, 1.0),
              InvalidArgumentError);
}

// --- production limiter (sections 19-20, 44, mandatory) -----------------

TEST(SSTEquationTest, ProductionLimiterUnlimitedBranch) {
  // limit = 10*0.09*1*2*3 = 5.4; productionK=3 < 5.4 -> unlimited.
  const Real limited = limitProduction(/*productionK=*/3.0, /*factor=*/10.0, /*betaStar=*/0.09,
                                       /*rho=*/1.0, /*k=*/2.0, /*omega=*/3.0);
  EXPECT_DOUBLE_EQ(limited, 3.0);
}

TEST(SSTEquationTest, ProductionLimiterLimitedBranch) {
  // productionK=10 > limit=5.4 -> limited to 5.4.
  const Real limited = limitProduction(/*productionK=*/10.0, /*factor=*/10.0, /*betaStar=*/0.09,
                                       /*rho=*/1.0, /*k=*/2.0, /*omega=*/3.0);
  EXPECT_DOUBLE_EQ(limited, 5.4);
}

TEST(SSTEquationTest, ProductionLimiterRejectsInvalidFactorOrBetaStar) {
  EXPECT_THROW((void)limitProduction(3.0, 0.0, 0.09, 1.0, 2.0, 3.0), InvalidArgumentError);
  EXPECT_THROW((void)limitProduction(3.0, 10.0, 0.0, 1.0, 2.0, 3.0), InvalidArgumentError);
}
