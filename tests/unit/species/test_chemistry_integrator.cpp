// G8 closure, step 3 -- the chemistry integrator.
//
// WHAT IS BEING TESTED AND HOW IT IS GATED. The integrator sub-divides P18.6's qualified explicit
// step so a transport-sized dt can be integrated through chemistry that is far stiffer than that
// step accepts. Three things must hold, and they are gated differently on purpose:
//
//   1. IT MUST NOT CHANGE THE ANSWER. Where P18.6's single step already works, the integrator must
//      reproduce it BITWISE for a dt it accepts in one go. Exact equality, no tolerance.
//   2. IT MUST BE ACCURATE, and accuracy is established by REFINEMENT, not by an invented absolute
//      bound: tightening maximumFractionalChange must reduce the error against the independent
//      reference. This is the same criterion P18.7 froze for the batch reactor, for the same
//      reason.
//   3. IT MUST NOT REPAIR ANYTHING. A refusal is answered with a smaller step, never a clipped or
//      renormalised state, and exhausting the budget THROWS rather than returning a partial
//      advance.
//
// THE REFERENCE is P18.7's, restated here as literal values: mpmath at 50 digits, quadrature plus
// bisection, NOT a time integration, so it shares no algorithm with this explicit march.
//   results/p18/conservation_validation/data/reference_values.txt
#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

#include "cfd/core/Exception.hpp"
#include "cfd/species/ArrheniusKinetics.hpp"
#include "cfd/species/ChemistryIntegrator.hpp"
#include "cfd/species/HeatRelease.hpp"
#include "cfd/species/MassFractionState.hpp"
#include "cfd/species/MixtureThermodynamics.hpp"
#include "cfd/species/ReactionMechanism.hpp"
#include "cfd/species/SpeciesRegistry.hpp"
#include "cfd/thermal/Enthalpy.hpp"
#include "cfd/thermal/TemperatureDependentProperty.hpp"

using cfd::Index;
using cfd::Real;
using cfd::species::ChemistryIntegratorSettings;
using cfd::species::FormationEnthalpies;
using cfd::species::MassFractionState;
using cfd::species::MixtureThermodynamics;
using cfd::species::Reaction;
using cfd::species::ReactionDirection;
using cfd::species::ReactionKinetics;
using cfd::species::ReactionMechanism;
using cfd::species::ReactionRateParameters;
using cfd::species::ReactionTerm;
using cfd::species::ReactorState;
using cfd::species::SpeciesRegistry;
using cfd::species::SpeciesThermodynamics;

namespace {

// ---- P18.7's reference problem, exactly: A -> B, both M = 0.0625, cp = 1000, h_f,B = -2e6,
// A = 5, n = 0, Ea = 30000, rho = 1, T_0 = 1000.
constexpr Real kWeight = 0.0625;
constexpr Real kSpecificHeat = 1000.0;
constexpr Real kFormationB = -2.0e6;
constexpr Real kPreExponential = 5.0;
constexpr Real kActivationEnergy = 30000.0;
constexpr Real kDensity = 1.0;
constexpr Real kInitialTemperature = 1000.0;

// The reference table, verbatim from P18.7's data/reference_values.txt (mpmath, dps=50).
struct ReferencePoint {
  Real time;
  Real massFractionA;
  Real temperature;
};
constexpr ReferencePoint kReference[] = {
    {0.05, 0.99307948811552110, 1013.8410237689578}, {0.1, 0.98585848306055263, 1028.2830338788947},
    {0.2, 0.97045151039151934, 1059.0969792169613},  {0.5, 0.91527271443601291, 1169.4545711279742},
    {1.0, 0.78521237887865421, 1429.5752422426916},
};

SpeciesThermodynamics makeSpecies(Real weight, Real specificHeat, const std::string& name) {
  return SpeciesThermodynamics(
      weight,
      cfd::thermal::SensibleEnthalpy(
          cfd::thermal::TemperatureDependentProperty::constant(specificHeat, name), 298.15, 0.0));
}

struct Problem {
  SpeciesRegistry registry;
  MixtureThermodynamics mixture;
  FormationEnthalpies formation;
  ReactionMechanism mechanism;
  ReactionKinetics kinetics;
};

Problem makeProblem() {
  SpeciesRegistry registry({"A", "B"});
  std::vector<SpeciesThermodynamics> thermo;
  thermo.push_back(makeSpecies(kWeight, kSpecificHeat, "cp_A"));
  thermo.push_back(makeSpecies(kWeight, kSpecificHeat, "cp_B"));
  MixtureThermodynamics mixture(registry, std::move(thermo));

  // h_f per MOLE for fromMolar: hbar = M * h.
  FormationEnthalpies formation =
      FormationEnthalpies::fromMolar(mixture, {0.0, kFormationB * kWeight});

  std::vector<Reaction> reactions;
  reactions.push_back(Reaction("A->B", registry, {{"A", 1.0}}, {{"B", 1.0}}));
  ReactionMechanism mechanism(registry, std::move(reactions), mixture);

  std::vector<ReactionRateParameters> parameters;
  parameters.push_back(ReactionRateParameters{
      "A->B", cfd::species::ArrheniusParameters(kPreExponential, 0.0, kActivationEnergy),
      std::nullopt});
  ReactionKinetics kinetics(mechanism, std::move(parameters));

  return Problem{registry, std::move(mixture), std::move(formation), std::move(mechanism),
                 std::move(kinetics)};
}

ReactorState initialState(const SpeciesRegistry& registry, Index cells) {
  return ReactorState{MassFractionState(registry, cells, {1.0, 0.0}),
                      cfd::fields::ScalarField(cells, kInitialTemperature),
                      cfd::fields::ScalarField(cells, kDensity)};
}

}  // namespace

// =================================================================================================
// 1. It must not change the answer where P18.6's step already works
// =================================================================================================

TEST(G8ChemistryIntegratorTest, ASingleAcceptableStepIsBITWISEP18_6sOwnStep) {
  Problem problem = makeProblem();
  const Index cells = 4;
  const ReactorState start = initialState(problem.registry, cells);

  // A dt small enough that the fractional-change limit does not bite, so the integrator should take
  // exactly ONE sub-step and that sub-step must be P18.6's step on the same inputs.
  const Real dt = 1.0e-4;

  const auto rates =
      cfd::species::arrheniusProgressRates(problem.mechanism, problem.kinetics, problem.mixture,
                                           start.composition, start.density, start.temperature);
  const ReactorState direct = cfd::species::advanceClosedReactor(
      problem.mechanism, problem.mixture, problem.formation, rates, start, dt);

  const auto integrated = cfd::species::integrateChemistry(
      problem.mechanism, problem.mixture, problem.formation, problem.kinetics, start, dt);

  EXPECT_EQ(integrated.subSteps, 1u) << "a dt inside the chemical limit must not be subdivided";
  EXPECT_EQ(integrated.refusals, 0u);
  for (Index i = 0; i < 2; ++i) {
    for (Index cell = 0; cell < cells; ++cell) {
      // BITWISE. A tolerance here would hide the integrator quietly doing something else.
      EXPECT_EQ(integrated.state.composition.massFraction(i, cell),
                direct.composition.massFraction(i, cell))
          << "species " << i << " cell " << cell;
    }
  }
  for (Index cell = 0; cell < cells; ++cell) {
    EXPECT_EQ(integrated.state.temperature[cell], direct.temperature[cell]) << "cell " << cell;
  }
}

// =================================================================================================
// 2. It integrates a dt P18.6's step REFUSES -- the whole reason it exists
// =================================================================================================

TEST(G8ChemistryIntegratorTest, ItIntegratesADtThatTheSingleQualifiedStepRefuses) {
  Problem problem = makeProblem();
  const Index cells = 2;
  const ReactorState start = initialState(problem.registry, cells);

  // A transport-sized dt: large enough that one explicit step would consume far more A than exists.
  const Real dt = 20.0;

  const auto rates =
      cfd::species::arrheniusProgressRates(problem.mechanism, problem.kinetics, problem.mixture,
                                           start.composition, start.density, start.temperature);
  // NON-VACUITY: the single step really must refuse this, or the test proves nothing.
  //
  // NumericalError, not InvalidArgumentError: the refusal is about the resulting STATE, not the
  // arguments. Asserting the wrong type here is what revealed that the integrator's retry branch
  // caught InvalidArgumentError and was therefore dead code.
  EXPECT_THROW(static_cast<void>(cfd::species::advanceClosedReactor(
                   problem.mechanism, problem.mixture, problem.formation, rates, start, dt)),
               cfd::NumericalError);

  const auto integrated = cfd::species::integrateChemistry(
      problem.mechanism, problem.mixture, problem.formation, problem.kinetics, start, dt);

  std::cout << "    dt = " << dt << " integrated in " << integrated.subSteps << " sub-steps, "
            << integrated.refusals << " refusals, smallest " << integrated.smallestSubStep
            << " s, worst fractional change " << integrated.worstFractionalChange << "\n";

  EXPECT_GT(integrated.subSteps, 1u);
  // The state is valid and the reaction ran essentially to completion.
  for (Index cell = 0; cell < cells; ++cell) {
    const Real a = integrated.state.composition.massFraction(0, cell);
    const Real b = integrated.state.composition.massFraction(1, cell);
    EXPECT_GE(a, 0.0);
    EXPECT_LE(a, 1.0);
    // TD-002 TIGHTENED THIS from 1.0e-14 to P18.1's own bound. The old tolerance was 5.6x looser
    // than the 4*eps*2 that MassFractionState enforces on every sub-step, so it asserted less than
    // the code already guaranteed -- and TD-002's audit found it while looking for why a loosened
    // sum tolerance was in the suite at all (reproducer.md, "why the existing suite is green").
    // Loosening this is forbidden; tightening it to the frozen invariant is the point of the item.
    EXPECT_LE(std::abs((a + b) - 1.0), cfd::species::massFractionSumTolerance(2))
        << "sum-to-one must hold to P18.1's invariant without normalisation, cell " << cell;
    EXPECT_LT(a, 0.01) << "A should be nearly consumed after 20 s";
    EXPECT_GT(integrated.state.temperature[cell], 2900.0)
        << "the adiabatic complete-conversion temperature is 3000 K";
  }
}

// =================================================================================================
// 3. ACCURACY BY REFINEMENT against the independent mpmath reference
// =================================================================================================

TEST(G8ChemistryIntegratorTest, TighteningTheStepControlReducesTheErrorAgainstTheReference) {
  Problem problem = makeProblem();
  const Index cells = 1;

  // Three successively tighter stability controls. The GATE is that the error FALLS; no absolute
  // bound is invented here, for the reason P18.7 froze refinement as its criterion.
  const Real controls[] = {0.2, 0.05, 0.0125};
  std::vector<Real> worstSpeciesError;
  std::vector<Real> worstTemperatureError;

  for (const Real control : controls) {
    ChemistryIntegratorSettings settings;
    settings.maximumFractionalChange = control;

    Real species = 0.0;
    Real temperature = 0.0;
    ReactorState state = initialState(problem.registry, cells);
    Real previousTime = 0.0;
    for (const ReferencePoint& point : kReference) {
      const auto step = cfd::species::integrateChemistry(problem.mechanism, problem.mixture,
                                                         problem.formation, problem.kinetics, state,
                                                         point.time - previousTime, settings);
      state = step.state;
      previousTime = point.time;

      species =
          std::max(species, std::abs(state.composition.massFraction(0, 0) - point.massFractionA) /
                                point.massFractionA);
      temperature = std::max(
          temperature, std::abs(state.temperature[0] - point.temperature) / point.temperature);
    }
    worstSpeciesError.push_back(species);
    worstTemperatureError.push_back(temperature);
    std::cout << "    control " << control << " -> worst relative error: Y_A " << species << ", T "
              << temperature << "\n";
  }

  // Monotone reduction, checked pairwise rather than only end-to-end: a non-monotone sequence that
  // happened to end lower would otherwise pass.
  for (std::size_t i = 1; i < worstSpeciesError.size(); ++i) {
    EXPECT_LT(worstSpeciesError[i], worstSpeciesError[i - 1])
        << "Y_A error did not fall from control " << controls[i - 1] << " to " << controls[i];
    EXPECT_LT(worstTemperatureError[i], worstTemperatureError[i - 1])
        << "T error did not fall from control " << controls[i - 1] << " to " << controls[i];
  }

  // NON-VACUITY: the coarsest control must have a MEASURABLE error, or "it fell" is a statement
  // about round-off.
  EXPECT_GT(worstSpeciesError.front(), 1.0e-6)
      << "the coarsest control produced an error of " << worstSpeciesError.front()
      << ", which is too small for the refinement claim to mean anything";
}

// =================================================================================================
// 4. The non-reacting limit must be cheap AND exact
// =================================================================================================

TEST(G8ChemistryIntegratorTest, WithNothingReactingItTakesOneSubStepAndChangesNothing) {
  Problem problem = makeProblem();
  const Index cells = 3;
  // A = 0 means no reaction anywhere. The chemical time scale is then infinite, so there is no
  // restriction and the whole dt goes in one sub-step -- not thousands of pointless ones.
  std::vector<ReactionRateParameters> parameters;
  parameters.push_back(ReactionRateParameters{
      "A->B", cfd::species::ArrheniusParameters(0.0, 0.0, kActivationEnergy), std::nullopt});
  ReactionKinetics zeroKinetics(problem.mechanism, std::move(parameters));

  const ReactorState start = initialState(problem.registry, cells);
  const auto integrated = cfd::species::integrateChemistry(
      problem.mechanism, problem.mixture, problem.formation, zeroKinetics, start, 1.0e6);

  EXPECT_EQ(integrated.subSteps, 1u) << "an inert mixture must not be sub-stepped";
  EXPECT_EQ(integrated.refusals, 0u);
  for (Index cell = 0; cell < cells; ++cell) {
    // BITWISE unchanged -- the zero-rate limit P18.6 established, preserved through the integrator.
    EXPECT_EQ(integrated.state.composition.massFraction(0, cell), 1.0);
    EXPECT_EQ(integrated.state.composition.massFraction(1, cell), 0.0);
    EXPECT_EQ(integrated.state.temperature[cell], kInitialTemperature);
  }
}

// =================================================================================================
// 5. The sub-steps must sum to EXACTLY the requested dt
// =================================================================================================

TEST(G8ChemistryIntegratorTest, TheStateAdvancesByExactlyTheRequestedDtNotApproximately) {
  Problem problem = makeProblem();
  const Index cells = 1;

  // Integrating dt in one call must match integrating it as two halves in sequence, to the accuracy
  // the control allows. If the integrator silently under- or over-shot dt, the two would diverge
  // systematically -- and every conservation balance built on "the state advanced by dt" would be
  // wrong by that amount.
  ChemistryIntegratorSettings settings;
  settings.maximumFractionalChange = 0.01;

  const Real dt = 0.4;
  const ReactorState start = initialState(problem.registry, cells);

  const auto whole = cfd::species::integrateChemistry(
      problem.mechanism, problem.mixture, problem.formation, problem.kinetics, start, dt, settings);
  const auto firstHalf =
      cfd::species::integrateChemistry(problem.mechanism, problem.mixture, problem.formation,
                                       problem.kinetics, start, 0.5 * dt, settings);
  const auto secondHalf =
      cfd::species::integrateChemistry(problem.mechanism, problem.mixture, problem.formation,
                                       problem.kinetics, firstHalf.state, 0.5 * dt, settings);

  const Real wholeA = whole.state.composition.massFraction(0, 0);
  const Real splitA = secondHalf.state.composition.massFraction(0, 0);
  std::cout << "    one call: Y_A = " << wholeA << " in " << whole.subSteps << " sub-steps\n"
            << "    two calls: Y_A = " << splitA << " in " << firstHalf.subSteps << " + "
            << secondHalf.subSteps << " sub-steps\n";

  // They are not required to be bitwise -- the sub-step sequence differs -- but they must agree far
  // more closely than the march's own error, or dt is not being honoured.
  EXPECT_NEAR(wholeA, splitA, 1.0e-6 * wholeA);
}

// =================================================================================================
// 6. It refuses rather than repairs, and refuses rather than partially advancing
// =================================================================================================

TEST(G8ChemistryIntegratorTest,
     ExhaustingTheSubStepBudgetThrowsRatherThanReturningAPartialAdvance) {
  Problem problem = makeProblem();
  const ReactorState start = initialState(problem.registry, 1);

  ChemistryIntegratorSettings settings;
  settings.maximumFractionalChange = 1.0e-4;  // forces very many sub-steps
  settings.maximumSubSteps = 10;              // and a budget far below what is needed

  // A caller that received a partially advanced state believing it got the full dt would corrupt
  // every balance downstream, so this must THROW, not truncate.
  //
  // NumericalError: running out of sub-steps is a numerical failure, not a bad argument. The
  // distinction matters to a caller that wants to retry with a different dt rather than treat its
  // own inputs as invalid.
  EXPECT_THROW(static_cast<void>(cfd::species::integrateChemistry(
                   problem.mechanism, problem.mixture, problem.formation, problem.kinetics, start,
                   10.0, settings)),
               cfd::NumericalError);
}

// =================================================================================================
// 6b. THE REFUSAL MODE THE LIMITER MAKES UNREACHABLE -- measured, not assumed
// =================================================================================================

TEST(G8ChemistryIntegratorTest,
     TheNegativeMassRefusalIsUnreachableUnderTheLimiterEvenAtFullControl) {
  Problem problem = makeProblem();
  const Index cells = 1;
  const ReactorState start = initialState(problem.registry, cells);

  // maximumFractionalChange = 1.0 is the most permissive setting allowed. The sub-step is then
  // exactly rho Y / |omega|, and the qualified step applies the SAME frozen rates, so the update
  // lands on rho Y = 0 and cannot pass it. If that reasoning were wrong, refusals would appear
  // here.
  ChemistryIntegratorSettings settings;
  settings.maximumFractionalChange = 1.0;

  const auto integrated =
      cfd::species::integrateChemistry(problem.mechanism, problem.mixture, problem.formation,
                                       problem.kinetics, start, 20.0, settings);

  std::cout << "    control 1.0: " << integrated.subSteps << " sub-steps, " << integrated.refusals
            << " refusals, worst fractional change " << integrated.worstFractionalChange << "\n";

  EXPECT_EQ(integrated.refusals, 0u)
      << "the negative-mass refusal is supposed to be unreachable under the limiter; "
      << integrated.refusals << " refusals means that reasoning is wrong";
  // and the state is still valid, which is the point of the bound
  EXPECT_GE(integrated.state.composition.massFraction(0, 0), 0.0);
  EXPECT_LE(integrated.state.composition.massFraction(0, 0), 1.0);

  // The retry path is therefore DEFENSIVE AND UNEXERCISED by this mechanism. Its exception type is
  // pinned here so the dead-code bug that caught the wrong type cannot return: a refusal from the
  // qualified step must be a NumericalError, which is what the integrator catches.
  const auto rates =
      cfd::species::arrheniusProgressRates(problem.mechanism, problem.kinetics, problem.mixture,
                                           start.composition, start.density, start.temperature);
  EXPECT_THROW(static_cast<void>(cfd::species::advanceClosedReactor(
                   problem.mechanism, problem.mixture, problem.formation, rates, start, 1.0e6)),
               cfd::NumericalError);
}

// =================================================================================================
// TD-002 -- the round-off carry, the long march, and the raw-fields entry point
// =================================================================================================

// A REVERSIBLE mechanism, because the irreversible one above is DEGENERATE for round-off: its fuel
// decays to ~1e-323 and its product rises to ~1, where every addition is exact. Measured in
// TD-002's audit: 7521 sub-steps of the irreversible problem drift 0.00 eps even uncompensated, so
// a test built on it would assert nothing about the carry.
Problem makeReversibleProblem() {
  SpeciesRegistry registry({"A", "B"});
  std::vector<SpeciesThermodynamics> thermo;
  thermo.push_back(makeSpecies(kWeight, kSpecificHeat, "cp_A"));
  thermo.push_back(makeSpecies(kWeight, kSpecificHeat, "cp_B"));
  MixtureThermodynamics mixture(registry, std::move(thermo));
  FormationEnthalpies formation =
      FormationEnthalpies::fromMolar(mixture, {0.0, kFormationB * kWeight});

  std::vector<Reaction> reactions;
  reactions.push_back(
      Reaction("A<->B", registry, {{"A", 1.0}}, {{"B", 1.0}}, ReactionDirection::Reversible));
  ReactionMechanism mechanism(registry, std::move(reactions), mixture);

  std::vector<ReactionRateParameters> parameters;
  parameters.push_back(ReactionRateParameters{
      "A<->B", cfd::species::ArrheniusParameters(kPreExponential, 0.0, kActivationEnergy),
      cfd::species::ArrheniusParameters(kPreExponential, 0.0, kActivationEnergy)});
  ReactionKinetics kinetics(mechanism, std::move(parameters));
  return Problem{registry, std::move(mixture), std::move(formation), std::move(mechanism),
                 std::move(kinetics)};
}

TEST(G8ChemistryIntegratorTest, TD002_ALongMarchKeepsSumToOneInsideP18_1sInvariant) {
  Problem problem = makeReversibleProblem();
  const ReactorState begin = initialState(problem.registry, 1);

  ChemistryIntegratorSettings settings;
  settings.maximumFractionalChange = 1.0e-4;  // drives tens of thousands of sub-steps
  settings.maximumSubSteps = 5000000;

  const auto marched =
      cfd::species::integrateChemistry(problem.mechanism, problem.mixture, problem.formation,
                                       problem.kinetics, begin, 600.0, settings);

  std::cout << "    " << marched.subSteps
            << " sub-steps, worst |sum Y - 1| = " << marched.worstSumDeviation << " ("
            << marched.worstSumDeviation / std::numeric_limits<Real>::epsilon() << " eps)\n";

  // The gate's own threshold: a march far beyond where the plain update is refused (~1333 steps).
  EXPECT_GE(marched.subSteps, 30000u) << "the march must be long enough to exercise the carry";
  EXPECT_LE(marched.worstSumDeviation, cfd::species::massFractionSumTolerance(2));
  for (Index i = 0; i < 2; ++i) {
    EXPECT_GE(marched.state.composition.massFraction(i, 0), 0.0);
  }
}

// THE NON-VACUITY CONTROL for the test above, and it is one settings flip rather than a second
// build -- so the control and production run the same binary.
TEST(G8ChemistryIntegratorTest, TD002_N1_WithoutTheCarryTheSameMarchIsREFUSED) {
  Problem problem = makeReversibleProblem();
  const ReactorState begin = initialState(problem.registry, 1);

  ChemistryIntegratorSettings settings;
  settings.compensateRoundOff = false;  // the pre-TD-002 arithmetic
  settings.maximumFractionalChange = 1.0e-4;
  settings.maximumSubSteps = 5000000;

  // It fails by REFUSAL, not by returning a drifted answer: MassFractionState validates every
  // sub-step, so the march stops the moment sum(Y) leaves the invariant. That is why the defect
  // survived a green suite -- there was never a wrong number to inspect.
  EXPECT_THROW(static_cast<void>(cfd::species::integrateChemistry(
                   problem.mechanism, problem.mixture, problem.formation, problem.kinetics, begin,
                   600.0, settings)),
               cfd::InvalidArgumentError);
}

TEST(G8ChemistryIntegratorTest, TD002_TheAccuracyControlCanBeTightenedWithoutFailing) {
  Problem problem = makeProblem();
  std::size_t previous = 0;
  for (const Real control : {0.1, 0.0125, 0.003125, 0.00078125}) {
    ChemistryIntegratorSettings settings;
    settings.maximumFractionalChange = control;
    settings.maximumSubSteps = 2000000;

    // Before TD-002 every rung below 0.05 THREW on this exact call (reproducer.md V1). An accuracy
    // control that cannot be tightened is not an accuracy control.
    const auto marched = cfd::species::integrateChemistry(
        problem.mechanism, problem.mixture, problem.formation, problem.kinetics,
        initialState(problem.registry, 1), 20.0, settings);
    EXPECT_LE(marched.worstSumDeviation, cfd::species::massFractionSumTolerance(2))
        << "control " << control;
    // Monotone rise, so a control that were accepted and then ignored would fail here rather than
    // pass by doing nothing.
    EXPECT_GT(marched.subSteps, previous) << "control " << control << " did not refine the march";
    previous = marched.subSteps;
  }
}

TEST(G8ChemistryIntegratorTest, TD002_TheRawFieldsPathCARRIESATransportClosureRatherThanErasingIt) {
  Problem problem = makeProblem();
  const Index cells = 2;

  // TD-001's MEASURED transport closure. MassFractionState refuses this outright, which is why a
  // march could not previously start from a transported composition at all.
  const Real deviationIn = 5.718197704229766e-08;
  std::vector<cfd::fields::ScalarField> fields;
  fields.emplace_back(cells, 0.75);
  fields.emplace_back(cells, 0.25 + deviationIn);
  const cfd::fields::ScalarField temperature(cells, kInitialTemperature);
  const cfd::fields::ScalarField density(cells, kDensity);

  // NON-VACUITY: the validated path really must refuse this input, or the test proves nothing.
  EXPECT_THROW(
      static_cast<void>(MassFractionState(problem.registry, cells, {0.75, 0.25 + deviationIn})),
      cfd::InvalidArgumentError);

  const auto marched = cfd::species::integrateChemistryFromFields(
      problem.mechanism, problem.mixture, problem.formation, problem.kinetics, problem.registry,
      fields, temperature, density, 1.0);

  std::cout << "    deviation in " << marched.worstSumDeviationIn << ", out "
            << marched.worstSumDeviationOut << ", ratio "
            << marched.worstSumDeviationOut / marched.worstSumDeviationIn << "\n";

  // THE ANTI-NORMALISATION ASSERTION. A normalising implementation would return ~eps here -- nine
  // orders below this bound -- so this is what makes "it does not normalise" a measurement instead
  // of a claim. The inherited closure is the FLOW solver's residual continuity error (TD-001 root
  // caused it); erasing it would make every downstream conservation figure meaningless.
  EXPECT_GE(marched.worstSumDeviationOut, 0.5 * marched.worstSumDeviationIn);
  EXPECT_GT(marched.worstSumDeviationIn, 1.0e-9) << "the input closure must be the one measured";
  EXPECT_GE(marched.minMassFraction, 0.0);
  EXPECT_GT(marched.subSteps, 0u);
}

TEST(G8ChemistryIntegratorTest, InvalidArgumentsAreRefused) {
  Problem problem = makeProblem();
  const ReactorState start = initialState(problem.registry, 1);

  for (const Real badDt :
       {0.0, -1.0, std::numeric_limits<Real>::quiet_NaN(), std::numeric_limits<Real>::infinity()}) {
    EXPECT_THROW(
        static_cast<void>(cfd::species::integrateChemistry(
            problem.mechanism, problem.mixture, problem.formation, problem.kinetics, start, badDt)),
        cfd::InvalidArgumentError);
  }
  for (const Real badControl : {0.0, -0.1, 1.5}) {
    ChemistryIntegratorSettings settings;
    settings.maximumFractionalChange = badControl;
    EXPECT_THROW(static_cast<void>(cfd::species::integrateChemistry(
                     problem.mechanism, problem.mixture, problem.formation, problem.kinetics, start,
                     1.0e-3, settings)),
                 cfd::InvalidArgumentError);
  }
}

// =================================================================================================
// 7. The chemical time scale is reported, and reports the non-reacting case distinctly
// =================================================================================================

TEST(G8ChemistryIntegratorTest, TheChemicalTimeScaleIsReportedAndDistinguishesAnInertMixture) {
  Problem problem = makeProblem();
  const Index cells = 1;
  const ReactorState start = initialState(problem.registry, cells);

  const auto rates =
      cfd::species::arrheniusProgressRates(problem.mechanism, problem.kinetics, problem.mixture,
                                           start.composition, start.density, start.temperature);
  const auto scale =
      cfd::species::chemicalTimeScale(problem.mechanism, problem.mixture, rates, start);
  std::cout << "    tau = " << scale.smallest << " s (cell " << scale.cell << ", species "
            << scale.species << ")\n";
  EXPECT_TRUE(scale.anyReaction);
  EXPECT_GT(scale.smallest, 0.0);
  EXPECT_TRUE(std::isfinite(scale.smallest));
  // k(1000 K) = 0.1355..., so tau = rho Y_A / |omega_A| is of order 1/k ~ 7 s.
  EXPECT_NEAR(scale.smallest, 1.0 / 0.13550689710042101, 0.5);

  // An inert mixture must be reported as having NO reaction, not as having a very large time
  // scale -- otherwise "nothing reacts" and "everything is slow" are indistinguishable.
  const auto zeroRates = cfd::species::zeroProgressRates(problem.mechanism.reactionCount(), cells);
  const auto inert =
      cfd::species::chemicalTimeScale(problem.mechanism, problem.mixture, zeroRates, start);
  EXPECT_FALSE(inert.anyReaction);
  EXPECT_FALSE(std::isfinite(inert.smallest));
}
