// P3-PHYS-006 section 6: ideal-gas EOS exact tests.
#include <gtest/gtest.h>

#include <cmath>
#include <limits>

#include "cfd/compressible/IdealGasEOS.hpp"
#include "cfd/core/Exception.hpp"

using cfd::InvalidArgumentError;
using cfd::Real;
using cfd::compressible::IdealGasEOS;

TEST(IdealGasEOSTest, MatchesKnownStandardAtmosphereState) {
  // p=101325 Pa, T=300 K, R=287.05 J/(kg K) (dry air) -> rho ~ 1.1766 kg/m^3.
  const Real p = 101325.0, T = 300.0, R = 287.05;
  const IdealGasEOS eos(R);
  const Real expected = p / (R * T);  // computed from the same formula/constants, not hand-typed.
  EXPECT_DOUBLE_EQ(eos.density(p, T), expected);
  EXPECT_NEAR(eos.density(p, T), 1.1766, 1e-4);
}

TEST(IdealGasEOSTest, DoublingPressureAtFixedTemperatureDoublesDensity) {
  const IdealGasEOS eos(287.05);
  const Real rho1 = eos.density(101325.0, 300.0);
  const Real rho2 = eos.density(202650.0, 300.0);
  EXPECT_NEAR(rho2, 2.0 * rho1, 1e-9 * rho1);
}

TEST(IdealGasEOSTest, DoublingTemperatureAtFixedPressureHalvesDensity) {
  const IdealGasEOS eos(287.05);
  const Real rho1 = eos.density(101325.0, 300.0);
  const Real rho2 = eos.density(101325.0, 600.0);
  EXPECT_NEAR(rho2, 0.5 * rho1, 1e-9 * rho1);
}

TEST(IdealGasEOSTest, RejectsNonPositivePressureOrTemperature) {
  const IdealGasEOS eos(287.05);
  EXPECT_THROW((void)eos.density(0.0, 300.0), InvalidArgumentError);
  EXPECT_THROW((void)eos.density(-101325.0, 300.0), InvalidArgumentError);
  EXPECT_THROW((void)eos.density(101325.0, 0.0), InvalidArgumentError);
  EXPECT_THROW((void)eos.density(101325.0, -300.0), InvalidArgumentError);
}

TEST(IdealGasEOSTest, RejectsNonFinitePressureOrTemperature) {
  const IdealGasEOS eos(287.05);
  const Real nan = std::numeric_limits<Real>::quiet_NaN();
  const Real inf = std::numeric_limits<Real>::infinity();
  EXPECT_THROW((void)eos.density(nan, 300.0), InvalidArgumentError);
  EXPECT_THROW((void)eos.density(inf, 300.0), InvalidArgumentError);
  EXPECT_THROW((void)eos.density(101325.0, nan), InvalidArgumentError);
  EXPECT_THROW((void)eos.density(101325.0, inf), InvalidArgumentError);
}

TEST(IdealGasEOSTest, RejectsNonPositiveGasConstant) {
  EXPECT_THROW(IdealGasEOS(0.0), InvalidArgumentError);
  EXPECT_THROW(IdealGasEOS(-287.05), InvalidArgumentError);
}

TEST(IdealGasEOSTest, RejectsNonFiniteGasConstant) {
  const Real nan = std::numeric_limits<Real>::quiet_NaN();
  const Real inf = std::numeric_limits<Real>::infinity();
  EXPECT_THROW(IdealGasEOS{nan}, InvalidArgumentError);
  EXPECT_THROW(IdealGasEOS{inf}, InvalidArgumentError);
}

// --- EOS derivatives (section 15/34) --------------------------------------

TEST(IdealGasEOSTest, DDensityDPressureMatchesAnalyticalFormula) {
  const Real R = 287.05, T = 300.0;
  const IdealGasEOS eos(R);
  EXPECT_DOUBLE_EQ(eos.dDensityDPressure(101325.0, T), 1.0 / (R * T));
}

TEST(IdealGasEOSTest, DDensityDPressureIsIndependentOfPressure) {
  // d(rho)/dp = 1/(R*T) has no p-dependence for an ideal gas.
  const Real R = 287.05, T = 300.0;
  const IdealGasEOS eos(R);
  EXPECT_DOUBLE_EQ(eos.dDensityDPressure(50000.0, T), eos.dDensityDPressure(200000.0, T));
}

TEST(IdealGasEOSTest, DDensityDTemperatureMatchesAnalyticalFormula) {
  const Real R = 287.05, T = 300.0, p = 101325.0;
  const IdealGasEOS eos(R);
  const Real expected = -p / (R * T * T);
  EXPECT_DOUBLE_EQ(eos.dDensityDTemperature(p, T), expected);
}

TEST(IdealGasEOSTest, DDensityDTemperatureIsNegative) {
  // Density decreases with temperature at constant pressure.
  const IdealGasEOS eos(287.05);
  EXPECT_LT(eos.dDensityDTemperature(101325.0, 300.0), 0.0);
}

// Section 34: a known pressure-correction p' has a known expected
// density correction rho' = p'/(R*T) at constant T -- the "reduced
// pressure-correction test... should not require the entire nonlinear
// CFD solver" this section explicitly asks for.
TEST(IdealGasEOSTest, PressureCorrectionGivesExpectedDensityCorrection) {
  const Real R = 287.05, T = 300.0;
  const IdealGasEOS eos(R);
  const Real pPrime = 50.0;  // a small pressure correction [Pa].
  const Real rhoPrime = eos.dDensityDPressure(101325.0, T) * pPrime;
  EXPECT_NEAR(rhoPrime, pPrime / (R * T), 1e-12);
}
