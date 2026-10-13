// P3-PHYS-006 section 6: ideal-gas EOS exact tests.
#include <gtest/gtest.h>

#include <cmath>
#include <limits>
#include <string>

#include "cfd/compressible/EquationOfState.hpp"
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

// --- P17.2: the inverse, p(rho, T) -----------------------------------------
//
// The forward direction (p,T)->rho existed; the inverse did not, so no test could
// close a round trip. These are on the ABSTRACTION deliberately: a caller holding
// an EquationOfState& must be able to invert the state relation without knowing
// which EOS it has.
//
// Bound: 1e-12 relative. p/(R*T) then rho*R*T is two roundings of a double, so exact
// bitwise recovery is not guaranteed for arbitrary values. 1e-12 is ~4500x the double
// epsilon and four orders tighter than the 1e-9 the low-Mach EOS-consistency gate
// already uses. Derived from the operation, not fitted to a result.

namespace {

// The states the P17.2 dry-run recorded, spanning three gases and five decades of
// pressure -- chosen so a hard-coded or R-dropping implementation cannot reproduce
// them. Not the defaults.
struct EosState {
  Real R, p, T;
  const char* label;
};
constexpr EosState kP17States[] = {
    {287.05, 101325.0, 288.15, "air, sea-level standard"},
    {287.05, 250000.0, 450.00, "air, compressed and hot"},
    {2077.10, 50000.0, 120.00, "helium, low p, cryogenic"},
    {461.50, 611.657, 273.16, "water vapour, triple point"},
};

}  // namespace

TEST(IdealGasEOSTest, PressureMatchesRhoRT) {
  // R4: against an independently formed expectation, at every state.
  for (const auto& s : kP17States) {
    const IdealGasEOS eos(s.R);
    const Real rho = eos.density(s.p, s.T);
    const Real expected = rho * s.R * s.T;
    EXPECT_DOUBLE_EQ(eos.pressure(rho, s.T), expected) << s.label;
  }
}

TEST(IdealGasEOSTest, PressureToDensityRoundTripRecoversDensity) {
  // R2: (rho,T) -> p -> rho.
  for (const auto& s : kP17States) {
    const IdealGasEOS eos(s.R);
    const Real rho = eos.density(s.p, s.T);
    const Real recovered = eos.density(eos.pressure(rho, s.T), s.T);
    EXPECT_NEAR(recovered, rho, 1e-12 * rho) << s.label;
  }
}

TEST(IdealGasEOSTest, DensityToPressureRoundTripRecoversPressure) {
  // R1: (p,T) -> rho -> p.
  for (const auto& s : kP17States) {
    const IdealGasEOS eos(s.R);
    const Real recovered = eos.pressure(eos.density(s.p, s.T), s.T);
    EXPECT_NEAR(recovered, s.p, 1e-12 * s.p) << s.label;
  }
}

TEST(IdealGasEOSTest, PressureIsReachableThroughTheAbstraction) {
  // The reason the method is on EquationOfState and not only on IdealGasEOS: a
  // caller that holds the base reference must be able to invert without down-casting.
  const IdealGasEOS concrete(287.05);
  const cfd::compressible::EquationOfState& eos = concrete;
  const Real rho = eos.density(101325.0, 288.15);
  EXPECT_NEAR(eos.pressure(rho, 288.15), 101325.0, 1e-12 * 101325.0);
}

TEST(IdealGasEOSTest, PressureRejectsNonPositiveDensityOrTemperature) {
  // V1, V2 -- and the message must name the field, as the forward direction's does.
  const IdealGasEOS eos(287.05);
  EXPECT_THROW(eos.pressure(0.0, 300.0), InvalidArgumentError);
  EXPECT_THROW(eos.pressure(-1.0, 300.0), InvalidArgumentError);
  EXPECT_THROW(eos.pressure(1.2, 0.0), InvalidArgumentError);
  EXPECT_THROW(eos.pressure(1.2, -300.0), InvalidArgumentError);
  try {
    eos.pressure(-1.0, 300.0);
    FAIL() << "a negative density was accepted";
  } catch (const InvalidArgumentError& e) {
    EXPECT_NE(std::string(e.what()).find("density"), std::string::npos) << e.what();
  }
}

TEST(IdealGasEOSTest, PressureRejectsNonFiniteDensityOrTemperature) {
  const IdealGasEOS eos(287.05);
  const Real inf = std::numeric_limits<Real>::infinity();
  const Real nan = std::numeric_limits<Real>::quiet_NaN();
  EXPECT_THROW(eos.pressure(inf, 300.0), InvalidArgumentError);
  EXPECT_THROW(eos.pressure(nan, 300.0), InvalidArgumentError);
  EXPECT_THROW(eos.pressure(1.2, inf), InvalidArgumentError);
  EXPECT_THROW(eos.pressure(1.2, nan), InvalidArgumentError);
}

TEST(IdealGasEOSTest, PressureRejectsAnOverflowingProduct) {
  // V3: rho and T can each be finite and positive while rho*R*T overflows to inf.
  // The result is checked, not assumed -- returning inf would hand a caller a
  // "valid" state that poisons everything downstream.
  const IdealGasEOS eos(287.05);
  const Real huge = std::numeric_limits<Real>::max();
  EXPECT_THROW(eos.pressure(huge, huge), InvalidArgumentError);
}

TEST(IdealGasEOSTest, PressureIsDeterministic) {
  // V4: bitwise identical across repeated calls.
  const IdealGasEOS eos(287.05);
  const Real first = eos.pressure(1.2250122659906946, 288.15);
  for (int i = 0; i < 64; ++i) {
    EXPECT_EQ(eos.pressure(1.2250122659906946, 288.15), first);
  }
}

TEST(IdealGasEOSTest, PressureScalesLinearlyInDensityAndTemperature) {
  // A shape check the round trips cannot give: doubling either argument doubles p.
  // A constant-returning implementation passes every round trip that starts from its
  // own output, but fails this.
  const IdealGasEOS eos(287.05);
  const Real base = eos.pressure(1.2, 300.0);
  EXPECT_DOUBLE_EQ(eos.pressure(2.4, 300.0), 2.0 * base);
  EXPECT_DOUBLE_EQ(eos.pressure(1.2, 600.0), 2.0 * base);
}

// ---------------------------------------------------------------------------
// P17.2 -- the third direction, T(p, rho), completing the two-argument closure.
//
// With density(p,T) and pressure(rho,T) already present, any one of {p, rho, T} is now
// recoverable from the other two. Before this, recovering T forced the CALLER to write
// p/(rho*R) itself -- duplicating the state relation outside the EOS, which is exactly
// what EquationOfState's own header says the abstraction exists to prevent. Measured on
// the pre-P17.2 tree in results/p17/ideal_gas_eos/logs/00_dryrun.log group D.
// ---------------------------------------------------------------------------

TEST(IdealGasEOSTest, TemperatureMatchesPOverRhoR) {
  const Real R = 287.05;
  const IdealGasEOS eos(R);
  const Real p = 101325.0;
  const Real rho = 1.2250122659906946;  // the dry-run's measured density(101325, 288.15)
  const Real expected = p / (rho * R);
  EXPECT_NEAR(eos.temperature(p, rho), expected, 1e-12 * expected);
}

TEST(IdealGasEOSTest, TemperatureToPressureRoundTripRecoversPressure) {
  // (p, rho) -> T -> p. The third round trip, alongside the two the earlier P17.2 added.
  const IdealGasEOS eos(287.05);
  const Real p = 101325.0;
  const Real rho = eos.density(p, 288.15);
  const Real T = eos.temperature(p, rho);
  const Real pBack = eos.pressure(rho, T);
  EXPECT_NEAR(pBack, p, 1e-12 * p);
}

TEST(IdealGasEOSTest, ThreeWayClosureHoldsAcrossGasesAndStateRanges) {
  // Gate criterion C7: all three directions and all three round trips, over 3 gases x 3
  // state ranges -- low / ambient / high in BOTH p and T. A hard-coded or single-state
  // implementation cannot reproduce nine distinct states across three gas constants.
  struct Gas {
    Real R;
    const char* label;
  };
  struct State {
    Real p, T;
    const char* label;
  };
  const Gas gases[] = {{287.05, "air"}, {2077.10, "helium"}, {461.50, "water vapour"}};
  const State states[] = {
      {1.0e3, 200.0, "low p, low T"},
      {101325.0, 288.15, "sea-level standard"},
      {5.0e6, 1500.0, "high p, high T"},
  };

  int checked = 0;
  for (const auto& g : gases) {
    const IdealGasEOS eos(g.R);
    for (const auto& s : states) {
      const std::string where = std::string(g.label) + " / " + s.label;

      // direction 1: (p,T) -> rho
      const Real rho = eos.density(s.p, s.T);
      EXPECT_NEAR(rho, s.p / (g.R * s.T), 1e-12 * rho) << where;

      // direction 2: (rho,T) -> p
      EXPECT_NEAR(eos.pressure(rho, s.T), s.p, 1e-12 * s.p) << where;

      // direction 3: (p,rho) -> T
      EXPECT_NEAR(eos.temperature(s.p, rho), s.T, 1e-12 * s.T) << where;

      // round trip A: (p,T) -> rho -> p
      EXPECT_NEAR(eos.pressure(eos.density(s.p, s.T), s.T), s.p, 1e-12 * s.p) << where;
      // round trip B: (rho,T) -> p -> rho
      EXPECT_NEAR(eos.density(eos.pressure(rho, s.T), s.T), rho, 1e-12 * rho) << where;
      // round trip C: (p,rho) -> T -> p
      EXPECT_NEAR(eos.pressure(rho, eos.temperature(s.p, rho)), s.p, 1e-12 * s.p) << where;

      ++checked;
    }
  }
  EXPECT_EQ(checked, 9);
}

TEST(IdealGasEOSTest, TemperatureIsReachableThroughTheAbstraction) {
  // Held as EquationOfState&, not IdealGasEOS: a density-based solver recovering
  // primitives from a conserved state must not have to down-cast to invert the relation.
  const IdealGasEOS concrete(287.05);
  const cfd::compressible::EquationOfState& eos = concrete;
  const Real p = 250000.0;
  const Real rho = eos.density(p, 450.0);
  EXPECT_NEAR(eos.temperature(p, rho), 450.0, 1e-12 * 450.0);
}

TEST(IdealGasEOSTest, TemperatureRejectsNonPositivePressureOrDensity) {
  const IdealGasEOS eos(287.05);
  EXPECT_THROW((void)eos.temperature(0.0, 1.2), InvalidArgumentError);
  EXPECT_THROW((void)eos.temperature(-101325.0, 1.2), InvalidArgumentError);
  EXPECT_THROW((void)eos.temperature(101325.0, 0.0), InvalidArgumentError);
  EXPECT_THROW((void)eos.temperature(101325.0, -1.2), InvalidArgumentError);
}

TEST(IdealGasEOSTest, TemperatureRejectsNonFinitePressureOrDensity) {
  const IdealGasEOS eos(287.05);
  const Real qnan = std::numeric_limits<Real>::quiet_NaN();
  const Real pinf = std::numeric_limits<Real>::infinity();
  for (const Real bad : {qnan, pinf, -pinf}) {
    EXPECT_THROW((void)eos.temperature(bad, 1.2), InvalidArgumentError) << bad;
    EXPECT_THROW((void)eos.temperature(101325.0, bad), InvalidArgumentError) << bad;
  }
}

TEST(IdealGasEOSTest, TemperatureRejectsAnOverflowingQuotient) {
  // The mirror of PressureRejectsAnOverflowingProduct: p and rho are each finite and
  // strictly positive, yet p/(rho*R) overflows to infinity. Checked rather than assumed.
  const IdealGasEOS eos(287.05);
  EXPECT_THROW((void)eos.temperature(1.0e308, 1.0e-308), InvalidArgumentError);
}

TEST(IdealGasEOSTest, TemperatureMessageNamesTheOffendingField) {
  const IdealGasEOS eos(287.05);
  try {
    (void)eos.temperature(101325.0, -1.0);
    FAIL() << "a negative density must be rejected";
  } catch (const InvalidArgumentError& e) {
    const std::string what = e.what();
    EXPECT_NE(what.find("density"), std::string::npos) << what;
  }
}

TEST(IdealGasEOSTest, EveryDirectionIsBitwiseDeterministic) {
  // Gate group D. Bitwise is the correct criterion and needs no caveat: every path is
  // straight-line double arithmetic -- no reduction, no threading, no iteration.
  const IdealGasEOS eos(287.05);
  const Real p = 101325.0, T = 288.15;
  const Real rho0 = eos.density(p, T);
  const Real p0 = eos.pressure(rho0, T);
  const Real t0 = eos.temperature(p, rho0);
  const Real dp0 = eos.dDensityDPressure(p, T);
  const Real dt0 = eos.dDensityDTemperature(p, T);
  for (int i = 0; i < 64; ++i) {
    EXPECT_EQ(eos.density(p, T), rho0) << "repeat " << i;
    EXPECT_EQ(eos.pressure(rho0, T), p0) << "repeat " << i;
    EXPECT_EQ(eos.temperature(p, rho0), t0) << "repeat " << i;
    EXPECT_EQ(eos.dDensityDPressure(p, T), dp0) << "repeat " << i;
    EXPECT_EQ(eos.dDensityDTemperature(p, T), dt0) << "repeat " << i;
  }
}
