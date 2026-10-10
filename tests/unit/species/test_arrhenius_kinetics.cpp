// P18.5 -- Arrhenius kinetics.
//
// EVERY EXPECTED VALUE CAME FROM AN INDEPENDENT ARBITRARY-PRECISION IMPLEMENTATION, which G6 names
// by name: results/p18/arrhenius_chemistry/tools/arrhenius_reference.py, mpmath at 60 decimal
// digits, NO CFDApp code imported or consulted. Its full output is preserved at
// results/p18/arrhenius_chemistry/data/reference_values.txt and the values below are transcribed
// from it at 17 significant figures, which round-trips a double exactly.
//
// THRESHOLD: G6's own, inherited verbatim -- relative error <= 32 * eps on each rate. "32 eps is
// P17.7's own frozen multiple for the same kind of pointwise closed-form comparison, inherited
// rather than invented."
#include <gtest/gtest.h>

#include <cmath>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "cfd/core/Exception.hpp"
#include "cfd/mesh/MeshGeometry.hpp"
#include "cfd/species/ArrheniusKinetics.hpp"
#include "cfd/species/MassFractionState.hpp"
#include "cfd/species/MixtureThermodynamics.hpp"
#include "cfd/species/ReactionMechanism.hpp"
#include "cfd/species/SpeciesRegistry.hpp"
#include "cfd/thermal/Enthalpy.hpp"
#include "cfd/thermal/TemperatureDependentProperty.hpp"

using cfd::Index;
using cfd::InvalidArgumentError;
using cfd::NumericalError;
using cfd::Real;
using cfd::fields::ScalarField;
using cfd::mesh::Mesh;
using cfd::mesh::MeshGeometry;
using cfd::species::ArrheniusParameters;
using cfd::species::arrheniusProgressRates;
using cfd::species::exponentDiagnostics;
using cfd::species::kUniversalGasConstant;
using cfd::species::MassFractionState;
using cfd::species::MixtureThermodynamics;
using cfd::species::molarConcentrations;
using cfd::species::Reaction;
using cfd::species::ReactionDirection;
using cfd::species::ReactionKinetics;
using cfd::species::ReactionMechanism;
using cfd::species::ReactionRateParameters;
using cfd::species::reactionSpeciesSources;
using cfd::species::sourceConservation;
using cfd::species::SpeciesRegistry;
using cfd::species::SpeciesThermodynamics;

namespace {

constexpr Real kEps = std::numeric_limits<Real>::epsilon();

// G6's allowance, relative.
Real g6(Real scale) { return 32.0 * kEps * std::abs(scale); }

// The reference mechanism: 2 H2 + O2 -> 2 H2O, with the exactly-balancing weights P18.4
// established.
constexpr Real kMH2 = 0.002016;
constexpr Real kMO2 = 0.031998;
constexpr Real kMH2O = 0.018015;

// The reference kinetics.
constexpr Real kA = 1.5e8;
constexpr Real kN = 0.5;
constexpr Real kEa = 1.25e5;  // J/mol

cfd::thermal::SensibleEnthalpy cp(Real value, const std::string& name) {
  return cfd::thermal::SensibleEnthalpy(
      cfd::thermal::TemperatureDependentProperty::constant(value, name), 298.15, 0.0);
}

SpeciesRegistry h2Registry() { return SpeciesRegistry({"H2", "O2", "H2O"}); }

MixtureThermodynamics h2Mixture() {
  std::vector<SpeciesThermodynamics> species;
  species.push_back(SpeciesThermodynamics(kMH2, cp(14307.0, "cp_H2")));
  species.push_back(SpeciesThermodynamics(kMO2, cp(918.0, "cp_O2")));
  species.push_back(SpeciesThermodynamics(kMH2O, cp(1996.0, "cp_H2O")));
  return MixtureThermodynamics(h2Registry(), std::move(species));
}

ReactionMechanism h2Mechanism(ReactionDirection direction = ReactionDirection::Irreversible) {
  std::vector<Reaction> reactions;
  reactions.push_back(Reaction("2H2+O2->2H2O", h2Registry(), {{"H2", 2.0}, {"O2", 1.0}},
                               {{"H2O", 2.0}}, direction));
  return ReactionMechanism(h2Registry(), std::move(reactions), h2Mixture());
}

ReactionKinetics h2Kinetics(const ReactionMechanism& mechanism) {
  std::vector<ReactionRateParameters> parameters;
  parameters.push_back({"2H2+O2->2H2O", ArrheniusParameters(kA, kN, kEa), std::nullopt});
  return ReactionKinetics(mechanism, std::move(parameters));
}

}  // namespace

// ===================================================================
// k(T) against the independent reference -- G6 criterion (a)
// ===================================================================

TEST(ArrheniusTest, RateCoefficientMatchesTheIndependentReference) {
  // The values are mpmath's, at 60 digits, printed at 17 significant figures.
  const ArrheniusParameters parameters(kA, kN, kEa);
  struct Point {
    Real temperature;
    Real expectedExponent;
    Real expectedK;
  };
  const std::vector<Point> points{
      {300.0, -50.113481267802517, 4.4734594277900207e-13},
      {500.0, -30.068088760681510, 0.00029320490484950842},
      {800.0, -18.792555475425944, 29.250487507690091},
      {1200.0, -12.528370316950629, 18822.607500755618},
      {1800.0, -8.3522468779670862, 1501041.7815468797},
      {2500.0, -6.0136177521363021, 18339194.535154295},
      {3000.0, -5.0113481267802517, 54733226.208860598},
  };
  Real worst = 0.0;
  for (const Point& point : points) {
    const Real x = parameters.exponentialArgument(point.temperature);
    EXPECT_NEAR(x, point.expectedExponent, g6(point.expectedExponent))
        << "T = " << point.temperature;
    const Real k = parameters.rateCoefficient(point.temperature);
    EXPECT_NEAR(k, point.expectedK, g6(point.expectedK)) << "T = " << point.temperature;
    worst = std::max(worst, std::abs(k - point.expectedK) / point.expectedK);
  }
  EXPECT_LE(worst, 32.0 * kEps) << "max relative k error = " << worst;
}

TEST(ArrheniusTest, TheRateSpansThirteenOrdersOfMagnitudeOverTheRange) {
  // Non-vacuity for the comparison above: if k barely moved, agreeing with a reference would say
  // little. From 300 K to 3000 K it moves by 1.2e20.
  const ArrheniusParameters parameters(kA, kN, kEa);
  const Real cold = parameters.rateCoefficient(300.0);
  const Real hot = parameters.rateCoefficient(3000.0);
  EXPECT_GT(hot / cold, 1.0e19) << "cold " << cold << " hot " << hot;
}

// ===================================================================
// The reduced analytical cases -- sections 8, 9, 10, 11
// ===================================================================

TEST(ArrheniusTest, ZeroPreExponentialGivesExactlyZeroAtEveryTemperature) {
  // Section 8. EXACTLY zero -- an algebraically exact branch, not a clamp.
  const ArrheniusParameters parameters(0.0, kN, kEa);
  for (const Real t : {1.0, 300.0, 1500.0, 5000.0, 1.0e6}) {
    EXPECT_EQ(parameters.rateCoefficient(t), 0.0) << "T = " << t;
    EXPECT_EQ(parameters.logRateCoefficient(t), -std::numeric_limits<Real>::infinity());
    EXPECT_EQ(parameters.rateCoefficientDerivative(t), 0.0) << "T = " << t;
  }
}

TEST(ArrheniusTest, ZeroActivationEnergyReducesToThePowerLaw) {
  // Section 9: Ea = 0 gives k = A T^n. Reference at T = 1500: 5809475019.3111253
  const ArrheniusParameters parameters(kA, kN, 0.0);
  const Real k = parameters.rateCoefficient(1500.0);
  EXPECT_NEAR(k, 5809475019.3111253, g6(5809475019.3111253));
  // and the exponential term is exactly 1, so the exponent argument is exactly 0
  EXPECT_EQ(parameters.exponentialArgument(1500.0), 0.0);
}

TEST(ArrheniusTest, ZeroTemperatureExponentReducesToTheExponential) {
  // Section 10: n = 0 gives k = A exp(-Ea/(R T)). Reference at T = 1500: 6657.1690027340104
  const ArrheniusParameters parameters(kA, 0.0, kEa);
  EXPECT_NEAR(parameters.rateCoefficient(1500.0), 6657.1690027340104, g6(6657.1690027340104));
}

TEST(ArrheniusTest, FullyReducedCaseIsTemperatureIndependentAndEqualsA) {
  // Section 11: n = 0, Ea = 0 gives k = A, for EVERY T. The strongest simple regression there is.
  // BITWISE under amendment A1: pow(T, 0.0) and exp(0.0) are both exactly 1.0, so A * 1 * 1 == A.
  // Under the original log-space-always form this gave 150000000.00000015 -- a 4.5 eps error,
  // preserved at logs/01_first_failures_PRESERVED.log and the reason for A1.
  const ArrheniusParameters parameters(kA, 0.0, 0.0);
  for (const Real t : {1.0, 273.15, 1500.0, 9000.0}) {
    EXPECT_EQ(parameters.rateCoefficient(t), kA) << "T = " << t;
  }
  // and dk/dT must be exactly 0, since both sensitivity terms vanish
  EXPECT_EQ(parameters.rateCoefficientDerivative(1500.0), 0.0);
}

// ===================================================================
// Temperature sensitivity -- sections 13, 14
// ===================================================================

TEST(ArrheniusTest, RateIncreasesWithTemperatureForPositiveEaAndZeroN) {
  // Section 13: monotone ONLY where it is derived. With n = 0 and Ea > 0,
  // d(ln k)/dT = Ea/(R T^2) > 0 strictly, so k is strictly increasing. For general n it is
  // n/T + Ea/(R T^2), whose sign depends on n -- so monotonicity is NOT asserted there.
  const ArrheniusParameters parameters(kA, 0.0, kEa);
  Real previous = -1.0;
  for (const Real t : {300.0, 600.0, 1000.0, 1500.0, 2500.0, 3500.0}) {
    const Real k = parameters.rateCoefficient(t);
    EXPECT_GT(k, previous) << "T = " << t;
    previous = k;
  }
}

TEST(ArrheniusTest, ANegativeExponentCanMakeTheRateNonMonotone) {
  // The companion to the test above, and the reason monotonicity is not asserted generally:
  // with n sufficiently negative the power law fights the exponential, and
  // d(ln k)/dT = n/T + Ea/(R T^2) changes sign at T* = -Ea/(n R).
  const Real n = -20.0;
  const ArrheniusParameters parameters(kA, n, kEa);
  const Real turning = -kEa / (n * kUniversalGasConstant);
  EXPECT_GT(turning, 300.0) << "the turning point must be inside the tested range";
  // below the turning point the derivative is positive, above it negative
  EXPECT_GT(parameters.rateCoefficientDerivative(turning * 0.5), 0.0);
  EXPECT_LT(parameters.rateCoefficientDerivative(turning * 2.0), 0.0);
  // and it is ~zero exactly at the turning point
  EXPECT_LT(
      std::abs(parameters.rateCoefficientDerivative(turning)) / parameters.rateCoefficient(turning),
      1.0e-14);
}

TEST(ArrheniusTest, AnalyticalDerivativeMatchesACentralFiniteDifference) {
  // Section 14: dk/dT = k[n/T + Ea/(R T^2)] from the closed form, against a CENTRAL finite
  // difference of the implementation -- two independent routes to the same number.
  //
  // THE STEP AND THE TOLERANCE ARE BOTH DERIVED, and the first attempt at them FAILED -- the
  // measurement is preserved at logs/01_first_failures_PRESERVED.log, where h = T*1e-5 with a
  // 1e-9 bound gave 8.45e-9 at T = 600.
  //
  // A central difference balances O(h^2) truncation against O(eps/h) cancellation, and its floor
  // is the textbook eps^(2/3) = 3.667e-11. Scanning h outside CFDApp confirmed the optimum is at
  // h = T * 1e-6, giving 3.8e-11 (T=600), 7.5e-11 (T=1500) and 1.27e-10 (T=2400) -- i.e. 1 to 3.5
  // times eps^(2/3), the remaining factor being the problem-dependent constant on k'''.
  //
  // The bound is therefore 8 * eps^(2/3): the derived floor times the margin factor this project
  // already uses (P17.8's, cited by G3). NOT the measured 1.27e-10, which would be fitting a
  // constant to an observation.
  const ArrheniusParameters parameters(kA, kN, kEa);
  const Real fdFloor = std::pow(kEps, 2.0 / 3.0);
  const Real fdBound = 8.0 * fdFloor;
  Real worst = 0.0;
  for (const Real t : {600.0, 1500.0, 2400.0}) {
    const Real h = t * 1.0e-6;
    const Real numerical =
        (parameters.rateCoefficient(t + h) - parameters.rateCoefficient(t - h)) / (2.0 * h);
    const Real analytical = parameters.rateCoefficientDerivative(t);
    const Real relative = std::abs(numerical - analytical) / std::abs(analytical);
    EXPECT_LT(relative, fdBound) << "T = " << t << " analytical " << analytical << " numerical "
                                 << numerical;
    worst = std::max(worst, relative);
  }
  EXPECT_LT(worst, fdBound) << "max relative dk/dT error = " << worst << " against the derived "
                            << "bound 8*eps^(2/3) = " << fdBound;
}

TEST(ArrheniusTest, AnalyticalDerivativeMatchesTheIndependentReference) {
  // The same derivative, against mpmath rather than against a finite difference.
  const ArrheniusParameters parameters(kA, kN, kEa);
  struct Point {
    Real temperature;
    Real expectedK;
    Real expectedDerivative;
  };
  const std::vector<Point> points{
      {600.0, 0.048212837547266329, 0.0020536049740331695},
      {1500.0, 257831.04680477060, 1808.7185268427616},
      {2400.0, 13986074.328896392, 39418.498500355654},
  };
  for (const Point& point : points) {
    EXPECT_NEAR(parameters.rateCoefficient(point.temperature), point.expectedK,
                g6(point.expectedK));
    EXPECT_NEAR(parameters.rateCoefficientDerivative(point.temperature), point.expectedDerivative,
                g6(point.expectedDerivative))
        << "T = " << point.temperature;
  }
}

// ===================================================================
// The Arrhenius plot -- section 35, the canonical validation
// ===================================================================

TEST(ArrheniusTest, LogLinearPlotHasTheAnalyticalSlopeAndIntercept) {
  // Section 35. With n = 0, ln k = ln A - (Ea/R_u)(1/T), so a least-squares fit of ln k against
  // 1/T must recover slope = -Ea/R_u and intercept = ln A.
  //
  // Reference: slope -15034.044380340755, intercept 18.826145852060530
  const ArrheniusParameters parameters(kA, 0.0, kEa);
  const std::vector<Real> temperatures{800.0, 1000.0, 1250.0, 1600.0, 2000.0};

  // ordinary least squares on (1/T, ln k)
  Real sumX = 0.0;
  Real sumY = 0.0;
  Real sumXX = 0.0;
  Real sumXY = 0.0;
  const Real count = static_cast<Real>(temperatures.size());
  for (const Real t : temperatures) {
    const Real x = 1.0 / t;
    const Real y = std::log(parameters.rateCoefficient(t));
    sumX += x;
    sumY += y;
    sumXX += x * x;
    sumXY += x * y;
  }
  const Real slope = ((count * sumXY) - (sumX * sumY)) / ((count * sumXX) - (sumX * sumX));
  const Real intercept = (sumY - (slope * sumX)) / count;

  const Real expectedSlope = -15034.044380340755;
  const Real expectedIntercept = 18.826145852060530;
  // THE RELATION IS EXACTLY LINEAR, so the fit is not an approximation of a curve -- the only
  // error is round-off in ln and in the normal equations. A few hundred eps is the honest
  // expectation for a 5-point fit whose normal equations involve 1/T^2 ~ 1e-6.
  EXPECT_NEAR(slope, expectedSlope, 1.0e-9 * std::abs(expectedSlope))
      << "slope " << slope << " expected " << expectedSlope;
  EXPECT_NEAR(intercept, expectedIntercept, 1.0e-9 * std::abs(expectedIntercept))
      << "intercept " << intercept << " expected " << expectedIntercept;
  // ...and the analytical slope is -Ea/R_u, independently of the fit
  EXPECT_NEAR(expectedSlope, -kEa / kUniversalGasConstant, g6(expectedSlope));
}

// ===================================================================
// Parameter sensitivities -- sections 15, 16, 17, 34
// ===================================================================

TEST(ArrheniusTest, RateScalesLinearlyWithThePreExponentialFactor) {
  // Section 16: A2 = c A1 => k2 = c k1. Exact up to one rounding, since A enters linearly.
  const Real t = 1400.0;
  const ArrheniusParameters base(kA, kN, kEa);
  for (const Real c : {0.5, 2.0, 1000.0, 1.0e-6}) {
    const ArrheniusParameters scaled(kA * c, kN, kEa);
    const Real ratio = scaled.rateCoefficient(t) / base.rateCoefficient(t);
    EXPECT_NEAR(ratio, c, g6(c)) << "c = " << c;
  }
}

TEST(ArrheniusTest, HigherActivationEnergyReducesTheRateByTheAnalyticalRatio) {
  // Section 15: k2/k1 = exp[-(Ea2-Ea1)/(R T)] for identical A and n. A RATIO test, which
  // eliminates A entirely (section 34).
  const Real t = 1600.0;
  const ArrheniusParameters low(kA, kN, 1.0e5);
  const ArrheniusParameters high(kA, kN, 1.5e5);
  const Real ratio = high.rateCoefficient(t) / low.rateCoefficient(t);
  const Real expected = std::exp(-(1.5e5 - 1.0e5) / (kUniversalGasConstant * t));
  EXPECT_NEAR(ratio, expected, g6(expected));
  EXPECT_LT(ratio, 1.0) << "a higher activation energy must REDUCE the rate";
}

TEST(ArrheniusTest, ExponentSensitivityFollowsThePowerLawRatio) {
  // Section 17: k(n2)/k(n1) = T^(n2-n1), for positive, zero AND negative n.
  const ArrheniusParameters reference(kA, 0.0, kEa);
  for (const Real t : {700.0, 1500.0, 2600.0}) {
    for (const Real n : {2.0, 1.0, 0.5, 0.0, -1.0, -3.5}) {
      const ArrheniusParameters parameters(kA, n, kEa);
      const Real ratio = parameters.rateCoefficient(t) / reference.rateCoefficient(t);
      const Real expected = std::pow(t, n);
      EXPECT_NEAR(ratio, expected, g6(expected) * 4.0) << "T = " << t << " n = " << n;
    }
  }
}

// ===================================================================
// Parameter and state validity -- sections 3, 4, 24, 31
// ===================================================================

TEST(ArrheniusTest, RejectsInvalidParameters) {
  const Real nan = std::numeric_limits<Real>::quiet_NaN();
  const Real inf = std::numeric_limits<Real>::infinity();
  // A
  EXPECT_THROW(ArrheniusParameters(-1.0, kN, kEa), InvalidArgumentError);
  EXPECT_THROW(ArrheniusParameters(nan, kN, kEa), InvalidArgumentError);
  EXPECT_THROW(ArrheniusParameters(inf, kN, kEa), InvalidArgumentError);
  // n
  EXPECT_THROW(ArrheniusParameters(kA, nan, kEa), InvalidArgumentError);
  EXPECT_THROW(ArrheniusParameters(kA, inf, kEa), InvalidArgumentError);
  // Ea
  EXPECT_THROW(ArrheniusParameters(kA, kN, nan), InvalidArgumentError);
  EXPECT_THROW(ArrheniusParameters(kA, kN, inf), InvalidArgumentError);
  // A NEGATIVE Ea IS REJECTED, and that is a documented model decision, not an oversight --
  // formulation.md section 6.
  EXPECT_THROW(ArrheniusParameters(kA, kN, -1.0), InvalidArgumentError);
  // ...while A = 0 and a NEGATIVE n are both ACCEPTED, and ordinary
  EXPECT_NO_THROW(ArrheniusParameters(0.0, kN, kEa));
  EXPECT_NO_THROW(ArrheniusParameters(kA, -4.0, kEa));
  EXPECT_NO_THROW(ArrheniusParameters(kA, kN, 0.0));
}

TEST(ArrheniusTest, N10_RejectsInvalidTemperatures) {
  // CONTROL N10 and section 4: checked BEFORE any division or exp(), never inferred from their
  // output.
  const ArrheniusParameters parameters(kA, kN, kEa);
  for (const Real bad : {0.0, -1.0, -300.0, std::numeric_limits<Real>::quiet_NaN(),
                         std::numeric_limits<Real>::infinity()}) {
    EXPECT_THROW(static_cast<void>(parameters.rateCoefficient(bad)), InvalidArgumentError)
        << "T = " << bad;
    EXPECT_THROW(static_cast<void>(parameters.logRateCoefficient(bad)), InvalidArgumentError);
    EXPECT_THROW(static_cast<void>(parameters.exponentialArgument(bad)), InvalidArgumentError);
  }
}

TEST(ArrheniusTest, OverflowIsRefusedAndUnderflowIsAccepted) {
  // Section 31. An overflow is refused BY NAME rather than returned as inf; an underflow to
  // exactly 0 is accepted and documented, because a rate that small is physically negligible and
  // refusing it would make an unreachable reaction an error instead of a non-event.
  const ArrheniusParameters huge(1.0e300, 20.0, 0.0);
  EXPECT_THROW(static_cast<void>(huge.rateCoefficient(1.0e6)), NumericalError);

  const ArrheniusParameters tiny(1.0e-300, 0.0, 1.0e6);
  const Real k = tiny.rateCoefficient(200.0);
  EXPECT_EQ(k, 0.0) << "an underflow must give exactly 0, not a denormal surprise";
  EXPECT_TRUE(std::isfinite(k));
}

// ===================================================================
// Concentrations and mass-action progress rates -- sections 19, 20, 23
// ===================================================================

TEST(ArrheniusKineticsTest, ConcentrationsReuseTheAuthoritativeStateAndWeights) {
  // Section 19: C_i = rho Y_i / M_i, with every input reused.
  const Mesh mesh = MeshGeometry::createCartesian2D(2, 2, 1.0, 1.0);
  const Index n = mesh.numberOfCells();
  const MassFractionState state(h2Registry(), n, {0.10, 0.80, 1.0 - 0.10 - 0.80});
  const ScalarField density(n, 0.5);
  const auto concentrations = molarConcentrations(h2Mixture(), state, density);

  ASSERT_EQ(concentrations.size(), 3u);
  for (Index cell = 0; cell < n; ++cell) {
    EXPECT_DOUBLE_EQ(concentrations[0][cell], 0.5 * 0.10 / kMH2);
    EXPECT_DOUBLE_EQ(concentrations[1][cell], 0.5 * 0.80 / kMO2);
  }
}

TEST(ArrheniusKineticsTest, N7_ProgressRateDependsOnConcentration) {
  // CONTROL N7: omitting the concentration dependence would make q = k, independent of
  // composition. Here doubling the density doubles every concentration, and with total reactant
  // order 3 the rate must rise by 2^3 = 8.
  const Mesh mesh = MeshGeometry::createCartesian2D(2, 2, 1.0, 1.0);
  const Index n = mesh.numberOfCells();
  const auto mixture = h2Mixture();
  const auto mechanism = h2Mechanism();
  const auto kinetics = h2Kinetics(mechanism);
  const MassFractionState state(h2Registry(), n, {0.10, 0.80, 1.0 - 0.10 - 0.80});
  const ScalarField temperature(n, 1500.0);

  const auto low =
      arrheniusProgressRates(mechanism, kinetics, mixture, state, ScalarField(n, 0.5), temperature);
  const auto high =
      arrheniusProgressRates(mechanism, kinetics, mixture, state, ScalarField(n, 1.0), temperature);
  EXPECT_NEAR(high.perReaction[0][0] / low.perReaction[0][0], 8.0, 1.0e-12)
      << "total reactant order is 3, so doubling every concentration must give 2^3";
  EXPECT_GT(low.perReaction[0][0], 0.0);
}

TEST(ArrheniusKineticsTest, ZeroReactantGivesExactlyZeroProgress) {
  // Section 23: a zero reactant concentration with a positive order gives q = 0 EXACTLY, and
  // pow() is never reached. The reference confirms q = 0 for the no-H2 composition.
  const Mesh mesh = MeshGeometry::createCartesian2D(2, 2, 1.0, 1.0);
  const Index n = mesh.numberOfCells();
  const MassFractionState noH2(h2Registry(), n, {0.0, 0.50, 0.50});
  const auto mechanism = h2Mechanism();
  const auto rates = arrheniusProgressRates(mechanism, h2Kinetics(mechanism), h2Mixture(), noH2,
                                            ScalarField(n, 1.0), ScalarField(n, 1500.0));
  for (Index cell = 0; cell < n; ++cell) {
    EXPECT_EQ(rates.perReaction[0][cell], 0.0) << "cell " << cell;
  }
}

TEST(ArrheniusKineticsTest, RejectsANegativeOrNonFiniteDensity) {
  // Section 24: no fractional power of a negative concentration is evaluated.
  const Mesh mesh = MeshGeometry::createCartesian2D(2, 2, 1.0, 1.0);
  const Index n = mesh.numberOfCells();
  const MassFractionState state(h2Registry(), n, {0.10, 0.80, 1.0 - 0.10 - 0.80});
  const auto mixture = h2Mixture();
  ScalarField negative(n, 1.0);
  negative[2] = -1.0;
  EXPECT_THROW(molarConcentrations(mixture, state, negative), InvalidArgumentError);
  ScalarField nan(n, 1.0);
  nan[1] = std::numeric_limits<Real>::quiet_NaN();
  EXPECT_THROW(molarConcentrations(mixture, state, nan), InvalidArgumentError);
}

// ===================================================================
// THE G6 MATRIX -- omega_i over T, p and composition, through P18.4
// ===================================================================

TEST(ArrheniusKineticsTest, G6Matrix_OmegaMatchesTheIndependentReference) {
  // G6's `measured` clause, implemented literally: omega_i for a known mechanism against the
  // independent mpmath implementation, over a matrix of T, p and composition.
  //
  // THE FULL MATRIX: 4 compositions x 3 pressures x 3 temperatures = 36 cases. The table is
  // GENERATED by tools/emit_cpp_cases.py from the same mpmath module, so not one value is
  // transcribed by hand. The first version of this test carried 9 hand-typed rows covering ONE
  // composition, which is not the composition matrix G6 asks for; the generator reproduced those
  // 9 exactly, which is how the hand transcription was confirmed before it was replaced.
  //
  // THE SOURCES COME FROM P18.4, NOT FROM P18.5 (section 25). P18.5 produces q; the conversion
  // q -> omega_i = sum_r M_i nu_i q_r is reactionSpeciesSources()'s, unchanged.
  const Mesh mesh = MeshGeometry::createCartesian2D(1, 1, 1.0, 1.0);
  const Index n = mesh.numberOfCells();
  const auto mixture = h2Mixture();
  const auto mechanism = h2Mechanism();
  const auto kinetics = h2Kinetics(mechanism);

  struct Case {
    Real yH2;
    Real yO2;
    Real pressure;
    Real temperature;
    Real rho;
    Real q;
    Real omegaH2;
    Real omegaO2;
    Real omegaH2O;
    // C_H2 = 0 makes q = 0 and every omega_i = 0 EXACTLY. A RELATIVE error is undefined there, so
    // those rows are checked for exact equality with 0 instead -- a sharper criterion, not a
    // weaker one.
    bool exactlyZero;
  };
  // GENERATED by tools/emit_cpp_cases.py from tools/arrhenius_reference.py
  // (mpmath, dps=60, printed at 17 significant figures). DO NOT EDIT BY HAND.
  // 4 compositions x 3 pressures x 3 temperatures = 36 cases.
  const std::vector<Case> cases{
      // stoich-ish  p = 50000 Pa  T = 800 K
      {0.10000000000000000, 0.80000000000000000, 50000.000000000000, 800.00000000000000,
       0.093780296181656849, 1484.0687017743407, -5.9837650055541417, -47.487230319375353,
       53.470995324929495, false},
      // stoich-ish  p = 50000 Pa  T = 1500 K
      {0.10000000000000000, 0.80000000000000000, 50000.000000000000, 1500.0000000000000,
       0.050016157963550320, 1984505.4370535873, -8001.5259222000641, -63500.204974840687,
       71501.730897040751, false},
      // stoich-ish  p = 50000 Pa  T = 2200 K
      {0.10000000000000000, 0.80000000000000000, 50000.000000000000, 2200.0000000000000,
       0.034101925884238854, 18484431.170037401, -74529.226477590801, -591464.82857885676,
       665994.05505644756, false},
      // stoich-ish  p = 101325 Pa  T = 800 K
      {0.10000000000000000, 0.80000000000000000, 101325.00000000000, 800.00000000000000,
       0.19004577021212760, 12350.764202795906, -49.798281265673094, -395.19975296106341,
       444.99803422673650, false},
      // stoich-ish  p = 101325 Pa  T = 1500 K
      {0.10000000000000000, 0.80000000000000000, 101325.00000000000, 1500.0000000000000,
       0.10135774411313472, 16515514.869972759, -66590.555955730163, -528463.44480938833,
       595054.00076511849, false},
      // stoich-ish  p = 101325 Pa  T = 2200 K
      {0.10000000000000000, 0.80000000000000000, 101325.00000000000, 2200.0000000000000,
       0.069107552804410038, 153831726.61143847, -620249.52169731990, -4922307.5881128081,
       5542557.1098101280, false},
      // stoich-ish  p = 1000000 Pa  T = 800 K
      {0.10000000000000000, 0.80000000000000000, 1000000.0000000000, 800.00000000000000,
       1.8756059236331370, 11872549.614194726, -47870.120044433133, -379897.84255500283,
       427767.96259943596, false},
      // stoich-ish  p = 1000000 Pa  T = 1500 K
      {0.10000000000000000, 0.80000000000000000, 1000000.0000000000, 1500.0000000000000,
       1.0003231592710064, 15876043496.428699, -64012207.377600512, -508001639.79872550,
       572013847.17632601, false},
      // stoich-ish  p = 1000000 Pa  T = 2200 K
      {0.10000000000000000, 0.80000000000000000, 1000000.0000000000, 2200.0000000000000,
       0.68203851768477708, 147875449360.29921, -596233811.82072641, -4731718628.6308541,
       5327952440.4515805, false},
      // H2-rich  p = 50000 Pa  T = 800 K
      {0.60000000000000000, 0.30000000000000000, 50000.000000000000, 800.00000000000000,
       0.024050964310689955, 337.94844789353249, -1.3626081419067230, -10.813674435697253,
       12.176282577603976, false},
      // H2-rich  p = 50000 Pa  T = 1500 K
      {0.60000000000000000, 0.30000000000000000, 50000.000000000000, 1500.0000000000000,
       0.012827180965701309, 451906.66138750841, -1822.0876587144339, -14460.109351077494,
       16282.197009791928, false},
      // H2-rich  p = 50000 Pa  T = 2200 K
      {0.60000000000000000, 0.30000000000000000, 50000.000000000000, 2200.0000000000000,
       0.0087458052038872564, 4209228.8696880181, -16971.610802582089, -134686.90537227720,
       151658.51617485929, false},
      // H2-rich  p = 101325 Pa  T = 800 K
      {0.60000000000000000, 0.30000000000000000, 101325.00000000000, 800.00000000000000,
       0.048739279175613194, 2812.4854244574873, -11.339941231412589, -89.993908611790679,
       101.33384984320327, false},
      // H2-rich  p = 101325 Pa  T = 1500 K
      {0.60000000000000000, 0.30000000000000000, 101325.00000000000, 1500.0000000000000,
       0.025994282226993704, 3760872.1279525546, -15163.836419904700, -120340.38635022584,
       135504.22277013054, false},
      // H2-rich  p = 101325 Pa  T = 2200 K
      {0.60000000000000000, 0.30000000000000000, 101325.00000000000, 2200.0000000000000,
       0.017723374245677525, 35030179.656078169, -141241.68437330718, -1120895.6886351893,
       1262137.3730084964, false},
      // H2-rich  p = 1000000 Pa  T = 800 K
      {0.60000000000000000, 0.30000000000000000, 1000000.0000000000, 800.00000000000000,
       0.48101928621379910, 2703587.5831482599, -10900.865135253784, -86509.395485578022,
       97410.260620831806, false},
      // H2-rich  p = 1000000 Pa  T = 1500 K
      {0.60000000000000000, 0.30000000000000000, 1000000.0000000000, 1500.0000000000000,
       0.25654361931402619, 3615253291.1000673, -14576701.269715471, -115680874.80861995,
       130257576.07833542, false},
      // H2-rich  p = 1000000 Pa  T = 2200 K
      {0.60000000000000000, 0.30000000000000000, 1000000.0000000000, 2200.0000000000000,
       0.17491610407774513, 33673830957.504145, -135772886.42065671, -1077495242.9782176,
       1213268129.3988743, false},
      // O2-rich  p = 50000 Pa  T = 800 K
      {0.050000000000000000, 0.90000000000000000, 50000.000000000000, 800.00000000000000,
       0.13494628419029918, 1243.6395612429125, -5.0143547109314232, -39.793978680650714,
       44.808333391582137, false},
      // O2-rich  p = 50000 Pa  T = 1500 K
      {0.050000000000000000, 0.90000000000000000, 50000.000000000000, 1500.0000000000000,
       0.071971351568159564, 1663002.1696911775, -6705.2247481948277, -53212.743425778298,
       59917.968173973126, false},
      // O2-rich  p = 50000 Pa  T = 2200 K
      {0.050000000000000000, 0.90000000000000000, 50000.000000000000, 2200.0000000000000,
       0.049071376069199703, 15489828.633031541, -62454.989048383173, -495643.53659974325,
       558098.52564812642, false},
      // O2-rich  p = 101325 Pa  T = 800 K
      {0.050000000000000000, 0.90000000000000000, 101325.00000000000, 800.00000000000000,
       0.27346864491164129, 10349.857089375713, -41.730623784362875, -331.17472714584407,
       372.90535093020694, false},
      // O2-rich  p = 101325 Pa  T = 1500 K
      {0.050000000000000000, 0.90000000000000000, 101325.00000000000, 1500.0000000000000,
       0.14584994395287536, 13839890.054979960, -55802.436701679201, -442848.80197924878,
       498651.23868092798, false},
      // O2-rich  p = 101325 Pa  T = 2200 K
      {0.050000000000000000, 0.90000000000000000, 101325.00000000000, 2200.0000000000000,
       0.099443143604233198, 128909949.22240381, -519764.91526473217, -4124860.5552184771,
       4644625.4704832093, false},
      // O2-rich  p = 1000000 Pa  T = 800 K
      {0.050000000000000000, 0.90000000000000000, 1000000.0000000000, 800.00000000000000,
       2.6989256838059837, 9949116.4899433000, -40114.837687451386, -318351.82944520571,
       358466.66713265710, false},
      // O2-rich  p = 1000000 Pa  T = 1500 K
      {0.050000000000000000, 0.90000000000000000, 1000000.0000000000, 1500.0000000000000,
       1.4394270313631913, 13304017357.529420, -53641797.985558622, -425701947.40622639,
       479343745.39178501, false},
      // O2-rich  p = 1000000 Pa  T = 2200 K
      {0.050000000000000000, 0.90000000000000000, 1000000.0000000000, 2200.0000000000000,
       0.98142752138399406, 123918629064.25233, -499639912.38706538, -3965148292.7979460,
       4464788205.1850114, false},
      // no-H2  p = 50000 Pa  T = 800 K
      {0.0, 0.50000000000000000, 50000.000000000000, 800.00000000000000, 0.17328063159098264, 0.0,
       0.0, 0.0, 0.0, true},
      // no-H2  p = 50000 Pa  T = 1500 K
      {0.0, 0.50000000000000000, 50000.000000000000, 1500.0000000000000, 0.092416336848524076, 0.0,
       0.0, 0.0, 0.0, true},
      // no-H2  p = 50000 Pa  T = 2200 K
      {0.0, 0.50000000000000000, 50000.000000000000, 2200.0000000000000, 0.063011138760357324, 0.0,
       0.0, 0.0, 0.0, true},
      // no-H2  p = 101325 Pa  T = 800 K
      {0.0, 0.50000000000000000, 101325.00000000000, 800.00000000000000, 0.35115319991912632, 0.0,
       0.0, 0.0, 0.0, true},
      // no-H2  p = 101325 Pa  T = 1500 K
      {0.0, 0.50000000000000000, 101325.00000000000, 1500.0000000000000, 0.18728170662353404, 0.0,
       0.0, 0.0, 0.0, true},
      // no-H2  p = 101325 Pa  T = 2200 K
      {0.0, 0.50000000000000000, 101325.00000000000, 2200.0000000000000, 0.12769207269786412, 0.0,
       0.0, 0.0, 0.0, true},
      // no-H2  p = 1000000 Pa  T = 800 K
      {0.0, 0.50000000000000000, 1000000.0000000000, 800.00000000000000, 3.4656126318196528, 0.0,
       0.0, 0.0, 0.0, true},
      // no-H2  p = 1000000 Pa  T = 1500 K
      {0.0, 0.50000000000000000, 1000000.0000000000, 1500.0000000000000, 1.8483267369704815, 0.0,
       0.0, 0.0, 0.0, true},
      // no-H2  p = 1000000 Pa  T = 2200 K
      {0.0, 0.50000000000000000, 1000000.0000000000, 2200.0000000000000, 1.2602227752071465, 0.0,
       0.0, 0.0, 0.0, true},
  };

  ASSERT_EQ(cases.size(), 36U) << "the generated matrix must be complete";

  Real worstRho = 0.0;
  Real worstQ = 0.0;
  Real worstOmega = 0.0;
  Real worstSum = 0.0;

  Index zeroCases = 0;
  Index nonZeroCases = 0;

  for (const Case& c : cases) {
    const MassFractionState state(h2Registry(), n, {c.yH2, c.yO2, 1.0 - c.yH2 - c.yO2});
    // rho from the mixture EOS -- P18.3's, so the chain is the production one end to end
    const Real rho = mixture.density(state.cellComposition(0), c.pressure, c.temperature);
    EXPECT_NEAR(rho, c.rho, g6(c.rho)) << "p = " << c.pressure << " T = " << c.temperature;
    worstRho = std::max(worstRho, std::abs(rho - c.rho) / c.rho);

    const ScalarField density(n, rho);
    const ScalarField temperature(n, c.temperature);
    const auto rates =
        arrheniusProgressRates(mechanism, kinetics, mixture, state, density, temperature);
    // omega_i THROUGH P18.4
    const auto sources = reactionSpeciesSources(mechanism, mixture, rates, n);
    const Real expected[3] = {c.omegaH2, c.omegaO2, c.omegaH2O};
    const auto conservation = sourceConservation(sources, n);

    if (c.exactlyZero) {
      ++zeroCases;
      EXPECT_EQ(rates.perReaction[0][0], 0.0)
          << "C_H2 = 0 must give q = 0 EXACTLY, p = " << c.pressure << " T = " << c.temperature;
      for (Index i = 0; i < 3; ++i) {
        EXPECT_EQ(sources.perSpecies[i].Su[0], 0.0) << "species " << i << " must be exactly 0";
      }
      EXPECT_EQ(conservation.maxAbsoluteSum, 0.0);
      EXPECT_EQ(conservation.maxAbsoluteSource, 0.0);
      continue;
    }

    ++nonZeroCases;
    EXPECT_NEAR(rates.perReaction[0][0], c.q, g6(c.q))
        << "p = " << c.pressure << " T = " << c.temperature;
    worstQ = std::max(worstQ, std::abs(rates.perReaction[0][0] - c.q) / c.q);

    for (Index i = 0; i < 3; ++i) {
      EXPECT_NEAR(sources.perSpecies[i].Su[0], expected[i], g6(expected[i]))
          << "species " << i << " p = " << c.pressure << " T = " << c.temperature;
      worstOmega = std::max(
          worstOmega, std::abs(sources.perSpecies[i].Su[0] - expected[i]) / std::abs(expected[i]));
    }
    // mass conservation, via P18.4's own diagnostic -- the formula is NOT duplicated here
    const Real relative = conservation.maxAbsoluteSum / conservation.maxAbsoluteSource;
    EXPECT_LE(relative, 32.0 * kEps) << "sum omega at p = " << c.pressure;
    worstSum = std::max(worstSum, relative);
  }

  // NEITHER BRANCH MAY SWALLOW THE MATRIX. Without this, an implementation returning 0 everywhere
  // would take the exact-zero branch for all 36 rows and the relative bounds below would be
  // vacuous -- they are initialised to 0 and would never be updated.
  EXPECT_EQ(nonZeroCases, 27) << "27 rows must carry a non-zero rate";
  EXPECT_EQ(zeroCases, 9) << "9 rows have C_H2 = 0";

  EXPECT_LE(worstRho, 32.0 * kEps) << "max relative rho error = " << worstRho;
  EXPECT_LE(worstQ, 32.0 * kEps) << "max relative q error = " << worstQ;
  EXPECT_LE(worstOmega, 32.0 * kEps) << "max relative omega error = " << worstOmega;
  EXPECT_LE(worstSum, 32.0 * kEps) << "max relative |sum omega| = " << worstSum;
}

TEST(ArrheniusTest, NV2_TheLogSpaceFallbackIsStillReachedAndCorrect) {
  // NON-VACUITY FOR AMENDMENT A1. A1 selects the direct form when A*T^n is finite -- so the
  // log-space branch it kept could silently become dead code, deleting the robustness the original
  // formulation existed for. This drives A*T^n to OVERFLOW while k itself stays representable, and
  // requires a finite, correct answer.
  //
  //   A = 1e300, n = 40, T = 1000  ->  A T^n = 1e300 * 1e120 = 1e420 = +inf
  //   but with Ea large enough, k = A T^n exp(-Ea/(R_u T)) is finite.
  //
  // ln k = ln(1e300) + 40 ln(1000) - Ea/(R_u * 1000)
  //      = 690.776 + 276.310 - Ea/8314.46
  // choosing Ea = 8.0e6 J/mol gives -Ea/(R_u T) = -962.17, so ln k = 4.92 and k ~ 136.6 --
  // comfortably finite, while the intermediate overflowed.
  const ArrheniusParameters parameters(1.0e300, 40.0, 8.0e6);
  const Real t = 1000.0;

  // the intermediate really does overflow, or this test proves nothing
  EXPECT_FALSE(std::isfinite(1.0e300 * std::pow(t, 40.0)))
      << "A*T^n must overflow here, or the fallback is not being exercised";

  const Real k = parameters.rateCoefficient(t);
  EXPECT_TRUE(std::isfinite(k)) << "the log-space fallback must return a finite k; got " << k;
  EXPECT_GT(k, 0.0);
  // against the closed form evaluated in log space independently
  const Real expectedLog =
      std::log(1.0e300) + (40.0 * std::log(t)) - (8.0e6 / (8.31446261815324 * t));
  EXPECT_NEAR(std::log(k), expectedLog, 32.0 * kEps * std::abs(expectedLog));
  EXPECT_NEAR(k, std::exp(expectedLog), 1.0e-9 * std::exp(expectedLog));
}

TEST(ArrheniusTest, NV1_TheTwoEvaluationFormsAgreeWhereBothAreSafe) {
  // RISK 1 of amendment A1: if the two branches disagreed beyond 32 eps, the branch selection
  // would be observable as a discontinuity in k. Compared across a T sweep and a range of n,
  // in the region where both forms are safe.
  Real worst = 0.0;
  for (const Real n : {-3.5, -1.0, 0.0, 0.5, 2.0}) {
    const ArrheniusParameters parameters(kA, n, kEa);
    for (const Real t : {300.0, 700.0, 1500.0, 2500.0, 3500.0}) {
      const Real direct = parameters.rateCoefficient(t);
      const Real viaLog = std::exp(parameters.logRateCoefficient(t));
      if (direct == 0.0 || viaLog == 0.0) {
        continue;  // both underflowed; nothing to compare
      }
      const Real relative = std::abs(direct - viaLog) / direct;
      EXPECT_LT(relative, 32.0 * kEps) << "n = " << n << " T = " << t;
      worst = std::max(worst, relative);
    }
  }
  EXPECT_LT(worst, 32.0 * kEps) << "max relative disagreement between the two forms = " << worst;
}

// ===================================================================
// Field evaluation -- sections 27, 28, and control N6
// ===================================================================

TEST(ArrheniusKineticsTest, N6_EachCellUsesItsOwnTemperature) {
  // CONTROL N6: "use global T instead of cell T". A single cached rate coefficient for a
  // spatially varying field would make every cell's rate identical.
  const Mesh mesh = MeshGeometry::createCartesian2D(8, 1, 1.0, 1.0);
  const Index n = mesh.numberOfCells();
  const auto mixture = h2Mixture();
  const auto mechanism = h2Mechanism();
  const auto kinetics = h2Kinetics(mechanism);
  // SAME composition everywhere, so temperature is the ONLY thing that varies
  const MassFractionState state(h2Registry(), n, {0.10, 0.80, 1.0 - 0.10 - 0.80});
  const ScalarField density(n, 0.5);

  ScalarField temperature(n);
  for (Index cell = 0; cell < n; ++cell) {
    temperature[cell] = 800.0 + (250.0 * static_cast<Real>(cell));  // 800 .. 2550 K
  }
  const auto rates =
      arrheniusProgressRates(mechanism, kinetics, mixture, state, density, temperature);

  // the rates must span orders of magnitude, and be strictly increasing (Ea > 0, n > 0 here)
  for (Index cell = 1; cell < n; ++cell) {
    EXPECT_GT(rates.perReaction[0][cell], rates.perReaction[0][cell - 1]) << "cell " << cell;
  }
  EXPECT_GT(rates.perReaction[0][n - 1] / rates.perReaction[0][0], 1.0e4)
      << "a cached global rate coefficient would make these equal";

  // and each cell must equal a direct single-point evaluation at ITS OWN temperature
  const ArrheniusParameters parameters(kA, kN, kEa);
  const auto concentrations = molarConcentrations(mixture, state, density);
  for (Index cell = 0; cell < n; ++cell) {
    const Real product = std::pow(concentrations[0][cell], 2.0) * concentrations[1][cell];
    const Real expected = parameters.rateCoefficient(temperature[cell]) * product;
    EXPECT_NEAR(rates.perReaction[0][cell], expected, g6(expected)) << "cell " << cell;
  }
}

TEST(ArrheniusKineticsTest, SpatiallyVaryingCompositionGivesVaryingRates) {
  // Section 28: each cell's chemistry from its own rho, Y_i and T.
  const Mesh mesh = MeshGeometry::createCartesian2D(8, 1, 1.0, 1.0);
  const Index n = mesh.numberOfCells();
  ScalarField h2(n);
  ScalarField o2(n);
  ScalarField h2o(n);
  for (Index cell = 0; cell < n; ++cell) {
    const Real t = static_cast<Real>(cell) / static_cast<Real>(n - 1);
    h2[cell] = 0.02 + (0.30 * t);
    o2[cell] = 0.60 - (0.20 * t);
    h2o[cell] = 1.0 - h2[cell] - o2[cell];  // exact remainder
  }
  const MassFractionState state(h2Registry(), {h2, o2, h2o});
  const auto mechanism = h2Mechanism();
  // UNIFORM temperature, so composition is the only thing that varies
  const auto rates = arrheniusProgressRates(mechanism, h2Kinetics(mechanism), h2Mixture(), state,
                                            ScalarField(n, 0.6), ScalarField(n, 1500.0));
  EXPECT_GT(rates.perReaction[0][n - 1] / rates.perReaction[0][0], 10.0)
      << "the H2 mass fraction rises 15x and the rate is second order in it";
}

// ===================================================================
// The sign, Ea, T^n, A and unit controls -- N1..N5
// ===================================================================

TEST(ArrheniusTest, N1_FlippingTheExponentSignIsDetected) {
  // CONTROL N1, and it is G6's OWN first control: "Arrhenius exponent sign flipped must FAIL".
  //
  // Reference at T = 1500: correct exp(-Ea/(R_u T)) = 4.4381126684893403e-5
  //                        flipped exp(+Ea/(R_u T)) = 22532.100347519644
  //                        ratio                     = 507695546.07069487
  const ArrheniusParameters parameters(kA, kN, kEa);
  const Real t = 1500.0;
  const Real x = parameters.exponentialArgument(t);
  EXPECT_LT(x, 0.0) << "the exponent argument must be NEGATIVE for Ea > 0 and T > 0";
  EXPECT_NEAR(std::exp(x), 4.4381126684893403e-5, g6(4.4381126684893403e-5));

  const Real flipped = std::exp(-x);  // the defect
  EXPECT_NEAR(flipped, 22532.100347519644, g6(22532.100347519644));
  EXPECT_GT(flipped / std::exp(x), 5.0e8) << "half a billion times -- unmistakable";

  // and the diagnostic reports the sign structurally
  const ScalarField temperature(4, t);
  const auto mechanism = h2Mechanism();
  const auto diagnostics = exponentDiagnostics(h2Kinetics(mechanism), 0, temperature);
  EXPECT_FALSE(diagnostics.anyPositive) << "a positive exponent argument would be a sign error";
  EXPECT_LT(diagnostics.mostNegative, 0.0);
}

TEST(ArrheniusTest, N2_OmittingTheActivationEnergyTermIsDetected) {
  // CONTROL N2, and G6's OWN second control: "activation energy set to 0 must FAIL (removes the
  // T dependence entirely)".
  //
  // Note the distinction from the ANALYTICAL Ea = 0 case, which is a legitimate reduced form
  // (ZeroActivationEnergyReducesToThePowerLaw). Here Ea = 0 is a DEFECT in a mechanism whose rate
  // comparison depends on the exponential, and the criterion it breaks is the mpmath match.
  const ArrheniusParameters correct(kA, kN, kEa);
  const ArrheniusParameters defective(kA, kN, 0.0);
  const Real t = 1500.0;
  // the reference value for the correct parameters
  EXPECT_NEAR(correct.rateCoefficient(t), 257831.04680477060, g6(257831.04680477060));
  // the defect is off by 1/exp(-Ea/RT) = 22532x
  const Real ratio = defective.rateCoefficient(t) / correct.rateCoefficient(t);
  EXPECT_NEAR(ratio, 22532.100347519644, 1.0e-6 * 22532.100347519644);
  EXPECT_FALSE(std::abs(defective.rateCoefficient(t) - 257831.04680477060) <=
               g6(257831.04680477060));

  // AND IT REMOVES THE TEMPERATURE SENSITIVITY SIGNATURE: with Ea = 0 the Arrhenius slope is 0.
  const ArrheniusParameters correctZeroN(kA, 0.0, kEa);
  const ArrheniusParameters defectiveZeroN(kA, 0.0, 0.0);
  const Real slopeCorrect = (std::log(correctZeroN.rateCoefficient(2000.0)) -
                             std::log(correctZeroN.rateCoefficient(800.0))) /
                            ((1.0 / 2000.0) - (1.0 / 800.0));
  const Real slopeDefective = (std::log(defectiveZeroN.rateCoefficient(2000.0)) -
                               std::log(defectiveZeroN.rateCoefficient(800.0))) /
                              ((1.0 / 2000.0) - (1.0 / 800.0));
  EXPECT_NEAR(slopeCorrect, -kEa / kUniversalGasConstant, 1.0e-9 * kEa / kUniversalGasConstant);
  EXPECT_EQ(slopeDefective, 0.0) << "Ea = 0 removes the T dependence entirely, as G6 says";
}

TEST(ArrheniusTest, N3_OmittingTheTemperaturePowerLawIsDetected) {
  // CONTROL N3. At T = 1500 with n = 0.5, the T^n factor is sqrt(1500) = 38.73x.
  const ArrheniusParameters correct(kA, kN, kEa);
  const ArrheniusParameters defective(kA, 0.0, kEa);  // n dropped
  const Real t = 1500.0;
  const Real ratio = correct.rateCoefficient(t) / defective.rateCoefficient(t);
  EXPECT_NEAR(ratio, std::sqrt(t), g6(std::sqrt(t)) * 4.0);
  EXPECT_GT(ratio, 38.0);
}

TEST(ArrheniusTest, N4_AKmolGasConstantIsWrongByOrdersOfMagnitude) {
  // CONTROL N4 and sections 5, 33 -- the authorization calls this "one of the most damaging silent
  // chemistry errors".
  //
  // Reference at T = 1000, Ea = 1.25e5:
  //   correct exp(-Ea/(R_u T))   = 2.9566334426841137e-7
  //   with R 1000x too large     = 0.98507840264838826
  //   ratio                      = 3331756.9517650683
  const Real t = 1000.0;
  const Real correct = std::exp(-kEa / (kUniversalGasConstant * t));
  const Real wrong = std::exp(-kEa / (kUniversalGasConstant * 1000.0 * t));
  EXPECT_NEAR(correct, 2.9566334426841137e-7, g6(2.9566334426841137e-7));
  EXPECT_NEAR(wrong, 0.98507840264838826, g6(0.98507840264838826));
  EXPECT_NEAR(wrong / correct, 3331756.9517650683, 1.0e-9 * 3331756.9517650683);

  // and the production path uses the mol-based constant, verifiably
  const ArrheniusParameters parameters(1.0, 0.0, kEa);
  EXPECT_NEAR(parameters.rateCoefficient(t), correct, g6(correct));
  EXPECT_FALSE(std::abs(parameters.rateCoefficient(t) - wrong) <= g6(wrong));
  // R_u is P18.3's, not a local copy
  EXPECT_DOUBLE_EQ(kUniversalGasConstant, 8.31446261815324);
}

TEST(ArrheniusTest, N5_IgnoringThePreExponentialFactorIsDetected) {
  // CONTROL N5: k scales linearly with A, so dropping it (treating A as 1) is off by exactly A.
  const ArrheniusParameters correct(kA, kN, kEa);
  const ArrheniusParameters defective(1.0, kN, kEa);
  const Real t = 1500.0;
  EXPECT_NEAR(correct.rateCoefficient(t) / defective.rateCoefficient(t), kA, g6(kA));
  EXPECT_GT(kA, 1.0e7) << "the margin is A itself, 1.5e8";
}

// ===================================================================
// Reversible kinetics -- section 22
// ===================================================================

namespace {

ReactionMechanism reversibleMechanism() {
  std::vector<Reaction> reactions;
  reactions.push_back(Reaction("2H2+O2<->2H2O", h2Registry(), {{"H2", 2.0}, {"O2", 1.0}},
                               {{"H2O", 2.0}}, ReactionDirection::Reversible));
  return ReactionMechanism(h2Registry(), std::move(reactions), h2Mixture());
}

}  // namespace

TEST(ArrheniusKineticsTest, ReversibleKineticsUseIndependentlySuppliedReverseParameters) {
  // Section 22: forward and reverse Arrhenius sets are supplied INDEPENDENTLY. They are NOT
  // derived from an equilibrium constant -- P18.5 has none, and section 20 forbids inventing one.
  // A caller can therefore specify a thermodynamically inconsistent pair and nothing detects it;
  // that is a stated limitation, not an oversight.
  const Mesh mesh = MeshGeometry::createCartesian2D(1, 1, 1.0, 1.0);
  const Index n = mesh.numberOfCells();
  const auto mixture = h2Mixture();
  const auto mechanism = reversibleMechanism();
  const MassFractionState state(h2Registry(), n, {0.10, 0.40, 1.0 - 0.10 - 0.40});
  const ScalarField density(n, 1.0);
  const ScalarField temperature(n, 1500.0);
  const auto concentrations = molarConcentrations(mixture, state, density);

  auto rateFor = [&](Real aReverse) {
    std::vector<ReactionRateParameters> parameters;
    parameters.push_back({"2H2+O2<->2H2O", ArrheniusParameters(kA, kN, kEa),
                          ArrheniusParameters(aReverse, kN, kEa)});
    const ReactionKinetics kinetics(mechanism, std::move(parameters));
    return arrheniusProgressRates(mechanism, kinetics, mixture, state, density, temperature)
        .perReaction[0][0];
  };

  // the forward and reverse concentration products, computed here independently
  const Real forwardProduct =
      std::pow(concentrations[0][0], 2.0) * concentrations[1][0];   // [H2]^2 [O2]
  const Real reverseProduct = std::pow(concentrations[2][0], 2.0);  // [H2O]^2
  const ArrheniusParameters forwardParameters(kA, kN, kEa);
  const Real kForward = forwardParameters.rateCoefficient(1500.0);
  const Real qForward = kForward * forwardProduct;

  // a tiny reverse rate: net forward
  EXPECT_GT(rateFor(1.0), 0.0);
  // a huge reverse rate: net REVERSE
  EXPECT_LT(rateFor(1.0e14), 0.0);

  // THE BALANCE POINT, solved analytically: q_net = 0 when
  //     k_r = k_f * forwardProduct / reverseProduct
  // and since k scales linearly with A at fixed n, Ea, that is A_r = kA * ratio.
  const Real balancedA = kA * forwardProduct / reverseProduct;
  const Real netAtBalance = rateFor(balancedA);
  EXPECT_LT(std::abs(netAtBalance) / qForward, 32.0 * kEps)
      << "q_net should vanish at the analytically derived reverse A; net = " << netAtBalance
      << " against q_forward = " << qForward;
}

TEST(ArrheniusKineticsTest, RejectsMismatchedDirectionParameters) {
  const auto irreversible = h2Mechanism();
  const auto reversible = reversibleMechanism();
  // reverse parameters on an IRREVERSIBLE reaction
  std::vector<ReactionRateParameters> withReverse;
  withReverse.push_back(
      {"2H2+O2->2H2O", ArrheniusParameters(kA, kN, kEa), ArrheniusParameters(kA, kN, kEa)});
  EXPECT_THROW(ReactionKinetics(irreversible, std::move(withReverse)), InvalidArgumentError);
  // MISSING reverse parameters on a REVERSIBLE one
  std::vector<ReactionRateParameters> withoutReverse;
  withoutReverse.push_back({"2H2+O2<->2H2O", ArrheniusParameters(kA, kN, kEa), std::nullopt});
  EXPECT_THROW(ReactionKinetics(reversible, std::move(withoutReverse)), InvalidArgumentError);
}

// ===================================================================
// Multi-reaction binding -- sections 36, 37, controls N8, N9
// ===================================================================

namespace {

// Two reactions over a 4-species registry with EQUAL molecular weights, so 1:1 conversions
// conserve mass.
SpeciesRegistry chainRegistry() { return SpeciesRegistry({"A", "B", "C", "Inert"}); }

MixtureThermodynamics chainMixture() {
  std::vector<SpeciesThermodynamics> species;
  for (const char* name : {"cp_A", "cp_B", "cp_C", "cp_Inert"}) {
    species.push_back(SpeciesThermodynamics(0.0625, cp(1000.0, name)));
  }
  return MixtureThermodynamics(chainRegistry(), std::move(species));
}

ReactionMechanism chainMechanism() {
  std::vector<Reaction> reactions;
  reactions.push_back(Reaction("A->B", chainRegistry(), {{"A", 1.0}}, {{"B", 1.0}}));
  reactions.push_back(Reaction("B->C", chainRegistry(), {{"B", 1.0}}, {{"C", 1.0}}));
  return ReactionMechanism(chainRegistry(), std::move(reactions), chainMixture());
}

}  // namespace

TEST(ArrheniusKineticsTest, N8_KineticsBindToReactionsByNameNotByPosition) {
  // CONTROL N8: "map kinetics to wrong reaction". Binding by POSITION would silently misattach
  // every parameter set if the supplied order differed from the mechanism's.
  //
  // Here the two reactions get DELIBERATELY DIFFERENT activation energies, and the parameters are
  // supplied in REVERSE order. Name binding must still attach each set to its own reaction.
  const Mesh mesh = MeshGeometry::createCartesian2D(1, 1, 1.0, 1.0);
  const Index n = mesh.numberOfCells();
  const auto mixture = chainMixture();
  const auto mechanism = chainMechanism();
  const Real eaLow = 5.0e4;
  const Real eaHigh = 1.5e5;

  std::vector<ReactionRateParameters> reversed;
  reversed.push_back({"B->C", ArrheniusParameters(kA, 0.0, eaHigh), std::nullopt});
  reversed.push_back({"A->B", ArrheniusParameters(kA, 0.0, eaLow), std::nullopt});
  const ReactionKinetics kinetics(mechanism, std::move(reversed));

  // reaction 0 is A->B and must have the LOW Ea despite being supplied second
  EXPECT_DOUBLE_EQ(kinetics.forward(0).activationEnergy(), eaLow);
  EXPECT_DOUBLE_EQ(kinetics.forward(1).activationEnergy(), eaHigh);

  // and the rates differ accordingly: the low-Ea reaction is much faster
  const MassFractionState state(chainRegistry(), n, {0.25, 0.25, 0.25, 0.25});
  const auto rates = arrheniusProgressRates(mechanism, kinetics, mixture, state,
                                            ScalarField(n, 1.0), ScalarField(n, 1000.0));
  EXPECT_GT(rates.perReaction[0][0], rates.perReaction[1][0])
      << "A->B has the lower activation energy and must be faster";
}

TEST(ArrheniusKineticsTest, RejectsMisnamedMissingAndDuplicateParameterSets) {
  const auto mechanism = chainMechanism();
  // a set naming a reaction the mechanism does not hold
  std::vector<ReactionRateParameters> misnamed;
  misnamed.push_back({"A->B", ArrheniusParameters(kA, 0.0, kEa), std::nullopt});
  misnamed.push_back({"X->Y", ArrheniusParameters(kA, 0.0, kEa), std::nullopt});
  EXPECT_THROW(ReactionKinetics(mechanism, std::move(misnamed)), InvalidArgumentError);
  // the wrong count
  std::vector<ReactionRateParameters> tooFew;
  tooFew.push_back({"A->B", ArrheniusParameters(kA, 0.0, kEa), std::nullopt});
  EXPECT_THROW(ReactionKinetics(mechanism, std::move(tooFew)), InvalidArgumentError);
  // two sets for one reaction
  std::vector<ReactionRateParameters> duplicated;
  duplicated.push_back({"A->B", ArrheniusParameters(kA, 0.0, kEa), std::nullopt});
  duplicated.push_back({"A->B", ArrheniusParameters(kA, 0.0, kEa), std::nullopt});
  EXPECT_THROW(ReactionKinetics(mechanism, std::move(duplicated)), InvalidArgumentError);
}

TEST(ArrheniusKineticsTest, N9_SourcesComeFromP18_4AndMassIsConservedByItsOwnDiagnostic) {
  // CONTROL N9: "bypass P18.4 source conversion". P18.5 produces NO omega_i -- the only way to
  // get one is through reactionSpeciesSources(), and this asserts that path end to end plus
  // P18.4's own conservation diagnostic. The mass-conservation formula is NOT duplicated here
  // (section 26).
  const Mesh mesh = MeshGeometry::createCartesian2D(4, 4, 1.0, 1.0);
  const Index n = mesh.numberOfCells();
  const auto mixture = chainMixture();
  const auto mechanism = chainMechanism();
  std::vector<ReactionRateParameters> parameters;
  parameters.push_back({"A->B", ArrheniusParameters(kA, 0.0, 5.0e4), std::nullopt});
  parameters.push_back({"B->C", ArrheniusParameters(kA, 0.0, 8.0e4), std::nullopt});
  const ReactionKinetics kinetics(mechanism, std::move(parameters));

  const MassFractionState state(chainRegistry(), n, {0.40, 0.30, 0.20, 0.10});
  ScalarField temperature(n);
  for (Index cell = 0; cell < n; ++cell) {
    temperature[cell] = 900.0 + (60.0 * static_cast<Real>(cell));
  }
  const auto rates =
      arrheniusProgressRates(mechanism, kinetics, mixture, state, ScalarField(n, 1.0), temperature);
  const auto sources = reactionSpeciesSources(mechanism, mixture, rates, n);
  const auto conservation = sourceConservation(sources, n);

  EXPECT_GT(conservation.maxAbsoluteSource, 0.0) << "the sources must be non-trivial";
  EXPECT_LE(conservation.maxAbsoluteSum, 32.0 * kEps * conservation.maxAbsoluteSource)
      << "max per-cell |sum omega| = " << conservation.maxAbsoluteSum << " at cell "
      << conservation.worstCell;
  // the inert species must be untouched by either reaction
  for (Index cell = 0; cell < n; ++cell) {
    EXPECT_EQ(sources.perSpecies[3].Su[cell], 0.0) << "cell " << cell;
  }
}

// ===================================================================
// Determinism -- section 36
// ===================================================================

TEST(ArrheniusKineticsTest, KineticsAreBitwiseDeterministic) {
  const Mesh mesh = MeshGeometry::createCartesian2D(8, 4, 1.0, 1.0);
  const Index n = mesh.numberOfCells();
  const auto mixture = chainMixture();
  const auto mechanism = chainMechanism();
  const MassFractionState state(chainRegistry(), n, {0.40, 0.30, 0.20, 0.10});
  ScalarField temperature(n);
  for (Index cell = 0; cell < n; ++cell) {
    temperature[cell] = 700.0 + (40.0 * static_cast<Real>(cell));
  }
  const ScalarField density(n, 1.2);

  auto build = [&]() {
    std::vector<ReactionRateParameters> parameters;
    parameters.push_back({"A->B", ArrheniusParameters(kA, 0.5, 5.0e4), std::nullopt});
    parameters.push_back({"B->C", ArrheniusParameters(kA, -0.5, 8.0e4), std::nullopt});
    return ReactionKinetics(mechanism, std::move(parameters));
  };

  const auto firstRates =
      arrheniusProgressRates(mechanism, build(), mixture, state, density, temperature);
  const auto firstSources = reactionSpeciesSources(mechanism, mixture, firstRates, n);

  for (int repeat = 0; repeat < 50; ++repeat) {
    // rebuild the kinetics too, so parameter binding is included
    const auto rates =
        arrheniusProgressRates(mechanism, build(), mixture, state, density, temperature);
    const auto sources = reactionSpeciesSources(mechanism, mixture, rates, n);
    for (Index r = 0; r < 2; ++r) {
      for (Index cell = 0; cell < n; ++cell) {
        EXPECT_EQ(rates.perReaction[r][cell], firstRates.perReaction[r][cell])
            << "repeat " << repeat << " reaction " << r << " cell " << cell;
      }
    }
    for (Index i = 0; i < 4; ++i) {
      for (Index cell = 0; cell < n; ++cell) {
        EXPECT_EQ(sources.perSpecies[i].Su[cell], firstSources.perSpecies[i].Su[cell]);
      }
    }
  }
}

TEST(ArrheniusKineticsTest, RejectsAnInvalidTemperatureFieldNamingTheCell) {
  // Section 38: the diagnostic must identify the cell.
  const Mesh mesh = MeshGeometry::createCartesian2D(4, 1, 1.0, 1.0);
  const Index n = mesh.numberOfCells();
  const auto mechanism = h2Mechanism();
  const MassFractionState state(h2Registry(), n, {0.10, 0.80, 1.0 - 0.10 - 0.80});
  ScalarField temperature(n, 1500.0);
  temperature[2] = -5.0;
  try {
    static_cast<void>(arrheniusProgressRates(mechanism, h2Kinetics(mechanism), h2Mixture(), state,
                                             ScalarField(n, 1.0), temperature));
    FAIL() << "expected an invalid-temperature rejection";
  } catch (const InvalidArgumentError& error) {
    const std::string message = error.what();
    EXPECT_NE(message.find("cell 2"), std::string::npos) << message;
    EXPECT_NE(message.find("2H2+O2->2H2O"), std::string::npos) << message;
  }
}
