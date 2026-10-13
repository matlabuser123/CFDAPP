// P18.6 -- heat release and the chemistry/energy coupling.
//
// EVERY EXPECTED VALUE CAME FROM AN INDEPENDENT ARBITRARY-PRECISION IMPLEMENTATION:
// results/p18/heat_release/tools/heat_release_reference.py, mpmath at 60 decimal digits, NO CFDApp
// code imported or consulted. Its full output is preserved at
// results/p18/heat_release/data/reference_values.txt and the values below are transcribed from it
// at 17 significant figures, which round-trips a double exactly.
//
// THE CONVENTION UNDER TEST WAS FROZEN BEFORE THE PRODUCTION CODE WAS WRITTEN, at
// results/p18/heat_release/energy_accounting.md:
//     transported energy   u_sens = rho cp(T,Y) T          -- NO formation energy
//     conserved total      E = u_sens + rho sum_i Y_i h_f,i
//     source               S_E = -sum_i h_f,i omega_i = -sum_r q_r Delta H_r^0
//     sign                 Delta H_r^0 < 0 exothermic; q > 0 forward; S_E > 0 adds heat
//
// THRESHOLD: G7's own, inherited verbatim -- |energy imbalance| / |total energy| <= 8 * eps *
// N_cells, "the same derived floor as G3 and for the same reason".
#include <gtest/gtest.h>

#include <cmath>
#include <limits>
#include <string>
#include <tuple>
#include <vector>

#include "cfd/core/Exception.hpp"
#include "cfd/mesh/MeshGeometry.hpp"
#include "cfd/species/ArrheniusKinetics.hpp"
#include "cfd/species/HeatRelease.hpp"
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
using cfd::mesh::MeshGeometry;
using cfd::species::ArrheniusParameters;
using cfd::species::arrheniusProgressRates;
using cfd::species::energyDensity;
using cfd::species::FormationEnthalpies;
using cfd::species::MassFractionState;
using cfd::species::MixtureThermodynamics;
using cfd::species::Reaction;
using cfd::species::reactionEnthalpy;
using cfd::species::reactionHeatCapacity;
using cfd::species::reactionHeatRelease;
using cfd::species::reactionHeatReleaseFromSpeciesSources;
using cfd::species::ReactionKinetics;
using cfd::species::ReactionMechanism;
using cfd::species::ReactionProgressRates;
using cfd::species::ReactionRateParameters;
using cfd::species::reactionSpeciesSources;
using cfd::species::ReactorState;
using cfd::species::SpeciesRegistry;
using cfd::species::SpeciesThermodynamics;
using cfd::species::standardReactionEnthalpy;
using cfd::species::temperatureFromSensibleEnergy;
using cfd::species::totalEnergy;
using cfd::thermal::SensibleEnthalpy;
using cfd::thermal::TemperatureDependentProperty;

namespace {

constexpr Real kEps = std::numeric_limits<Real>::epsilon();

// G7's allowance for the energy balance: 8 * eps * N_cells, relative.
Real g7(Real scale, Index cells) { return 8.0 * kEps * static_cast<Real>(cells) * std::abs(scale); }

// ---------------------------------------------------------------- the reference mechanism
// Equal, DYADIC molecular weights, so sum_i M_i nu_i == 0 EXACTLY in binary for a 1:1 conversion.
constexpr Real kM = 0.0625;        // 2^-4 kg/mol
constexpr Real kCp = 1000.0;       // J/(kg K), EQUAL for every species
constexpr Real kHfA = 0.0;         // J/kg -- the reference element
constexpr Real kHfB = -2000000.0;  // J/kg -- lower, so A->B is EXOTHERMIC
constexpr Real kHfC = 3000000.0;   // J/kg -- higher, so A->C is ENDOTHERMIC

// hbar_f,i = M_i h_f,i [J/mol], from the reference
constexpr Real kHbarB = -125000.0;
constexpr Real kHbarC = 187500.0;

// Delta H_r^0 [J/mol], from the reference
constexpr Real kDhAB = -125000.0;
constexpr Real kDhAC = 187500.0;
constexpr Real kDhBC = 312500.0;

SensibleEnthalpy constantCp(Real value, const std::string& name) {
  return SensibleEnthalpy(TemperatureDependentProperty::constant(value, name), 298.15, 0.0);
}

SpeciesRegistry abcRegistry() { return SpeciesRegistry({"A", "B", "C"}); }

// All three species share ONE constant cp, so cp_mix is composition-independent and the adiabatic
// final temperature is analytic. The composition-DEPENDENT case is a separate fixture below, so
// the two effects can never be confused (authorization section 35).
MixtureThermodynamics equalCpMixture() {
  std::vector<SpeciesThermodynamics> species;
  species.push_back(SpeciesThermodynamics(kM, constantCp(kCp, "cp_A")));
  species.push_back(SpeciesThermodynamics(kM, constantCp(kCp, "cp_B")));
  species.push_back(SpeciesThermodynamics(kM, constantCp(kCp, "cp_C")));
  return MixtureThermodynamics(abcRegistry(), std::move(species));
}

FormationEnthalpies abcFormation() {
  return FormationEnthalpies(abcRegistry(), {kHfA, kHfB, kHfC});
}

Reaction reactionAB() { return Reaction("A->B", abcRegistry(), {{"A", 1.0}}, {{"B", 1.0}}); }
Reaction reactionAC() { return Reaction("A->C", abcRegistry(), {{"A", 1.0}}, {{"C", 1.0}}); }
Reaction reactionBC() { return Reaction("B->C", abcRegistry(), {{"B", 1.0}}, {{"C", 1.0}}); }

ReactionMechanism mechanismAB() {
  std::vector<Reaction> reactions;
  reactions.push_back(reactionAB());
  return ReactionMechanism(abcRegistry(), std::move(reactions), equalCpMixture());
}

ReactionMechanism mechanismABandAC() {
  std::vector<Reaction> reactions;
  reactions.push_back(reactionAB());
  reactions.push_back(reactionAC());
  return ReactionMechanism(abcRegistry(), std::move(reactions), equalCpMixture());
}

// One reaction, one cell, a prescribed progress rate -- the injected-q cases of sections 10-13.
ReactionProgressRates ratesOf(const std::vector<Real>& perReaction, Index cells) {
  ReactionProgressRates rates;
  for (const Real q : perReaction) {
    rates.perReaction.push_back(ScalarField(cells, q));
  }
  return rates;
}

}  // namespace

// ===================================================================
// FormationEnthalpies -- the one quantity CFDApp lacked
// ===================================================================

TEST(FormationEnthalpyTest, MassAndMolarFormsAgreeWithTheReference) {
  const auto mixture = equalCpMixture();
  const auto formation = abcFormation();

  EXPECT_DOUBLE_EQ(formation.massSpecific(0), kHfA);
  EXPECT_DOUBLE_EQ(formation.massSpecific(1), kHfB);
  EXPECT_DOUBLE_EQ(formation.massSpecific(2), kHfC);

  // hbar_f,i = M_i h_f,i. The weights are dyadic, so this is EXACT.
  EXPECT_DOUBLE_EQ(formation.molar(mixture, 0), 0.0);
  EXPECT_DOUBLE_EQ(formation.molar(mixture, 1), kHbarB);
  EXPECT_DOUBLE_EQ(formation.molar(mixture, 2), kHbarC);
}

TEST(FormationEnthalpyTest, FromMolarRoundTripsExactlyForDyadicWeights) {
  const auto mixture = equalCpMixture();
  const auto formation = FormationEnthalpies::fromMolar(mixture, {0.0, kHbarB, kHbarC});
  // h_f = hbar_f / M. With M = 2^-4 the division is exact, so the round trip is BITWISE.
  EXPECT_DOUBLE_EQ(formation.massSpecific(0), kHfA);
  EXPECT_DOUBLE_EQ(formation.massSpecific(1), kHfB);
  EXPECT_DOUBLE_EQ(formation.massSpecific(2), kHfC);
  for (Index i = 0; i < 3; ++i) {
    EXPECT_EQ(formation.molar(mixture, i), abcFormation().molar(mixture, i)) << "species " << i;
  }
}

TEST(FormationEnthalpyTest, RejectsBadInputAndAcceptsEveryPhysicalSign) {
  const auto registry = abcRegistry();
  // wrong count
  EXPECT_THROW(FormationEnthalpies(registry, {0.0, 1.0}), InvalidArgumentError);
  EXPECT_THROW(FormationEnthalpies(registry, {0.0, 1.0, 2.0, 3.0}), InvalidArgumentError);
  // non-finite
  for (const Real bad :
       {std::numeric_limits<Real>::quiet_NaN(), std::numeric_limits<Real>::infinity(),
        -std::numeric_limits<Real>::infinity()}) {
    EXPECT_THROW(FormationEnthalpies(registry, {0.0, bad, 0.0}), InvalidArgumentError);
  }
  // EVERY SIGN IS VALID, including zero: h_f = 0 is correct for an element in its reference state,
  // so a positivity check here would be a defect rather than a safeguard.
  EXPECT_NO_THROW(FormationEnthalpies(registry, {0.0, -1.0e7, 1.0e7}));
  EXPECT_NO_THROW(FormationEnthalpies(registry, {0.0, 0.0, 0.0}));
}

TEST(FormationEnthalpyTest, N2_IsDistinctFromTheSensibleReferenceEnthalpy) {
  // CONTROL N2, and it is G7's OWN third control, verbatim: "h_f,i replaced by the SENSIBLE
  // reference enthalpy must FAIL. This control matters because those are different quantities that
  // the existing API could easily conflate".
  //
  // SensibleEnthalpy::referenceEnthalpy is an OFFSET on one substance's sensible scale. It cancels
  // out of every physical result. A heat of formation does not cancel -- it IS the heat release.
  //
  // Here the sensible reference enthalpies are deliberately set to the values a confused
  // implementation would read as formation enthalpies, and the two give different answers.
  std::vector<SpeciesThermodynamics> species;
  species.push_back(SpeciesThermodynamics(
      kM, SensibleEnthalpy(TemperatureDependentProperty::constant(kCp, "cp_A"), 298.15, 111.0)));
  species.push_back(SpeciesThermodynamics(
      kM, SensibleEnthalpy(TemperatureDependentProperty::constant(kCp, "cp_B"), 298.15, 222.0)));
  species.push_back(SpeciesThermodynamics(
      kM, SensibleEnthalpy(TemperatureDependentProperty::constant(kCp, "cp_C"), 298.15, 333.0)));
  const MixtureThermodynamics mixture(abcRegistry(), std::move(species));
  const auto formation = abcFormation();
  const Reaction reaction = reactionAB();

  // the real reaction enthalpy, from the FORMATION enthalpies
  const Real correct = standardReactionEnthalpy(reaction, mixture, formation);
  EXPECT_DOUBLE_EQ(correct, kDhAB);

  // what a confused implementation would get from the SENSIBLE references: 0.0625*(222 - 111)
  const Real confused = kM * (222.0 - 111.0);
  EXPECT_NEAR(confused, 6.9375, 1.0e-12);
  EXPECT_GT(std::abs(correct - confused), 1.0e4)
      << "the two quantities differ by " << std::abs(correct - confused) << " J/mol";
  // and the ratio is four orders of magnitude -- not a subtle error
  EXPECT_GT(std::abs(correct / confused), 1.0e4);

  // AND THE SENSIBLE REFERENCE PROVABLY DOES NOT AFFECT THE ANSWER. A common sensible offset is
  // unobservable in Delta H_r^0, which is the formal statement of "it cancels".
  EXPECT_EQ(standardReactionEnthalpy(reaction, mixture, formation),
            standardReactionEnthalpy(reaction, equalCpMixture(), formation))
      << "Delta H_r^0 must be BITWISE independent of the sensible reference enthalpies";
}

// ===================================================================
// Reaction enthalpy and its sign -- sections 5, 10, 11, 12
// ===================================================================

TEST(ReactionEnthalpyTest, StandardReactionEnthalpyMatchesTheReferenceAndSigns) {
  const auto mixture = equalCpMixture();
  const auto formation = abcFormation();

  // Transcribed from data/reference_values.txt. Dyadic weights make these EXACT.
  EXPECT_DOUBLE_EQ(standardReactionEnthalpy(reactionAB(), mixture, formation), kDhAB);
  EXPECT_DOUBLE_EQ(standardReactionEnthalpy(reactionAC(), mixture, formation), kDhAC);
  EXPECT_DOUBLE_EQ(standardReactionEnthalpy(reactionBC(), mixture, formation), kDhBC);

  // THE FROZEN SIGN CONVENTION (section 5): negative is exothermic.
  EXPECT_LT(kDhAB, 0.0) << "A->B must be exothermic";
  EXPECT_GT(kDhAC, 0.0) << "A->C must be endothermic";
}

TEST(ReactionEnthalpyTest, AThermoneutralReactionIsExactlyZeroByCancellation) {
  // Section 12. The two species share ONE NON-ZERO h_f, so the result is 0 because the terms
  // CANCEL -- not because every term is 0. A thermoneutral test built with h_f = 0 everywhere
  // would pass for an implementation that ignores h_f entirely.
  const Real shared = -750000.0;
  const auto formation = FormationEnthalpies(abcRegistry(), {shared, shared, 1234567.0});
  const auto mixture = equalCpMixture();
  EXPECT_EQ(standardReactionEnthalpy(reactionAB(), mixture, formation), 0.0)
      << "exactly 0, not merely small";
  // and the individual terms are NOT zero, which is what makes this a cancellation
  EXPECT_NE(formation.molar(mixture, 0), 0.0);
  EXPECT_NE(formation.molar(mixture, 1), 0.0);
}

TEST(ReactionEnthalpyTest, ACatalystContributesExactlyNothing) {
  // A species with equal reactant and product coefficients has net nu = 0 and must not enter
  // Delta H_r at all, whatever its formation enthalpy.
  std::vector<Reaction> reactions;
  reactions.push_back(Reaction("A->B (C catalyses)", abcRegistry(), {{"A", 1.0}, {"C", 1.0}},
                               {{"B", 1.0}, {"C", 1.0}}));
  const ReactionMechanism mechanism(abcRegistry(), std::move(reactions), equalCpMixture());
  const auto mixture = equalCpMixture();
  // C's h_f is +3e6 J/kg, the largest in the set, so a defect that counted it would be obvious
  EXPECT_DOUBLE_EQ(standardReactionEnthalpy(mechanism.reaction(0), mixture, abcFormation()), kDhAB);
  EXPECT_DOUBLE_EQ(mechanism.reaction(0).netStoichiometry(2), 0.0);
}

// ===================================================================
// Kirchhoff and the temperature-dependent reaction enthalpy -- sections 31, 32
// ===================================================================

namespace {

// cp_i(T) = a0 + a1 T, DIFFERENT per species so Delta Cp_r is not zero.
MixtureThermodynamics linearCpMixture() {
  std::vector<SpeciesThermodynamics> species;
  species.push_back(SpeciesThermodynamics(
      kM, SensibleEnthalpy(TemperatureDependentProperty({900.0, 0.25}, 200.0, 3000.0, "cp_A"),
                           298.15, 0.0)));
  species.push_back(SpeciesThermodynamics(
      kM, SensibleEnthalpy(TemperatureDependentProperty({1100.0, -0.15}, 200.0, 3000.0, "cp_B"),
                           298.15, 0.0)));
  species.push_back(SpeciesThermodynamics(
      kM, SensibleEnthalpy(TemperatureDependentProperty({1000.0, 0.05}, 200.0, 3000.0, "cp_C"),
                           298.15, 0.0)));
  return MixtureThermodynamics(abcRegistry(), std::move(species));
}

}  // namespace

TEST(ReactionEnthalpyTest, TemperatureDependentReactionEnthalpyMatchesTheReference) {
  // Section 31: Delta H_r(T) = sum_i nu_i M_i [h_f,i + h_i(T)], with h_i(T) P15's INTEGRATED
  // sensible enthalpy -- never cp_i * T, which P15's own header measures as 26-103% wrong.
  const auto mixture = linearCpMixture();
  const auto formation = abcFormation();
  const Reaction ab = reactionAB();
  const Reaction ac = reactionAC();

  struct Point {
    Real temperature;
    Real deltaHab;
    Real deltaCpAb;
    Real deltaHac;
    Real deltaCpAc;
  };
  // Transcribed from data/reference_values.txt at 17 significant figures.
  const std::vector<Point> points{
      {400.0, -124615.70721875000, 2.5000000000000000, 187692.14639062500, 1.2500000000000000},
      {800.0, -125615.70721875000, -7.5000000000000000, 187192.14639062500, -3.7500000000000000},
      {1500.0, -136990.70721875000, -25.000000000000000, 181504.64639062500, -12.500000000000000},
      {2500.0, -174490.70721875000, -50.000000000000000, 162754.64639062500, -25.000000000000000},
  };

  Real worst = 0.0;
  for (const Point& p : points) {
    const Real hab = reactionEnthalpy(ab, mixture, formation, p.temperature);
    const Real hac = reactionEnthalpy(ac, mixture, formation, p.temperature);
    EXPECT_NEAR(hab, p.deltaHab, 32.0 * kEps * std::abs(p.deltaHab)) << "T = " << p.temperature;
    EXPECT_NEAR(hac, p.deltaHac, 32.0 * kEps * std::abs(p.deltaHac)) << "T = " << p.temperature;
    worst = std::max(worst, std::abs(hab - p.deltaHab) / std::abs(p.deltaHab));
    worst = std::max(worst, std::abs(hac - p.deltaHac) / std::abs(p.deltaHac));

    EXPECT_NEAR(reactionHeatCapacity(ab, mixture, p.temperature), p.deltaCpAb,
                32.0 * kEps * std::abs(p.deltaCpAb));
    EXPECT_NEAR(reactionHeatCapacity(ac, mixture, p.temperature), p.deltaCpAc,
                32.0 * kEps * std::abs(p.deltaCpAc));
  }
  EXPECT_LE(worst, 32.0 * kEps) << "worst relative Delta H_r(T) error = " << worst;

  // DELTA CP IS NOT ZERO, so the temperature dependence is real and the Kirchhoff test below is
  // not comparing two zeros.
  EXPECT_NE(reactionHeatCapacity(ab, mixture, 1500.0), 0.0);
}

TEST(ReactionEnthalpyTest, KirchhoffRelationHoldsAgainstAFiniteDifference) {
  // Section 32: d(Delta H_r)/dT = Delta Cp_r. Two independent routes -- one differentiates the
  // implemented reaction enthalpy, the other sums nu_i M_i cp_i(T) directly.
  //
  // THE STEP AND THE BOUND ARE DERIVED, exactly as P18.5's derivative test derived them: a central
  // difference's total error is minimised near h ~ T eps^(1/3), and the floor on the RELATIVE
  // agreement is then eps^(2/3). The bound is 8 * eps^(2/3) -- the dimensional floor times a small
  // integer, not the observation.
  const auto mixture = linearCpMixture();
  const auto formation = abcFormation();
  const Real floorValue = std::cbrt(kEps * kEps);

  Real worst = 0.0;
  for (const Reaction& reaction : {reactionAB(), reactionAC(), reactionBC()}) {
    for (const Real t : {400.0, 700.0, 1200.0, 1800.0, 2500.0}) {
      const Real closed = reactionHeatCapacity(reaction, mixture, t);
      const Real h = t * 1.0e-6;
      const Real fd = (reactionEnthalpy(reaction, mixture, formation, t + h) -
                       reactionEnthalpy(reaction, mixture, formation, t - h)) /
                      (2.0 * h);
      // Delta Cp_r is a SMALL difference of large numbers here (tens, from terms of ~1e5), so the
      // comparison is made against the scale of the TERMS rather than of their difference --
      // otherwise the bound would demand a precision the subtraction has already lost.
      const Real scale = std::abs(reactionEnthalpy(reaction, mixture, formation, t));
      const Real relative = std::abs(fd - closed) / scale;
      worst = std::max(worst, relative);
      EXPECT_LE(relative, 8.0 * floorValue)
          << reaction.name() << " at T = " << t << ": closed " << closed << " vs fd " << fd;
    }
  }
  EXPECT_LE(worst, 8.0 * floorValue) << "worst Kirchhoff disagreement = " << worst << " = "
                                     << worst / floorValue << " x eps^(2/3)";
}

// ===================================================================
// The energy source -- sections 6, 7, 10, 11, 13, 26, 27, 28
// ===================================================================

TEST(HeatSourceTest, ExothermicReactionAddsEnergyWithTheReferenceMagnitude) {
  // Section 10. Injected q > 0, independently computed Delta H_r and S_E.
  const auto mechanism = mechanismAB();
  const auto mixture = equalCpMixture();
  const auto formation = abcFormation();
  const Index cells = 4;

  struct Case {
    Real q;
    Real expected;  // S_E [W/m^3], from data/reference_values.txt
  };
  const std::vector<Case> cases{
      {0.0, 0.0}, {1.0, 125000.0}, {2.0, 250000.0}, {7.5, 937500.0}, {1000.0, 125000000.0}};

  for (const Case& c : cases) {
    const auto source =
        reactionHeatRelease(mechanism, mixture, formation, ratesOf({c.q}, cells), cells);
    for (Index cell = 0; cell < cells; ++cell) {
      EXPECT_DOUBLE_EQ(source[cell], c.expected) << "q = " << c.q << " cell " << cell;
    }
    if (c.q > 0.0) {
      EXPECT_GT(source[0], 0.0) << "exothermic + forward progress must ADD energy";
    }
  }
}

TEST(HeatSourceTest, EndothermicReactionRemovesEnergyWithTheReferenceMagnitude) {
  // Section 11 -- the control against a GLOBAL heat-source sign reversal, which an
  // exothermic-only test cannot detect.
  std::vector<Reaction> reactions;
  reactions.push_back(reactionAC());
  const ReactionMechanism mechanism(abcRegistry(), std::move(reactions), equalCpMixture());
  const auto mixture = equalCpMixture();
  const auto formation = abcFormation();
  const Index cells = 3;

  for (const auto& [q, expected] : std::vector<std::pair<Real, Real>>{
           {1.0, -187500.0}, {2.0, -375000.0}, {7.5, -1406250.0}, {1000.0, -187500000.0}}) {
    const auto source =
        reactionHeatRelease(mechanism, mixture, formation, ratesOf({q}, cells), cells);
    EXPECT_DOUBLE_EQ(source[0], expected) << "q = " << q;
    EXPECT_LT(source[0], 0.0) << "endothermic + forward progress must REMOVE energy";
  }
}

TEST(HeatSourceTest, ThermoneutralReactionGivesExactlyZeroAtEveryRate) {
  // Section 12: separates reaction PROGRESS from heat RELEASE. Non-zero q, exactly zero S_E.
  const Real shared = -750000.0;
  const auto formation = FormationEnthalpies(abcRegistry(), {shared, shared, 1234567.0});
  const auto mechanism = mechanismAB();
  const auto mixture = equalCpMixture();
  const Index cells = 2;
  // A NEGATIVE RATE IS INCLUDED DELIBERATELY and reaches only the MOLAR route, which needs just q
  // and Delta H_r. P18.4 would refuse it for this irreversible reaction, and that asymmetry is the
  // intended one: direction validity is P18.4's invariant, enforced in one place.
  for (const Real q : {1.0, 1000.0, 1.0e6, -5.0}) {
    const auto source =
        reactionHeatRelease(mechanism, mixture, formation, ratesOf({q}, cells), cells);
    EXPECT_EQ(source[0], 0.0) << "q = " << q << " must give EXACTLY zero heat release";
  }
  // and the species sources are NOT zero, so progress really is happening
  const auto sources = reactionSpeciesSources(mechanism, mixture, ratesOf({1000.0}, cells), cells);
  EXPECT_NE(sources.perSpecies[0].Su[0], 0.0) << "species must still be converting";
}

TEST(HeatSourceTest, N6_ZeroRateGivesExactlyZeroSourceAndZeroOmega) {
  // Section 13 and CONTROL N6 (omit the reaction energy source): with q = 0 both omega_i and S_E
  // must be EXACTLY zero, so the reacting path reduces to the non-reacting one identically.
  const auto mechanism = mechanismABandAC();
  const auto mixture = equalCpMixture();
  const auto formation = abcFormation();
  const Index cells = 8;
  const auto rates = ratesOf({0.0, 0.0}, cells);

  const auto source = reactionHeatRelease(mechanism, mixture, formation, rates, cells);
  const auto sources = reactionSpeciesSources(mechanism, mixture, rates, cells);
  for (Index cell = 0; cell < cells; ++cell) {
    EXPECT_EQ(source[cell], 0.0) << "cell " << cell;
    for (Index i = 0; i < 3; ++i) {
      EXPECT_EQ(sources.perSpecies[i].Su[cell], 0.0) << "species " << i << " cell " << cell;
    }
  }
}

TEST(HeatSourceTest, TheMolarAndMassRoutesAgree) {
  // Sections 6 and 7. S_E = -sum_r q_r Delta H_r^0  and  S_E = -sum_i h_f,i omega_i are
  // ALGEBRAICALLY IDENTICAL; they are implemented independently so the two can be compared, and
  // they share no arithmetic beyond h_f,i.
  const auto mechanism = mechanismABandAC();
  const auto mixture = equalCpMixture();
  const auto formation = abcFormation();
  const Index cells = 6;

  // EVERY RATE HERE IS NON-NEGATIVE, because both reactions are IRREVERSIBLE and P18.4 refuses a
  // negative rate on one -- correctly. The first version of this test fed q = -2.0 and P18.4's
  // guard caught it; the failure is preserved at logs/02_first_failure_PRESERVED.log, and the
  // negative-rate comparison moved to the REVERSIBLE case below, where it is legitimate.
  //
  // NOTE THE ASYMMETRY, which is deliberate: the MOLAR route needs only q and Delta H_r and does
  // not duplicate P18.4's direction validation, so it would have accepted the negative rate. One
  // invariant, enforced in one place.
  Real worst = 0.0;
  for (const auto& pair : std::vector<std::pair<Real, Real>>{
           {1.0, 0.0}, {0.0, 1.0}, {2.0, 0.5}, {7.5, 3.25}, {1000.0, 1.0e-3}, {4.0, 2.0}}) {
    const auto rates = ratesOf({pair.first, pair.second}, cells);
    const auto molar = reactionHeatRelease(mechanism, mixture, formation, rates, cells);
    const auto sources = reactionSpeciesSources(mechanism, mixture, rates, cells);
    const auto mass = reactionHeatReleaseFromSpeciesSources(formation, sources, cells);
    for (Index cell = 0; cell < cells; ++cell) {
      const Real scale = std::max(std::abs(molar[cell]), 1.0);
      const Real relative = std::abs(molar[cell] - mass[cell]) / scale;
      worst = std::max(worst, relative);
      EXPECT_LE(relative, 8.0 * kEps)
          << "q = (" << pair.first << ", " << pair.second << ") cell " << cell << ": molar "
          << molar[cell] << " vs mass " << mass[cell];
    }
  }
  EXPECT_LE(worst, 8.0 * kEps) << "worst relative route disagreement = " << worst;
}

TEST(HeatSourceTest, SourceIsExactlyLinearInTheProgressRate) {
  // Section 27 -- catches hidden nonlinear scaling in the source conversion.
  const auto mechanism = mechanismAB();
  const auto mixture = equalCpMixture();
  const auto formation = abcFormation();
  const Index cells = 1;
  const Real q1 = 3.25;
  const Real base =
      reactionHeatRelease(mechanism, mixture, formation, ratesOf({q1}, cells), cells)[0];
  EXPECT_DOUBLE_EQ(base, 406250.0);  // from data/reference_values.txt

  for (const Real c : {2.0, 0.5, 1.0e6, -1.0, 0.0}) {
    const Real scaled =
        reactionHeatRelease(mechanism, mixture, formation, ratesOf({c * q1}, cells), cells)[0];
    // dyadic c values make this BITWISE; 1e6 is not dyadic, so it is held to round-off
    if (c == 2.0 || c == 0.5 || c == -1.0 || c == 0.0) {
      EXPECT_EQ(scaled, c * base) << "c = " << c << " must be bitwise exact";
    } else {
      EXPECT_NEAR(scaled, c * base, 8.0 * kEps * std::abs(c * base)) << "c = " << c;
    }
  }
}

TEST(HeatSourceTest, MultipleReactionsSuperposeExactly) {
  // Section 28, including the exothermic + endothermic mix that a same-sign pair cannot test.
  const auto mechanism = mechanismABandAC();
  const auto mixture = equalCpMixture();
  const auto formation = abcFormation();
  const Index cells = 1;

  const Real qAB = 2.0;
  const Real qAC = 0.5;
  const Real both =
      reactionHeatRelease(mechanism, mixture, formation, ratesOf({qAB, qAC}, cells), cells)[0];
  const Real onlyAB =
      reactionHeatRelease(mechanism, mixture, formation, ratesOf({qAB, 0.0}, cells), cells)[0];
  const Real onlyAC =
      reactionHeatRelease(mechanism, mixture, formation, ratesOf({0.0, qAC}, cells), cells)[0];

  // from data/reference_values.txt
  EXPECT_DOUBLE_EQ(onlyAB, 250000.0);
  EXPECT_DOUBLE_EQ(onlyAC, -93750.0);
  EXPECT_DOUBLE_EQ(both, 156250.0);
  EXPECT_EQ(both, onlyAB + onlyAC) << "superposition must be exact addition";
  // the two have OPPOSITE signs, so this is not a same-sign superposition that cannot detect a
  // sign error in one term
  EXPECT_GT(onlyAB, 0.0);
  EXPECT_LT(onlyAC, 0.0);

  // two EXOTHERMIC reactions as well
  std::vector<Reaction> twoExo;
  twoExo.push_back(reactionAB());
  twoExo.push_back(Reaction("A->B bis", abcRegistry(), {{"A", 1.0}}, {{"B", 1.0}}));
  const ReactionMechanism exoMech(abcRegistry(), std::move(twoExo), equalCpMixture());
  const Real sum =
      reactionHeatRelease(exoMech, mixture, formation, ratesOf({1.0, 3.0}, cells), cells)[0];
  EXPECT_DOUBLE_EQ(sum, 125000.0 * 4.0);
}

TEST(HeatSourceTest, N11_OmittingOneReactionIsDetected) {
  // CONTROL N11: omit one reaction from the total S_E.
  const auto mixture = equalCpMixture();
  const auto formation = abcFormation();
  const Index cells = 1;
  const auto both = mechanismABandAC();

  std::vector<Reaction> onlyFirst;
  onlyFirst.push_back(reactionAB());
  const ReactionMechanism partial(abcRegistry(), std::move(onlyFirst), equalCpMixture());

  const Real full =
      reactionHeatRelease(both, mixture, formation, ratesOf({2.0, 0.5}, cells), cells)[0];
  const Real dropped =
      reactionHeatRelease(partial, mixture, formation, ratesOf({2.0}, cells), cells)[0];
  EXPECT_DOUBLE_EQ(full, 156250.0);
  EXPECT_DOUBLE_EQ(dropped, 250000.0);
  EXPECT_NE(full, dropped);
  // the omitted reaction is 60% of the magnitude of the one kept -- unmistakable
  EXPECT_GT(std::abs(full - dropped) / std::abs(full), 0.5);
}

TEST(HeatSourceTest, N9_AKmolFormationEnthalpyIsWrongByExactlyAThousand) {
  // CONTROL N9 and section 43: a mol/kmol error in the formation enthalpy.
  const auto mechanism = mechanismAB();
  const auto mixture = equalCpMixture();
  const Index cells = 1;
  const auto correct = abcFormation();
  // h_f entered as J/g instead of J/kg, i.e. 1000x too small -- the realistic direction of the
  // mistake, since tables are published per mole and per gram as often as per kilogram
  const auto wrong =
      FormationEnthalpies(abcRegistry(), {kHfA / 1000.0, kHfB / 1000.0, kHfC / 1000.0});
  const Real good =
      reactionHeatRelease(mechanism, mixture, correct, ratesOf({1.0}, cells), cells)[0];
  const Real bad = reactionHeatRelease(mechanism, mixture, wrong, ratesOf({1.0}, cells), cells)[0];
  EXPECT_DOUBLE_EQ(good, 125000.0);
  EXPECT_DOUBLE_EQ(bad, 125.0);
  EXPECT_DOUBLE_EQ(good / bad, 1000.0) << "exactly 1000x, and dyadic weights make it exact";
}

TEST(HeatSourceTest, N1_FlippingTheHeatReleaseSignIsDetected) {
  // CONTROL N1, and it is G7's OWN first control: "heat-release sign flipped
  // (exothermic <-> endothermic) must FAIL".
  const auto mechanism = mechanismAB();
  const auto mixture = equalCpMixture();
  const auto formation = abcFormation();
  const Index cells = 1;
  const Real correct =
      reactionHeatRelease(mechanism, mixture, formation, ratesOf({2.0}, cells), cells)[0];
  EXPECT_DOUBLE_EQ(correct, 250000.0);
  EXPECT_GT(correct, 0.0);

  // the defect: S_E = +sum q Delta H instead of -sum q Delta H
  const Real flipped = -correct;
  EXPECT_LT(flipped, 0.0) << "the flipped source REMOVES energy from an exothermic reaction";
  // the gap is twice the magnitude -- there is no tolerance at which this is ambiguous
  EXPECT_DOUBLE_EQ(std::abs(correct - flipped), 2.0 * std::abs(correct));

  // AND IT IS DETECTABLE THROUGH THE PHYSICS, not just the arithmetic: the exothermic and
  // endothermic reactions must give OPPOSITE signs, which a global flip preserves but a
  // per-reaction sign error does not.
  std::vector<Reaction> endo;
  endo.push_back(reactionAC());
  const ReactionMechanism endoMech(abcRegistry(), std::move(endo), equalCpMixture());
  const Real endothermic =
      reactionHeatRelease(endoMech, mixture, formation, ratesOf({2.0}, cells), cells)[0];
  EXPECT_LT(endothermic * correct, 0.0) << "the two must have opposite signs";
}

// ===================================================================
// Reference-state invariance -- section 33
// ===================================================================

TEST(ReactionEnthalpyTest, AMassSpecificReferenceShiftCancelsExactly) {
  // Section 33, and the invariance is EXACT because it rests on P18.4's sum_i M_i nu_i = 0, which
  // is enforced PER REACTION on the definition.
  const auto mixture = equalCpMixture();
  const Reaction reaction = reactionAB();
  const Real base = standardReactionEnthalpy(reaction, mixture, abcFormation());
  EXPECT_DOUBLE_EQ(base, kDhAB);
  EXPECT_DOUBLE_EQ(reaction.massResidual(mixture), 0.0) << "the invariance rests on this";

  for (const Real c : {1.0e5, -2.5e6, 0.125, 1.0e9}) {
    const auto shifted = FormationEnthalpies(abcRegistry(), {kHfA + c, kHfB + c, kHfC + c});
    const Real value = standardReactionEnthalpy(reaction, mixture, shifted);
    // BITWISE for the dyadic shifts; the others are held to round-off against the SHIFT's scale,
    // because that is the magnitude the cancellation has to annihilate.
    EXPECT_NEAR(value, base, 8.0 * kEps * std::abs(c) * kM)
        << "a common mass-specific shift of " << c << " must cancel";
  }
  // a DYADIC shift must be bitwise, with no tolerance at all
  const auto dyadic =
      FormationEnthalpies(abcRegistry(), {kHfA + 0.125, kHfB + 0.125, kHfC + 0.125});
  EXPECT_EQ(standardReactionEnthalpy(reaction, mixture, dyadic), base)
      << "a dyadic common shift must cancel BITWISE";
}

TEST(ReactionEnthalpyTest, AMolarShiftAndASingleSpeciesShiftMustNotCancel) {
  // THE NON-INVARIANCE IS AS IMPORTANT AS THE INVARIANCE. Without it, the test above would pass
  // for a quantity that is insensitive to everything -- including to the formation enthalpies.
  //
  // AND A 1:1 REACTION CANNOT SHOW IT: for A->B, sum_i nu_i = -1 + 1 = 0, so a MOLAR shift cancels
  // too, for the wrong reason. The first version of the reference script was vacuous for exactly
  // this reason. A + B -> C conserves mass but NOT moles, which separates the two.
  const SpeciesRegistry registry({"A", "B", "C"});
  std::vector<SpeciesThermodynamics> species;
  species.push_back(SpeciesThermodynamics(0.0625, constantCp(kCp, "cp_A")));
  species.push_back(SpeciesThermodynamics(0.125, constantCp(kCp, "cp_B")));
  species.push_back(SpeciesThermodynamics(0.1875, constantCp(kCp, "cp_C")));  // = 0.0625 + 0.125
  const MixtureThermodynamics mixture(registry, std::move(species));
  const Reaction combine("A+B->C", registry, {{"A", 1.0}, {"B", 1.0}}, {{"C", 1.0}});

  EXPECT_DOUBLE_EQ(combine.massResidual(mixture), 0.0) << "mass IS conserved";
  const Real moleResidual =
      combine.netStoichiometry(0) + combine.netStoichiometry(1) + combine.netStoichiometry(2);
  EXPECT_DOUBLE_EQ(moleResidual, -1.0) << "moles are NOT conserved -- this is what makes the test";

  const auto formation = FormationEnthalpies(registry, {0.0, 0.0, -4000000.0});
  const Real base = standardReactionEnthalpy(combine, mixture, formation);
  EXPECT_DOUBLE_EQ(base, -750000.0);  // from data/reference_values.txt

  // a MASS-specific shift still cancels
  for (const Real c : {1.0e5, -2.5e6}) {
    const auto shifted = FormationEnthalpies(registry, {c, c, -4000000.0 + c});
    EXPECT_NEAR(standardReactionEnthalpy(combine, mixture, shifted), base,
                8.0 * kEps * std::abs(c) * 0.1875);
  }
  // a MOLAR shift must NOT cancel: it contributes c * sum_i nu_i = -c
  for (const auto& [c, expected] :
       std::vector<std::pair<Real, Real>>{{1.0e5, -850000.0}, {-2.5e6, 1750000.0}}) {
    const auto shifted =
        FormationEnthalpies::fromMolar(mixture, {0.0 + c, 0.0 + c, (-4000000.0 * 0.1875) + c});
    const Real value = standardReactionEnthalpy(combine, mixture, shifted);
    EXPECT_NEAR(value, expected, 32.0 * kEps * std::abs(expected))
        << "a molar shift of " << c << " must NOT cancel";
    EXPECT_NE(value, base);
  }
  // shifting ONE species must not cancel either
  const auto single = FormationEnthalpies(registry, {0.0, 1.0e5, -4000000.0});
  EXPECT_NE(standardReactionEnthalpy(combine, mixture, single), base);
}

// ===================================================================
// Reversible reaction heat -- section 29
// ===================================================================

TEST(HeatSourceTest, ReversibleNetRateReversesBothOmegaAndTheSource) {
  // Section 29: reversing q_net must reverse BOTH omega_i and S_E, consistently.
  std::vector<Reaction> reactions;
  reactions.push_back(Reaction("A<->B", abcRegistry(), {{"A", 1.0}}, {{"B", 1.0}},
                               cfd::species::ReactionDirection::Reversible));
  const ReactionMechanism mechanism(abcRegistry(), std::move(reactions), equalCpMixture());
  const auto mixture = equalCpMixture();
  const auto formation = abcFormation();
  const Index cells = 2;

  const Real q = 3.0;
  const auto forwardRates = ratesOf({q}, cells);
  const auto reverseRates = ratesOf({-q}, cells);

  const auto sourceForward =
      reactionHeatRelease(mechanism, mixture, formation, forwardRates, cells);
  const auto sourceReverse =
      reactionHeatRelease(mechanism, mixture, formation, reverseRates, cells);
  const auto omegaForward = reactionSpeciesSources(mechanism, mixture, forwardRates, cells);
  const auto omegaReverse = reactionSpeciesSources(mechanism, mixture, reverseRates, cells);

  // BITWISE sign reversal, both quantities
  EXPECT_EQ(sourceReverse[0], -sourceForward[0]);
  for (Index i = 0; i < 3; ++i) {
    EXPECT_EQ(omegaReverse.perSpecies[i].Su[0], -omegaForward.perSpecies[i].Su[0])
        << "species " << i;
  }
  // forward is exothermic, so reversing it must be endothermic
  EXPECT_GT(sourceForward[0], 0.0);
  EXPECT_LT(sourceReverse[0], 0.0);

  // q_forward = q_reverse => q_net = 0 => omega_i = 0 AND S_E = 0, both EXACTLY
  const auto balanced = ratesOf({0.0}, cells);
  const auto sourceBalanced = reactionHeatRelease(mechanism, mixture, formation, balanced, cells);
  const auto omegaBalanced = reactionSpeciesSources(mechanism, mixture, balanced, cells);
  EXPECT_EQ(sourceBalanced[0], 0.0);
  for (Index i = 0; i < 3; ++i) {
    EXPECT_EQ(omegaBalanced.perSpecies[i].Su[0], 0.0) << "species " << i;
  }

  // and the two routes still agree at a NEGATIVE rate, which is legitimate here
  const auto massRoute = reactionHeatReleaseFromSpeciesSources(formation, omegaReverse, cells);
  EXPECT_LE(std::abs(massRoute[0] - sourceReverse[0]) / std::abs(sourceReverse[0]), 8.0 * kEps);
}

// ===================================================================
// Energy accounting and temperature recovery -- sections 16, 17, 24
// ===================================================================

TEST(EnergyAccountingTest, EnergyDensitySplitsIntoSensibleAndChemicalPools) {
  // Section 24's conserved quantity, per cell:
  //     E = rho cp(T,Y) T  +  rho sum_i Y_i h_f,i
  const auto mixture = equalCpMixture();
  const auto formation = abcFormation();
  const Real rho = 1.0;
  const Real t = 1000.0;

  // pure A: the chemical pool is 0 because h_f,A = 0
  const auto pureA = energyDensity(mixture, formation, {1.0, 0.0, 0.0}, rho, t);
  EXPECT_DOUBLE_EQ(pureA.sensible, 1.0e6);  // 1 * 1000 * 1000
  EXPECT_DOUBLE_EQ(pureA.chemical, 0.0);
  EXPECT_DOUBLE_EQ(pureA.total, 1.0e6);

  // pure B: the chemical pool is h_f,B
  const auto pureB = energyDensity(mixture, formation, {0.0, 1.0, 0.0}, rho, t);
  EXPECT_DOUBLE_EQ(pureB.sensible, 1.0e6);
  EXPECT_DOUBLE_EQ(pureB.chemical, kHfB);
  EXPECT_DOUBLE_EQ(pureB.total, 1.0e6 + kHfB);

  // half and half: the chemical pool is the mass-weighted mean
  const auto half = energyDensity(mixture, formation, {0.5, 0.5, 0.0}, rho, t);
  EXPECT_DOUBLE_EQ(half.chemical, 0.5 * kHfB);
  EXPECT_DOUBLE_EQ(half.total, half.sensible + half.chemical);

  // THE SENSIBLE POOL ALONE IS NOT THE CONSERVED QUANTITY: pure A and pure B have the SAME
  // sensible energy at the same T and very different totals. Reporting only the sensible part
  // would make an exothermic conversion look like energy creation.
  EXPECT_EQ(pureA.sensible, pureB.sensible);
  EXPECT_NE(pureA.total, pureB.total);
}

TEST(EnergyAccountingTest, TemperatureRecoveryInvertsTheSensibleEnergyExactly) {
  // Section 17's inverse: solve rho cp(T,Y) T = u_sens for T.
  const auto mixture = equalCpMixture();
  for (const std::vector<Real>& composition :
       std::vector<std::vector<Real>>{{1.0, 0.0, 0.0}, {0.0, 1.0, 0.0}, {0.25, 0.5, 0.25}}) {
    for (const Real rho : {0.5, 1.0, 7.25}) {
      for (const Real t : {300.0, 1000.0, 2500.0}) {
        const Real u = rho * mixture.specificHeatPressure(composition, t) * t;
        const Real recovered = temperatureFromSensibleEnergy(mixture, composition, rho, u);
        EXPECT_NEAR(recovered, t, 8.0 * kEps * t)
            << "rho = " << rho << " T = " << t << " u = " << u;
      }
    }
  }
}

TEST(EnergyAccountingTest, TemperatureRecoveryHandlesATemperatureDependentSpecificHeat) {
  // With cp(T) the inversion is a ROOT FIND, not a division -- u_sens = rho cp(T) T is quadratic
  // in T for a linear cp. The bisection must still land on the right root.
  const auto mixture = linearCpMixture();
  const std::vector<Real> composition{0.25, 0.5, 0.25};
  for (const Real t : {400.0, 900.0, 1600.0, 2400.0}) {
    const Real rho = 1.3;
    const Real u = rho * mixture.specificHeatPressure(composition, t) * t;
    const Real recovered = temperatureFromSensibleEnergy(mixture, composition, rho, u);
    EXPECT_NEAR(recovered, t, 1.0e-9 * t) << "T = " << t;
  }
}

TEST(EnergyAccountingTest, N12_TemperatureRecoveryRefusesAnImpossibleEnergy) {
  // CONTROL N12 and section 42: an energy update that would give T <= 0 is REJECTED, not floored.
  const auto mixture = equalCpMixture();
  const std::vector<Real> composition{1.0, 0.0, 0.0};
  for (const Real bad : {0.0, -1.0, -1.0e9}) {
    EXPECT_THROW(static_cast<void>(temperatureFromSensibleEnergy(mixture, composition, 1.0, bad)),
                 NumericalError)
        << "u_sens = " << bad;
  }
  for (const Real bad :
       {std::numeric_limits<Real>::quiet_NaN(), std::numeric_limits<Real>::infinity()}) {
    EXPECT_THROW(static_cast<void>(temperatureFromSensibleEnergy(mixture, composition, 1.0, bad)),
                 InvalidArgumentError);
  }
  // and an invalid density
  EXPECT_THROW(static_cast<void>(temperatureFromSensibleEnergy(mixture, composition, 0.0, 1.0e6)),
               InvalidArgumentError);
  EXPECT_THROW(static_cast<void>(temperatureFromSensibleEnergy(mixture, composition, -1.0, 1.0e6)),
               InvalidArgumentError);
  // THE MESSAGE SAYS NO FLOOR IS APPLIED, so a reader cannot mistake the refusal for a clamp
  try {
    static_cast<void>(temperatureFromSensibleEnergy(mixture, composition, 1.0, -5.0));
    FAIL() << "expected a refusal";
  } catch (const NumericalError& error) {
    const std::string message = error.what();
    EXPECT_NE(message.find("No temperature floor"), std::string::npos) << message;
  }
}

// ===================================================================
// The closed adiabatic reactor -- sections 23, 24, 25, 26, 38
// ===================================================================

namespace {

// A closed, homogeneous, adiabatic, constant-volume reactor: U = 0, no boundary flux.
ReactorState makeReactor(Index cells, const std::vector<Real>& composition, Real rho, Real t) {
  return ReactorState{MassFractionState(abcRegistry(), cells, composition), ScalarField(cells, t),
                      ScalarField(cells, rho)};
}

}  // namespace

TEST(ClosedReactorTest, OneStepBalanceIsExactlyDtTimesTheSource) {
  // Section 26: a direct hand-arithmetic test of the source wiring.
  //     Delta u_sens = dt * S_E
  const auto mechanism = mechanismAB();
  const auto mixture = equalCpMixture();
  const auto formation = abcFormation();
  const Index cells = 1;
  const Real rho = 1.0;
  const Real t0 = 1000.0;

  struct Case {
    Real q;
    Real dt;
    Real source;      // S_E, from data/reference_values.txt
    Real deltaUsens;  // dt * S_E
  };
  const std::vector<Case> cases{
      {1.0, 1.0e-3, 125000.0, 125.0},
      {100.0, 1.0e-5, 12500000.0, 125.0},
      {0.5, 2.0, 62500.0, 125000.0},
  };

  Real worst = 0.0;
  for (const Case& c : cases) {
    const auto state = makeReactor(cells, {1.0, 0.0, 0.0}, rho, t0);
    const auto rates = ratesOf({c.q}, cells);
    const Real source = reactionHeatRelease(mechanism, mixture, formation, rates, cells)[0];
    EXPECT_DOUBLE_EQ(source, c.source) << "q = " << c.q;

    const auto next = advanceClosedReactor(mechanism, mixture, formation, rates, state, c.dt);

    const Real before = energyDensity(mixture, formation, state.composition.cellComposition(0), rho,
                                      state.temperature[0])
                            .sensible;
    const Real after = energyDensity(mixture, formation, next.composition.cellComposition(0), rho,
                                     next.temperature[0])
                           .sensible;
    const Real measured = after - before;
    const Real relative = std::abs(measured - c.deltaUsens) / std::abs(c.deltaUsens);
    worst = std::max(worst, relative);
    EXPECT_LE(relative, 8.0 * kEps) << "q = " << c.q << " dt = " << c.dt << ": measured "
                                    << measured << " expected " << c.deltaUsens;
  }
  EXPECT_LE(worst, 8.0 * kEps) << "worst one-step balance error = " << worst;
}

TEST(ClosedReactorTest, TotalEnergyIsConservedToG7sFloor) {
  // SECTIONS 23, 24 -- THE PRIMARY P18.6 CONSERVATION TEST.
  //
  // THE CONSERVED QUANTITY IS DEFINED EXPLICITLY, as section 24 requires:
  //     E = sum_cells V [ rho cp(T,Y) T  +  rho sum_i Y_i h_f,i ]
  // Reporting only the sensible part would be wrong -- exothermic chemistry is SUPPOSED to raise
  // it while E stays fixed. Both pools are measured and both are reported.
  //
  // G7's acceptance: |imbalance| / |E| <= 8 * eps * N_cells.
  const auto mechanism = mechanismAB();
  const auto mixture = equalCpMixture();
  const auto formation = abcFormation();
  const Index cells = 16;
  const Real rho = 1.0;
  const Real t0 = 1000.0;
  const ScalarField volume(cells, 2.0);

  auto state = makeReactor(cells, {1.0, 0.0, 0.0}, rho, t0);
  const auto initial =
      totalEnergy(mixture, formation, state.composition, state.density, state.temperature, volume);
  EXPECT_DOUBLE_EQ(initial.sensible, 1.0e6 * 2.0 * static_cast<Real>(cells));
  EXPECT_DOUBLE_EQ(initial.chemical, 0.0);

  // THE STEP COUNT IS BOUNDED BY P18.1's INVARIANT, NOT BY G7, and the bound is MEASURED.
  //
  // The first version marched 200 steps and P18.1 refused the state at step 72:
  //     "mass fractions at cell 0 sum to 0.99999999999999722; must be within 2.66e-15 of 1"
  // An explicit per-species update rounds Y_i + increment_i separately, and because the species
  // sit at very different magnitudes the two roundings are ASYMMETRIC -- adding -3.125e-5 to
  // Y_A ~ 1 loses bits, adding +3.125e-5 to Y_B ~ 0 does not. So the sum-to-one error is
  // SYSTEMATIC, not a random walk: tools/drift_probe.cpp measures it LINEAR at ~0.17 eps/step
  // (17 eps at 100 steps, 69 at 400, 284 at 1600, 1034 at 6400), crossing P18.1's 4 eps N = 12 eps
  // at step 72.
  //
  // THE INCREMENTS THEMSELVES ARE EXACTLY BALANCED -- the probe measures
  // increment_A + increment_B = 0 exactly -- so this is not a stoichiometry or source defect.
  // It is the irreducible cost of an explicit march that section 41 forbids renormalising.
  //
  // G7's OWN CRITERION IS UNAFFECTED AND UNTOUCHED: the probe measures the ENERGY drift at
  // EXACTLY ZERO for the first 1000 steps. The limit is recorded as technical debt, not hidden.
  // NO THRESHOLD WAS CHANGED.
  //
  // THE STEP COUNT CAME DOWN AND THE RATE WENT UP, so the march still does real chemistry.
  // Shortening the march alone made it vacuous -- T rose 3.75 K and Y_A fell to 0.998, and this
  // test's own non-vacuity guards caught that, which is what they are for. The per-step sum-to-one
  // cost is ~1 ulp of Y ~ 1 REGARDLESS of the increment size, so converting more per step buys
  // conversion for free:
  //     q = 128 (dyadic), dt = 1e-3  =>  dY_A = M q dt = 0.008 per step
  //     50 steps  =>  40% of A converted, T rises ~800 K, sum-to-one error ~8.5 eps of 12
  const Real q = 128.0;
  const Real dt = 1.0e-3;
  Real worstDrift = 0.0;
  Real sensibleRose = 0.0;
  for (int step = 0; step < 50; ++step) {
    state = advanceClosedReactor(mechanism, mixture, formation, ratesOf({q}, cells), state, dt);
    const auto now = totalEnergy(mixture, formation, state.composition, state.density,
                                 state.temperature, volume);
    const Real drift = std::abs(now.total - initial.total) / std::abs(initial.total);
    worstDrift = std::max(worstDrift, drift);
    sensibleRose = now.sensible - initial.sensible;
  }

  // THE CHEMISTRY ACTUALLY RAN -- without this the conservation result would be vacuous.
  EXPECT_GT(sensibleRose, 0.0) << "the sensible pool must RISE for an exothermic reaction";
  EXPECT_GT(state.temperature[0], t0 + 100.0)
      << "T rose from " << t0 << " to " << state.temperature[0];
  EXPECT_LT(state.composition.massFraction(0, 0), 0.99) << "A must have been consumed";

  // AND TOTAL ENERGY CLOSED.
  EXPECT_LE(worstDrift, 8.0 * kEps * static_cast<Real>(cells))
      << "worst relative total-energy drift = " << worstDrift << " over 50 steps, G7 allows "
      << 8.0 * kEps * static_cast<Real>(cells);

  // AND THE SUM-TO-ONE ERROR IS STILL INSIDE P18.1's INVARIANT at this step count, which is the
  // reason the count is 50. Asserting it here pins the bound rather than leaving it implicit.
  for (Index cell = 0; cell < cells; ++cell) {
    Real sum = 0.0;
    for (Index i = 0; i < 3; ++i) {
      sum += state.composition.massFraction(i, cell);
    }
    EXPECT_LE(std::abs(sum - 1.0), cfd::species::massFractionSumTolerance(3))
        << "cell " << cell << " sums to " << sum;
  }

  // the two pools moved in OPPOSITE directions by the same amount
  const auto finalEnergy =
      totalEnergy(mixture, formation, state.composition, state.density, state.temperature, volume);
  EXPECT_LE(std::abs((finalEnergy.sensible - initial.sensible) +
                     (finalEnergy.chemical - initial.chemical)) /
                std::abs(initial.total),
            8.0 * kEps * static_cast<Real>(cells));
}

TEST(ClosedReactorTest, TheExplicitMarchHasAMeasuredSumToOneStepBudget) {
  // THIS TEST EXISTS TO PIN A LIMITATION, not to demonstrate a capability.
  //
  // An explicit per-species source march accumulates sum-to-one round-off SYSTEMATICALLY, because
  // the species sit at different magnitudes and Y_i + increment_i rounds differently against
  // Y ~ 1 than against Y ~ 0. tools/drift_probe.cpp measures the growth as LINEAR at
  // ~0.17 eps/step, crossing P18.1's 4 eps N tolerance at step 72 for this case.
  //
  // Section 41 forbids the renormalisation that would mask it, so the correct behaviour is for
  // P18.1 to REFUSE the state once the invariant is broken -- which it does. This test asserts
  // both halves: the march survives inside the budget, and it is refused outside it.
  const auto mechanism = mechanismAB();
  const auto mixture = equalCpMixture();
  const auto formation = abcFormation();
  const Index cells = 1;
  const Real q = 0.5;
  const Real dt = 1.0e-3;

  auto state = makeReactor(cells, {1.0, 0.0, 0.0}, 1.0, 1000.0);
  int survived = 0;
  bool refused = false;
  for (int step = 0; step < 400; ++step) {
    try {
      state = advanceClosedReactor(mechanism, mixture, formation, ratesOf({q}, cells), state, dt);
      ++survived;
    } catch (const InvalidArgumentError&) {
      refused = true;  // P18.1 refused the state -- the invariant is doing its job
      break;
    }
  }

  // THE MARCH IS REFUSED, NOT SILENTLY RENORMALISED. That is the behaviour section 41 requires.
  EXPECT_TRUE(refused) << "the invariant must eventually refuse an un-renormalised explicit march";
  // and the budget is where the probe measured it, to within a few steps
  EXPECT_GE(survived, 60) << "survived only " << survived << " steps";
  EXPECT_LE(survived, 90) << "survived " << survived
                          << " steps; the probe measured the crossing at 72, so a much larger "
                             "number would mean the invariant is no longer being enforced";
}

TEST(ClosedReactorTest, FinalTemperatureMatchesTheIndependentAdiabaticBalance) {
  // G7: "adiabatic closed reactor: the final temperature must match the adiabatic flame
  // temperature computed from an INDEPENDENT equilibrium/enthalpy balance, to the
  // discretization's own accuracy -- REPORTED with its order, not gated on an invented constant".
  //
  // With one CONSTANT cp shared by every species, cp_mix is composition-independent and the
  // balance is analytic:
  //     T_final = T_0 + ( sum Y^0 h_f - sum Y^f h_f ) / cp
  // From data/reference_values.txt: 50% conversion from T_0 = 1000 gives EXACTLY 2000 K and
  // complete conversion gives EXACTLY 3000 K.
  const auto mechanism = mechanismAB();
  const auto mixture = equalCpMixture();
  const auto formation = abcFormation();
  const Index cells = 1;
  const Real rho = 1.0;
  const Real t0 = 1000.0;

  // Drive to EXACTLY 50% conversion in one step, so the comparison carries no time-integration
  // error at all: d(rho Y_A) = -M q dt = -0.5  =>  q dt = 0.5 / 0.0625 = 8.
  const Real dt = 1.0;
  const Real q = 8.0;
  auto state = makeReactor(cells, {1.0, 0.0, 0.0}, rho, t0);
  state = advanceClosedReactor(mechanism, mixture, formation, ratesOf({q}, cells), state, dt);

  EXPECT_NEAR(state.composition.massFraction(0, 0), 0.5, 8.0 * kEps);
  EXPECT_NEAR(state.composition.massFraction(1, 0), 0.5, 8.0 * kEps);
  const Real expected = 2000.0;  // the independent analytic balance
  const Real relative = std::abs(state.temperature[0] - expected) / expected;
  EXPECT_LE(relative, 8.0 * kEps) << "T_final = " << state.temperature[0]
                                  << " vs the independent balance " << expected
                                  << ", relative error " << relative;

  // and the complete conversion, in a second such step
  state = advanceClosedReactor(mechanism, mixture, formation, ratesOf({q}, cells), state, dt);
  EXPECT_NEAR(state.composition.massFraction(1, 0), 1.0, 8.0 * kEps);
  const Real complete = 3000.0;  // the independent analytic balance
  EXPECT_LE(std::abs(state.temperature[0] - complete) / complete, 16.0 * kEps)
      << "T_final = " << state.temperature[0] << " vs " << complete;
}

TEST(ClosedReactorTest, N4_TemperatureRecoveryUsesTheUpdatedComposition) {
  // CONTROL N4 (stale composition for temperature recovery) and section 17.
  //
  // THE CONTROL NEEDS COMPOSITION-DEPENDENT cp, or there is nothing to detect: with equal cp_i,
  // cp_mix is the same before and after and stale Y gives the identical answer. That is exactly
  // the kind of vacuous control this project keeps finding, so the mixture here has UNEQUAL cp
  // and the test asserts that cp_mix really moved.
  std::vector<SpeciesThermodynamics> species;
  species.push_back(SpeciesThermodynamics(kM, constantCp(1000.0, "cp_A")));
  species.push_back(SpeciesThermodynamics(kM, constantCp(2500.0, "cp_B")));  // 2.5x
  species.push_back(SpeciesThermodynamics(kM, constantCp(1000.0, "cp_C")));
  const MixtureThermodynamics unequal(abcRegistry(), std::move(species));

  std::vector<Reaction> reactions;
  reactions.push_back(reactionAB());
  const ReactionMechanism mechanism(abcRegistry(), std::move(reactions), unequal);
  const auto formation = abcFormation();
  const Index cells = 1;
  const Real rho = 1.0;
  const Real t0 = 1000.0;
  const Real dt = 1.0;
  const Real q = 8.0;  // 50% conversion

  const auto state = makeReactor(cells, {1.0, 0.0, 0.0}, rho, t0);
  const auto next =
      advanceClosedReactor(mechanism, unequal, formation, ratesOf({q}, cells), state, dt);

  // what the production path did: recover from the UPDATED composition
  const Real sensibleOld =
      rho * unequal.specificHeatPressure(state.composition.cellComposition(0), t0) * t0;
  const Real source =
      reactionHeatRelease(mechanism, unequal, formation, ratesOf({q}, cells), cells)[0];
  const Real sensibleNew = sensibleOld + (dt * source);
  const Real correct =
      temperatureFromSensibleEnergy(unequal, next.composition.cellComposition(0), rho, sensibleNew);
  EXPECT_NEAR(next.temperature[0], correct, 8.0 * kEps * correct);

  // THE DEFECT: recover from the STALE composition
  const Real stale = temperatureFromSensibleEnergy(unequal, state.composition.cellComposition(0),
                                                   rho, sensibleNew);
  EXPECT_GT(std::abs(correct - stale) / correct, 0.1)
      << "correct " << correct << " vs stale " << stale;
  EXPECT_NE(next.temperature[0], stale);

  // AND THE CONTROL IS NON-VACUOUS BY CONSTRUCTION: cp_mix really did change.
  const Real cpBefore = unequal.specificHeatPressure(state.composition.cellComposition(0), t0);
  const Real cpAfter = unequal.specificHeatPressure(next.composition.cellComposition(0), t0);
  EXPECT_NE(cpBefore, cpAfter) << "without this, N4 would be vacuous";
  EXPECT_DOUBLE_EQ(cpBefore, 1000.0);
  EXPECT_DOUBLE_EQ(cpAfter, 1750.0);
}

TEST(ClosedReactorTest, N5_DoubleCountingChemicalEnergyIsDetected) {
  // CONTROL N5 and section 45 -- THE PRIMARY DESIGN GATE's control.
  //
  // The defect: build the source from the TOTAL species enthalpy h_f,i + h_i(T) instead of h_f,i
  // alone. The extra term is the sensible enthalpy of the species converted, which the thermal
  // equation ALREADY evolves, so adding it double-counts.
  const auto mixture = equalCpMixture();
  const auto formation = abcFormation();
  const Reaction reaction = reactionAB();
  const Real t = 1500.0;

  // WITH EQUAL cp THE SPURIOUS TERM CANCELS, so this mixture cannot detect the defect at all.
  // Stating that here is the point: a double-counting control built on an equal-cp mixture would
  // be vacuous, which is the same trap as N4.
  EXPECT_EQ(reactionEnthalpy(reaction, mixture, formation, t),
            standardReactionEnthalpy(reaction, mixture, formation))
      << "with EQUAL cp_i the sensible term cancels exactly";

  // with UNEQUAL cp it does not cancel, and the gap is large
  const auto unequal = linearCpMixture();
  const Real correctU = standardReactionEnthalpy(reaction, unequal, formation);
  const Real wrongU = reactionEnthalpy(reaction, unequal, formation, t);
  EXPECT_DOUBLE_EQ(correctU, kDhAB);
  EXPECT_NEAR(wrongU, -136990.70721875000, 32.0 * kEps * 136990.0);  // reference value
  const Real relative = std::abs(wrongU - correctU) / std::abs(correctU);
  EXPECT_GT(relative, 0.05) << "the double-counted term is " << (100.0 * relative)
                            << "% of the real reaction enthalpy -- big enough to matter, small "
                               "enough to look like discretization error";

  // AND THE PRODUCTION SOURCE USES THE FORMATION-ONLY FORM, which is the claim being checked.
  std::vector<Reaction> reactions;
  reactions.push_back(reactionAB());
  const ReactionMechanism mechanism(abcRegistry(), std::move(reactions), unequal);
  const Real source = reactionHeatRelease(mechanism, unequal, formation, ratesOf({1.0}, 1), 1)[0];
  EXPECT_DOUBLE_EQ(source, -correctU) << "the source must use Delta H_r^0, not Delta H_r(T)";
  EXPECT_NE(source, -wrongU);
}

TEST(ClosedReactorTest, N8_UsingADifferentProgressRateForEnergyIsDetected) {
  // CONTROL N8, and it is G7's OWN second control: "the reaction rate fed to species but NOT to
  // energy must FAIL -- this is the control that the two are actually coupled rather than
  // separately plausible".
  //
  // The production API takes the progress rates as an ARGUMENT, so species and energy share one
  // q_r structurally (section 30). This control shows what a mismatch would look like.
  const auto mechanism = mechanismAB();
  const auto mixture = equalCpMixture();
  const auto formation = abcFormation();
  const Index cells = 1;

  const auto speciesRates = ratesOf({2.0}, cells);
  const auto energyRates = ratesOf({5.0}, cells);  // THE DEFECT: a different rate

  const auto sources = reactionSpeciesSources(mechanism, mixture, speciesRates, cells);
  const Real coupled = reactionHeatRelease(mechanism, mixture, formation, speciesRates, cells)[0];
  const Real mismatched = reactionHeatRelease(mechanism, mixture, formation, energyRates, cells)[0];

  // the mass route is derived FROM the species sources, so it always matches the species rate
  const auto massRoute = reactionHeatReleaseFromSpeciesSources(formation, sources, cells);
  EXPECT_LE(std::abs(massRoute[0] - coupled) / std::abs(coupled), 8.0 * kEps)
      << "the coupled pair must agree";
  EXPECT_GT(std::abs(massRoute[0] - mismatched) / std::abs(mismatched), 0.5)
      << "and the mismatched pair must NOT: " << massRoute[0] << " vs " << mismatched;

  // THE CROSS-CHECK IS THE DETECTOR. Comparing the molar route (which takes q) against the mass
  // route (which takes omega) catches a mismatch neither route alone could see, because each is
  // individually plausible.
  EXPECT_DOUBLE_EQ(coupled, 250000.0);
  EXPECT_DOUBLE_EQ(mismatched, 625000.0);
}

TEST(ClosedReactorTest, RefusesAStepThatWouldDriveSpeciesNegative) {
  // SECTION 41: a step that would consume more species than available is REFUSED, not clamped and
  // renormalised. Silently clamping would destroy the conservation this phase exists to prove.
  const auto mechanism = mechanismAB();
  const auto mixture = equalCpMixture();
  const auto formation = abcFormation();
  const Index cells = 3;
  const auto state = makeReactor(cells, {0.25, 0.75, 0.0}, 1.0, 1000.0);

  // rho Y_A = 0.25; one step at q = 8, dt = 1 removes M*q*dt = 0.5 > 0.25
  try {
    static_cast<void>(
        advanceClosedReactor(mechanism, mixture, formation, ratesOf({8.0}, cells), state, 1.0));
    FAIL() << "expected a refusal";
  } catch (const NumericalError& error) {
    const std::string message = error.what();
    EXPECT_NE(message.find("\"A\""), std::string::npos) << message;
    EXPECT_NE(message.find("cell 0"), std::string::npos) << message;
    EXPECT_NE(message.find("does not clamp or renormalise"), std::string::npos) << message;
  }
  // a small enough step IS accepted, so the guard is not simply always firing
  EXPECT_NO_THROW(static_cast<void>(
      advanceClosedReactor(mechanism, mixture, formation, ratesOf({8.0}, cells), state, 0.1)));
}

TEST(ClosedReactorTest, RefusesAnEnergyUpdateThatWouldGiveANonPositiveTemperature) {
  // SECTION 42: an endothermic reaction can remove more energy than the sensible pool holds. The
  // reference records exactly this -- complete A->C from T_0 = 1000 K implies T = -2000 K, which
  // is physically impossible and must be REFUSED rather than floored.
  std::vector<Reaction> reactions;
  reactions.push_back(reactionAC());
  const ReactionMechanism mechanism(abcRegistry(), std::move(reactions), equalCpMixture());
  const auto mixture = equalCpMixture();
  const auto formation = abcFormation();
  const Index cells = 1;
  const auto state = makeReactor(cells, {1.0, 0.0, 0.0}, 1.0, 1000.0);

  // complete conversion in one step: q dt = 1/0.0625 = 16
  EXPECT_THROW(static_cast<void>(advanceClosedReactor(mechanism, mixture, formation,
                                                      ratesOf({16.0}, cells), state, 1.0)),
               NumericalError);
  // a small endothermic step IS accepted and LOWERS the temperature
  const auto cooled =
      advanceClosedReactor(mechanism, mixture, formation, ratesOf({1.0}, cells), state, 0.1);
  EXPECT_LT(cooled.temperature[0], 1000.0) << "an endothermic step must cool the reactor";
}

// ===================================================================
// Temperature feedback into the kinetics -- sections 18, 20, 21, 22
// ===================================================================

namespace {

constexpr Real kArrA = 1.0e4;
constexpr Real kArrEa = 6.0e4;  // J/mol

}  // namespace

TEST(TemperatureFeedbackTest, N3_HeatReleaseRaisesTheRateAtTheNextEvaluation) {
  // SECTIONS 20, 21 AND CONTROL N3 (stale temperature for the next kinetics evaluation) -- THE
  // PRIMARY P18.6 FEEDBACK TEST.
  //
  // reaction -> heat release -> T rises -> k(T) rises. No runaway or ignition is asserted; the
  // purpose is only to prove the feedback REACHES the kinetics path (section 21).
  const auto mechanism = mechanismAB();
  const auto mixture = equalCpMixture();
  const auto formation = abcFormation();
  std::vector<ReactionRateParameters> parameters;
  parameters.push_back({"A->B", ArrheniusParameters(kArrA, 0.0, kArrEa), std::nullopt});
  const ReactionKinetics kinetics(mechanism, std::move(parameters));

  const Index cells = 1;
  const Real rho = 1.0;
  const Real t0 = 1200.0;
  auto state = makeReactor(cells, {1.0, 0.0, 0.0}, rho, t0);

  // STEP 2 of the frozen order: ONE evaluation of q at (T^n, Y^n, rho^n)
  const auto rates0 = arrheniusProgressRates(mechanism, kinetics, mixture, state.composition,
                                             state.density, state.temperature);
  const Real q0 = rates0.perReaction[0][0];
  EXPECT_GT(q0, 0.0);

  // STEPS 3-7: advance species and energy, recover T from the updated composition
  const Real dt = 1.0e-2;
  state = advanceClosedReactor(mechanism, mixture, formation, rates0, state, dt);
  const Real t1 = state.temperature[0];

  // the heat release RAISED the temperature
  EXPECT_GT(t1, t0) << "T went " << t0 << " -> " << t1;

  // STEP 8: the next evaluation uses T^{n+1}
  const auto rates1 = arrheniusProgressRates(mechanism, kinetics, mixture, state.composition,
                                             state.density, state.temperature);
  const Real q1 = rates1.perReaction[0][0];
  EXPECT_GT(q1, 0.0);

  // THE RATE RESPONDED. For Ea > 0 and n = 0 the rate COEFFICIENT is strictly increasing in T, so
  // k must have risen; q also carries the concentration factor, which FELL as A was consumed, so
  // the quantitative comparison is made on k, where the prediction is unambiguous.
  const ArrheniusParameters p(kArrA, 0.0, kArrEa);
  const Real k0 = p.rateCoefficient(t0);
  const Real k1 = p.rateCoefficient(t1);
  EXPECT_GT(k1, k0) << "k went " << k0 << " -> " << k1;

  // AND THE MEASURED RESPONSE MATCHES THE ANALYTICAL PREDICTION, which is what makes this more
  // than a direction check: k1/k0 = exp(-(Ea/R)(1/T1 - 1/T0)).
  const Real predicted =
      std::exp(-(kArrEa / cfd::species::kUniversalGasConstant) * ((1.0 / t1) - (1.0 / t0)));
  EXPECT_NEAR(k1 / k0, predicted, 32.0 * kEps * predicted)
      << "measured ratio " << (k1 / k0) << " vs predicted " << predicted;

  // CONTROL N3: had the next evaluation reused T^n, the k factor would be unchanged. The
  // difference is the detector, and it is far above round-off.
  const auto staleRates = arrheniusProgressRates(mechanism, kinetics, mixture, state.composition,
                                                 state.density, ScalarField(cells, t0));
  EXPECT_NE(staleRates.perReaction[0][0], q1);
  EXPECT_GT(std::abs(q1 - staleRates.perReaction[0][0]) / std::abs(q1), 1.0e-3)
      << "stale-T chemistry differs by "
      << (100.0 * std::abs(q1 - staleRates.perReaction[0][0]) / std::abs(q1)) << "%";
}

TEST(TemperatureFeedbackTest, EndothermicFeedbackLowersTheRate) {
  // SECTION 22 -- catches a one-sided or hard-coded exothermic implementation. An endothermic
  // reaction must COOL the reactor and the Arrhenius rate must respond downward.
  std::vector<Reaction> reactions;
  reactions.push_back(reactionAC());
  const ReactionMechanism mechanism(abcRegistry(), std::move(reactions), equalCpMixture());
  const auto mixture = equalCpMixture();
  const auto formation = abcFormation();
  std::vector<ReactionRateParameters> parameters;
  parameters.push_back({"A->C", ArrheniusParameters(kArrA, 0.0, kArrEa), std::nullopt});
  const ReactionKinetics kinetics(mechanism, std::move(parameters));

  const Index cells = 1;
  const Real t0 = 1800.0;
  auto state = makeReactor(cells, {1.0, 0.0, 0.0}, 1.0, t0);

  const auto rates = arrheniusProgressRates(mechanism, kinetics, mixture, state.composition,
                                            state.density, state.temperature);
  // dt IS SIZED FROM THE RATE, not guessed. At T = 1800 the rate consumes A fast enough that
  // dt = 1e-2 would remove 1.8x the available A, and the production guard refused the step --
  // correctly. The failure is preserved at logs/03_four_failures_PRESERVED.log.
  //
  //     dt < rho Y_A / |omega_A| = rho Y_A / (M q)
  // and a tenth of that limit leaves ample margin while still moving T measurably.
  const Real omegaA = std::abs(kM * rates.perReaction[0][0]);
  const Real dtLimit = (state.density[0] * state.composition.massFraction(0, 0)) / omegaA;
  const Real dt = 0.1 * dtLimit;
  state = advanceClosedReactor(mechanism, mixture, formation, rates, state, dt);
  const Real t1 = state.temperature[0];

  EXPECT_LT(t1, t0) << "an endothermic reaction must COOL: " << t0 << " -> " << t1;

  const ArrheniusParameters p(kArrA, 0.0, kArrEa);
  EXPECT_LT(p.rateCoefficient(t1), p.rateCoefficient(t0)) << "and the rate must fall";
  const Real predicted =
      std::exp(-(kArrEa / cfd::species::kUniversalGasConstant) * ((1.0 / t1) - (1.0 / t0)));
  EXPECT_LT(predicted, 1.0);
  EXPECT_NEAR(p.rateCoefficient(t1) / p.rateCoefficient(t0), predicted, 32.0 * kEps * predicted);
}

TEST(TemperatureFeedbackTest, N7_UsingTheWrongSpeciesEnthalpyIsDetected) {
  // CONTROL N7: wrong species enthalpy. Permuting which species carries which h_f changes the
  // reaction enthalpy, hence the heat release, hence the temperature.
  const auto mechanism = mechanismAB();
  const auto mixture = equalCpMixture();
  const Index cells = 1;
  const auto correct = abcFormation();                                           // 0, -2e6, +3e6
  const auto permuted = FormationEnthalpies(abcRegistry(), {kHfB, kHfA, kHfC});  // A, B swapped

  const Real good =
      reactionHeatRelease(mechanism, mixture, correct, ratesOf({1.0}, cells), cells)[0];
  const Real bad =
      reactionHeatRelease(mechanism, mixture, permuted, ratesOf({1.0}, cells), cells)[0];
  EXPECT_DOUBLE_EQ(good, 125000.0);
  EXPECT_DOUBLE_EQ(bad, -125000.0);
  // the swap turns an exothermic reaction ENDOTHERMIC -- a sign change, not a small error
  EXPECT_LT(good * bad, 0.0);
}

// ===================================================================
// The non-reacting limit -- sections 34, 35
// ===================================================================

TEST(NonReactingLimitTest, N10_WithNoReactionTheThermalStateIsBitwiseUnchanged) {
  // SECTION 34 AND CONTROL N10 (break the non-reacting thermal identity) -- THE PRIMARY
  // BACKWARD-COMPATIBILITY TEST.
  //
  // With zero progress rates the reactor must not move AT ALL. Bitwise, not "small": section 34
  // says "Do not tolerate a chemistry-off perturbation merely because it is small".
  const auto mechanism = mechanismABandAC();
  const auto mixture = equalCpMixture();
  const auto formation = abcFormation();
  const Index cells = 32;
  const ScalarField volume(cells, 1.5);

  // a NON-UNIFORM state, so a defect cannot hide behind uniformity
  ScalarField a(cells);
  ScalarField b(cells);
  ScalarField c(cells);
  ScalarField temperature(cells);
  ScalarField density(cells);
  for (Index cell = 0; cell < cells; ++cell) {
    const Real s = static_cast<Real>(cell) / static_cast<Real>(cells);
    a[cell] = 0.25 + (0.5 * s);
    b[cell] = 0.5 - (0.25 * s);
    c[cell] = 1.0 - a[cell] - b[cell];  // the exact remainder, as P18.1 requires
    temperature[cell] = 800.0 + (20.0 * static_cast<Real>(cell));
    density[cell] = 0.75 + (0.01 * static_cast<Real>(cell));
  }
  std::vector<ScalarField> fields{a, b, c};
  const ReactorState state{MassFractionState(abcRegistry(), std::move(fields)), temperature,
                           density};

  const auto before =
      totalEnergy(mixture, formation, state.composition, state.density, state.temperature, volume);

  // ten steps at EXACTLY zero rate
  ReactorState next = state;
  for (int step = 0; step < 10; ++step) {
    next =
        advanceClosedReactor(mechanism, mixture, formation, ratesOf({0.0, 0.0}, cells), next, 1.0);
  }

  // BITWISE equality, every field, every cell
  for (Index cell = 0; cell < cells; ++cell) {
    EXPECT_EQ(next.temperature[cell], state.temperature[cell]) << "T at cell " << cell;
    EXPECT_EQ(next.density[cell], state.density[cell]) << "rho at cell " << cell;
    for (Index i = 0; i < 3; ++i) {
      EXPECT_EQ(next.composition.massFraction(i, cell), state.composition.massFraction(i, cell))
          << "Y_" << i << " at cell " << cell;
    }
  }
  const auto after =
      totalEnergy(mixture, formation, next.composition, next.density, next.temperature, volume);
  EXPECT_EQ(after.total, before.total) << "total energy must be BITWISE unchanged";
  EXPECT_EQ(after.sensible, before.sensible);
  EXPECT_EQ(after.chemical, before.chemical);

  // AND THE CONTROL IS NON-VACUOUS: the same state with a NON-zero rate does move.
  const auto moved =
      advanceClosedReactor(mechanism, mixture, formation, ratesOf({0.5, 0.0}, cells), state, 1.0);
  EXPECT_NE(moved.temperature[0], state.temperature[0])
      << "without this, the bitwise check above would pass for a stepper that does nothing";
}

TEST(NonReactingLimitTest, ZeroHeatReleaseChangesSpeciesButNotTemperature) {
  // SECTION 35, and the expected result is DOCUMENTED BEFORE THE TEST, as it requires:
  //
  //   with Delta H_r = 0 AND equal cp_i: the sensible pool rho cp T is unchanged and cp_mix is
  //   composition-independent, so T is EXACTLY unchanged while Y changes.
  //
  //   with Delta H_r = 0 and UNEQUAL cp_i: cp_mix changes, and since rho cp T is the conserved
  //   sensible quantity, T MUST move. That is a composition-induced thermodynamic effect, NOT
  //   reaction heat. The two are measured separately so they can never be confused.
  const Real shared = -750000.0;
  const auto formation = FormationEnthalpies(abcRegistry(), {shared, shared, 1234567.0});
  const Index cells = 1;
  const Real t0 = 1000.0;

  // --- equal cp: T exactly unchanged
  {
    const auto mechanism = mechanismAB();
    const auto mixture = equalCpMixture();
    const auto state = makeReactor(cells, {1.0, 0.0, 0.0}, 1.0, t0);
    const Real source =
        reactionHeatRelease(mechanism, mixture, formation, ratesOf({8.0}, cells), cells)[0];
    EXPECT_EQ(source, 0.0) << "thermoneutral: the explicit heat source is EXACTLY zero";
    const auto next =
        advanceClosedReactor(mechanism, mixture, formation, ratesOf({8.0}, cells), state, 1.0);
    EXPECT_NEAR(next.composition.massFraction(1, 0), 0.5, 8.0 * kEps) << "species DID change";
    EXPECT_EQ(next.temperature[0], t0) << "and T is BITWISE unchanged";
  }

  // --- unequal cp: T moves, and it is the cp change, not reaction heat
  {
    std::vector<SpeciesThermodynamics> species;
    species.push_back(SpeciesThermodynamics(kM, constantCp(1000.0, "cp_A")));
    species.push_back(SpeciesThermodynamics(kM, constantCp(2500.0, "cp_B")));
    species.push_back(SpeciesThermodynamics(kM, constantCp(1000.0, "cp_C")));
    const MixtureThermodynamics unequal(abcRegistry(), std::move(species));
    std::vector<Reaction> reactions;
    reactions.push_back(reactionAB());
    const ReactionMechanism mechanism(abcRegistry(), std::move(reactions), unequal);
    const auto state = makeReactor(cells, {1.0, 0.0, 0.0}, 1.0, t0);
    const Real source =
        reactionHeatRelease(mechanism, unequal, formation, ratesOf({8.0}, cells), cells)[0];
    EXPECT_EQ(source, 0.0) << "still EXACTLY zero reaction heat";
    const auto next =
        advanceClosedReactor(mechanism, unequal, formation, ratesOf({8.0}, cells), state, 1.0);
    EXPECT_NE(next.temperature[0], t0) << "but T moves, because cp_mix changed";
    // and it moves by exactly what rho cp T = const predicts: T1 = T0 cp0/cp1
    const Real cp0 = 1000.0;
    const Real cp1 = 1750.0;  // 0.5*1000 + 0.5*2500
    EXPECT_NEAR(next.temperature[0], t0 * cp0 / cp1, 1.0e-9 * t0)
        << "T1 = " << next.temperature[0] << " vs the predicted " << (t0 * cp0 / cp1);
  }
}

// ===================================================================
// Determinism and invalid states -- sections 40, 48
// ===================================================================

TEST(HeatReleaseDeterminismTest, TheWholeCoupledStepIsBitwiseDeterministic) {
  const Index cells = 64;
  const ScalarField volume(cells, 1.25);

  ScalarField temperature(cells);
  ScalarField density(cells);
  for (Index cell = 0; cell < cells; ++cell) {
    temperature[cell] = 900.0 + (10.0 * static_cast<Real>(cell));
    density[cell] = 0.8 + (0.005 * static_cast<Real>(cell));
  }
  const ReactorState start{MassFractionState(abcRegistry(), cells, {0.6, 0.3, 0.1}), temperature,
                           density};
  const auto rates = ratesOf({0.4, 0.1}, cells);

  // EVERYTHING IS REBUILT ON EVERY REPEAT, so mechanism construction, formation-enthalpy binding
  // and mixture construction are all inside the determinism claim. Reusing one object would hide
  // an order-dependent construction completely.
  auto run = [&]() {
    const auto m = mechanismABandAC();
    const auto mix = equalCpMixture();
    const auto f = abcFormation();
    const auto step = advanceClosedReactor(m, mix, f, rates, start, 1.0e-3);
    const auto heat = reactionHeatRelease(m, mix, f, rates, cells);
    const auto omega = reactionSpeciesSources(m, mix, rates, cells);
    const auto energy =
        totalEnergy(mix, f, step.composition, step.density, step.temperature, volume);
    return std::make_tuple(step, heat, omega, energy);
  };

  const auto first = run();
  for (int repeat = 0; repeat < 50; ++repeat) {
    const auto again = run();
    const auto& step = std::get<0>(again);
    const auto& heat = std::get<1>(again);
    const auto& omega = std::get<2>(again);
    const auto& energy = std::get<3>(again);
    for (Index cell = 0; cell < cells; ++cell) {
      EXPECT_EQ(heat[cell], std::get<1>(first)[cell])
          << "S_E, repeat " << repeat << " cell " << cell;
      EXPECT_EQ(step.temperature[cell], std::get<0>(first).temperature[cell])
          << "T, repeat " << repeat << " cell " << cell;
      EXPECT_EQ(step.density[cell], std::get<0>(first).density[cell]);
      for (Index i = 0; i < 3; ++i) {
        EXPECT_EQ(step.composition.massFraction(i, cell),
                  std::get<0>(first).composition.massFraction(i, cell));
        EXPECT_EQ(omega.perSpecies[i].Su[cell], std::get<2>(first).perSpecies[i].Su[cell]);
      }
    }
    EXPECT_EQ(energy.total, std::get<3>(first).total) << "total energy, repeat " << repeat;
    EXPECT_EQ(energy.sensible, std::get<3>(first).sensible);
    EXPECT_EQ(energy.chemical, std::get<3>(first).chemical);
  }
}

TEST(HeatReleaseDeterminismTest, RejectsMismatchedSizesAndNonFiniteInput) {
  // Section 40: every invalid thermochemical state is refused, naming the quantity.
  const auto mechanism = mechanismAB();
  const auto mixture = equalCpMixture();
  const auto formation = abcFormation();
  const Index cells = 4;

  // a non-finite progress rate
  ReactionProgressRates bad;
  bad.perReaction.push_back(ScalarField(cells, std::numeric_limits<Real>::quiet_NaN()));
  EXPECT_THROW(static_cast<void>(reactionHeatRelease(mechanism, mixture, formation, bad, cells)),
               InvalidArgumentError);
  // a wrong reaction count
  EXPECT_THROW(static_cast<void>(reactionHeatRelease(mechanism, mixture, formation,
                                                     ratesOf({1.0, 2.0}, cells), cells)),
               InvalidArgumentError);
  // a wrong cell count
  EXPECT_THROW(static_cast<void>(reactionHeatRelease(mechanism, mixture, formation,
                                                     ratesOf({1.0}, cells), cells + 1)),
               InvalidArgumentError);
  // a bad dt
  const auto state = makeReactor(cells, {1.0, 0.0, 0.0}, 1.0, 1000.0);
  for (const Real dt : {0.0, -1.0, std::numeric_limits<Real>::infinity()}) {
    EXPECT_THROW(static_cast<void>(advanceClosedReactor(mechanism, mixture, formation,
                                                        ratesOf({1.0}, cells), state, dt)),
                 InvalidArgumentError)
        << "dt = " << dt;
  }
  // an invalid energy-density input
  EXPECT_THROW(static_cast<void>(energyDensity(mixture, formation, {1.0, 0.0, 0.0}, 1.0, -5.0)),
               InvalidArgumentError);
  EXPECT_THROW(static_cast<void>(energyDensity(mixture, formation, {1.0, 0.0, 0.0}, 0.0, 1000.0)),
               InvalidArgumentError);
  // a mismatched formation-enthalpy count against the species sources
  const auto sources = reactionSpeciesSources(mechanism, mixture, ratesOf({1.0}, cells), cells);
  const auto twoSpecies = FormationEnthalpies(SpeciesRegistry({"A", "B"}), {0.0, 1.0});
  EXPECT_THROW(static_cast<void>(reactionHeatReleaseFromSpeciesSources(twoSpecies, sources, cells)),
               InvalidArgumentError);
}
