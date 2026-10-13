// P15 -- temperature-dependent properties and sensible enthalpy.
//
// The test that matters most is NaiveCpTimesTemperatureIsWrongForVariableCp:
// it pins the exact error the enthalpy formulation exists to prevent, so a
// future "simplification" back to h = cp*T fails loudly instead of quietly
// conserving the wrong quantity.
#include <gtest/gtest.h>

#include <cmath>
#include <vector>

#include "cfd/core/Exception.hpp"
#include "cfd/fields/ScalarField.hpp"
#include "cfd/thermal/Enthalpy.hpp"
#include "cfd/thermal/TemperatureDependentProperty.hpp"

using cfd::Index;
using cfd::InvalidArgumentError;
using cfd::Real;
using cfd::fields::ScalarField;
using cfd::thermal::SensibleEnthalpy;
using cfd::thermal::TemperatureDependentProperty;

namespace {

// cp(T) = 1000 + 0.5 T, valid 200..2000 K. Deliberately a real variation:
// cp doubles across the range, so a formulation error cannot hide.
TemperatureDependentProperty varyingCp() {
  return TemperatureDependentProperty({1000.0, 0.5}, 200.0, 2000.0, "cp");
}

}  // namespace

// --- the property model -----------------------------------------------------

TEST(TemperatureProperty, PolynomialMatchesHandCalculation) {
  // k(T) = 2 + 0.01 T + 1e-5 T^2 at T = 300:
  //   2 + 3 + 1e-5*90000 = 2 + 3 + 0.9 = 5.9
  const TemperatureDependentProperty k({2.0, 0.01, 1.0e-5}, 200.0, 1000.0, "k");
  EXPECT_DOUBLE_EQ(k.evaluate(300.0), 5.9);
}

TEST(TemperatureProperty, ConstantIsTheSingleCoefficientCase) {
  const auto k = TemperatureDependentProperty::constant(7.5, "k");
  EXPECT_TRUE(k.isConstant());
  EXPECT_DOUBLE_EQ(k.evaluate(300.0), 7.5);
  EXPECT_DOUBLE_EQ(k.evaluate(900.0), 7.5) << "a constant must not vary with T";
}

TEST(TemperatureProperty, OutOfRangeIsRefusedNotExtrapolated) {
  const TemperatureDependentProperty k({2.0, 0.01}, 300.0, 400.0, "k");
  EXPECT_NO_THROW((void)k.evaluate(350.0));
  EXPECT_THROW((void)k.evaluate(299.0), InvalidArgumentError);
  EXPECT_THROW((void)k.evaluate(401.0), InvalidArgumentError);
  EXPECT_THROW((void)k.evaluate(std::nan("")), InvalidArgumentError);
}

TEST(TemperatureProperty, NonPositiveOverTheRangeIsRejectedAtConstruction) {
  // k(T) = 10 - 0.05 T is positive at 200 (=0) ... actually 10-10 = 0 at 200,
  // and negative above. A property that goes non-positive inside its own
  // declared range is not a material; catching it here keeps the failure out
  // of a linear solve where its cause would be unrecognisable.
  EXPECT_THROW(TemperatureDependentProperty({10.0, -0.05}, 200.0, 400.0, "k"),
               InvalidArgumentError);
  EXPECT_THROW(TemperatureDependentProperty({}, 200.0, 400.0, "k"), InvalidArgumentError);
  EXPECT_THROW(TemperatureDependentProperty({1.0}, 400.0, 200.0, "k"), InvalidArgumentError);
}

TEST(TemperatureProperty, FieldEvaluationNamesTheOffendingCell) {
  const TemperatureDependentProperty k({2.0, 0.01}, 300.0, 400.0, "k");
  ScalarField temperature(4, 350.0);
  EXPECT_NO_THROW((void)k.evaluateField(temperature));
  temperature[2] = 500.0;
  try {
    (void)k.evaluateField(temperature);
    FAIL() << "an out-of-range cell was accepted";
  } catch (const InvalidArgumentError& error) {
    const std::string message = error.what();
    EXPECT_NE(message.find("cell 2"), std::string::npos) << message;
  }
}

// --- enthalpy ---------------------------------------------------------------

TEST(SensibleEnthalpyTest, ConstantCpGivesTheLinearForm) {
  // h - h_ref = cp (T - T_ref) exactly, when cp is constant.
  const SensibleEnthalpy h(TemperatureDependentProperty::constant(1005.0, "cp"), 298.15);
  EXPECT_NEAR(h.enthalpy(398.15), 1005.0 * 100.0, 1e-9);
  EXPECT_NEAR(h.enthalpy(298.15), 0.0, 1e-9) << "h(T_ref) must be h_ref";
}

TEST(SensibleEnthalpyTest, VariableCpMatchesTheClosedFormIntegral) {
  // cp = b0 + b1 T  =>  h(T) - h(Tref) = b0 (T - Tref) + b1/2 (T^2 - Tref^2)
  const Real b0 = 1000.0;
  const Real b1 = 0.5;
  const Real tRef = 300.0;
  const Real t = 800.0;
  const SensibleEnthalpy h(varyingCp(), tRef);
  const Real expected = b0 * (t - tRef) + 0.5 * b1 * (t * t - tRef * tRef);
  EXPECT_NEAR(h.enthalpy(t), expected, 1e-8);
}

TEST(SensibleEnthalpyTest, NaiveCpTimesTemperatureIsWrongForVariableCp) {
  // THE test this formulation exists for. cp*T is not the integral of cp dT.
  // For cp = b0 + b1 T the integral carries b1/2 T^2; the product carries
  // b1 T^2 -- a factor of two on the quadratic term.
  const Real tRef = 300.0;
  const Real t = 1500.0;
  const SensibleEnthalpy h(varyingCp(), tRef);

  const Real correct = h.enthalpy(t);
  const Real naive = h.specificHeat(t) * t - h.specificHeat(tRef) * tRef;

  // Not a rounding difference: assert they are far apart, so this test cannot
  // pass if someone replaces the integral with the product.
  const Real relativeGap = std::abs(naive - correct) / std::abs(correct);
  EXPECT_GT(relativeGap, 0.1) << "correct " << correct << " vs naive " << naive
                              << " -- if these are close, the integral was replaced by a product";
}

TEST(SensibleEnthalpyTest, TemperatureEnthalpyRoundTrip) {
  const SensibleEnthalpy h(varyingCp(), 300.0);
  for (Real t = 250.0; t <= 1900.0; t += 50.0) {
    const Real roundTripped = h.temperature(h.enthalpy(t));
    EXPECT_NEAR(roundTripped, t, 1e-9) << "T = " << t;
  }
}

TEST(SensibleEnthalpyTest, RoundTripHoldsForConstantCpToo) {
  const SensibleEnthalpy h(TemperatureDependentProperty::constant(1005.0, "cp"), 298.15);
  for (Real t = 200.0; t <= 900.0; t += 100.0) {
    EXPECT_NEAR(h.temperature(h.enthalpy(t)), t, 1e-9) << "T = " << t;
  }
}

TEST(SensibleEnthalpyTest, EnthalpyIsStrictlyIncreasing) {
  // Required for the inversion to be well posed, and guaranteed by cp > 0.
  const SensibleEnthalpy h(varyingCp(), 300.0);
  Real previous = h.enthalpy(200.0);
  for (Real t = 210.0; t <= 2000.0; t += 10.0) {
    const Real current = h.enthalpy(t);
    EXPECT_GT(current, previous) << "T = " << t;
    previous = current;
  }
}

TEST(SensibleEnthalpyTest, OutOfRangeEnthalpyIsRefusedNotClamped) {
  const SensibleEnthalpy h(varyingCp(), 300.0);
  const Real tooHigh = h.enthalpy(2000.0) * 2.0;
  const Real tooLow = h.enthalpy(200.0) - 1.0e9;
  EXPECT_THROW((void)h.temperature(tooHigh), InvalidArgumentError);
  EXPECT_THROW((void)h.temperature(tooLow), InvalidArgumentError);
  EXPECT_THROW((void)h.temperature(std::nan("")), InvalidArgumentError);
}

TEST(SensibleEnthalpyTest, ReferenceEnthalpyShiftsTheWholeCurve) {
  const SensibleEnthalpy zero(varyingCp(), 300.0, 0.0);
  const SensibleEnthalpy offset(varyingCp(), 300.0, 5.0e5);
  for (Real t = 400.0; t <= 1200.0; t += 200.0) {
    EXPECT_NEAR(offset.enthalpy(t) - zero.enthalpy(t), 5.0e5, 1e-6) << "T = " << t;
    // And the inversion must undo the shift consistently.
    EXPECT_NEAR(offset.temperature(offset.enthalpy(t)), t, 1e-9) << "T = " << t;
  }
}
