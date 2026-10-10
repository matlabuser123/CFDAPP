// G8 closure, step 4 -- the steady 1D premixed flame solver.
//
// WHY EVERY TEST HERE USES A SYNTHETIC MECHANISM, and none uses methane.
//
// G8's gated quantity is the laminar burning velocity of stoichiometric methane-air, against a band
// frozen and cited before any measurement. Two of that measurement's inputs -- the constant cp and
// the lambda(T) law -- are not yet frozen. If the solver were debugged against methane, every
// decision taken while looking at a methane flame speed would be a decision taken with the answer
// visible, and the gate would be measuring the tuning rather than the code. That is the single
// failure mode g8_reference.md section 2 calls "the integrity crux of the whole gate".
//
// So the solver is qualified HERE on a synthetic fuel F -> P, and the methane measurement is made
// ONCE, afterwards, against inputs frozen in between.
//
// WHAT IS GATED, and how. There is no published flame speed for a made-up fuel, so none of these
// tests asserts an absolute S_L. They assert the PROPERTIES a correct eigenvalue solver must have,
// each of which fails loudly for a plausible wrong implementation:
//
//   * the eigenvalue is independent of the initial guess        <- the degeneracy trap
//   * the eigenvalue plateaus in the anchor position and domain <- the cold-boundary difficulty
//   * S_L converges under mesh refinement
//   * the consumption integral agrees with mdot/rho_u           <- discrete conservation
//   * the peak temperature approaches the independent T_ad
//   * reactions off => no flame at all
//   * a 10x pre-exponential moves S_L far
//   * lambda(T) is actually consumed, not silently frozen at one value
#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <iostream>
#include <string>
#include <vector>

#include "cfd/core/Exception.hpp"
#include "cfd/physics/TemperatureProperty.hpp"
#include "cfd/species/ArrheniusKinetics.hpp"
#include "cfd/species/HeatRelease.hpp"
#include "cfd/species/MixtureThermodynamics.hpp"
#include "cfd/species/PremixedFlame.hpp"
#include "cfd/species/ReactionMechanism.hpp"
#include "cfd/species/SpeciesRegistry.hpp"
#include "cfd/thermal/Enthalpy.hpp"
#include "cfd/thermal/TemperatureDependentProperty.hpp"

using cfd::Index;
using cfd::Real;
using cfd::physics::ConstantProperty;
using cfd::physics::LinearProperty;
using cfd::species::FormationEnthalpies;
using cfd::species::MixtureThermodynamics;
using cfd::species::PremixedFlameChemistry;
using cfd::species::PremixedFlameConditions;
using cfd::species::PremixedFlameSettings;
using cfd::species::PremixedFlameStatus;
using cfd::species::Reaction;
using cfd::species::ReactionKinetics;
using cfd::species::ReactionMechanism;
using cfd::species::ReactionRateParameters;
using cfd::species::SpeciesRegistry;
using cfd::species::SpeciesThermodynamics;

namespace {

// ---- the synthetic flame: F -> P, air-like molecular weight so the density is realistic.
constexpr Real kWeight = 0.029;         // [kg/mol]
constexpr Real kSpecificHeat = 1200.0;  // [J/(kg K)]
constexpr Real kFuelFraction = 0.05;    // Y_F,u
constexpr Real kUnburntT = 300.0;       // [K]
constexpr Real kPressure = 101325.0;    // [Pa]
constexpr Real kConductivity = 0.05;    // [W/(m K)]
constexpr Real kActivation = 1.2e5;     // [J/mol]

// A IS SIZED SO THE FRONT FITS BETWEEN TWO OPPOSING CONSTRAINTS, and that is a property of the
// TEST PROBLEM, not a tuning of the solver. The flame's diffusive thickness is delta = alpha/S_L
// with S_L ~ sqrt(A), so A sets the thickness, and the thickness is squeezed from both sides:
//
//     delta >> cell size       or the front is unresolved on an upwind scheme
//     delta << x_anchor        or the preheat zone does not FIT upstream of the front, and the
//                              cold Dirichlet inlet quenches the flame whatever mdot is
//
// With a 30 mm domain, 300 cells (dx = 0.1 mm) and the anchor at 0.35 (10.5 mm upstream):
//
//     alpha = lambda/(rho_u cp) = 0.05/(1.178 * 1200) = 3.54e-5 m^2/s
//     want delta ~ 0.8 mm  =>  8 cells wide, and 13 thicknesses of upstream room
//     S_L = alpha/delta = 4.4 cm/s, and S_L ~ sqrt(A) anchored on a MEASURED point
//     (A = 1e5 gave a measured S_L ~ 0.8 cm/s)  =>  A ~ 1e5 * (4.4/0.8)^2 ~ 3e6
//
// BOTH CONSTRAINTS WERE FOUND BY VIOLATING THEM, in this order:
//
//   * A = 1e7 put delta at 0.1 mm against 0.1 mm cells -- a front ONE CELL wide. It did not
//     converge to a wrong answer, it EXTINGUISHED. An unresolved front on an upwind scheme does
//     not degrade gracefully. The resolution assertion in the refinement test exists because of it.
//   * A = 1e5 then put delta at ~4.4 mm with only 7 mm of domain upstream of the anchor, so the
//     preheat zone had nowhere to sit and the flame was quenched by the inlet at EVERY mdot --
//     including mdot ~ 1e-5, where the inflow velocity is 8e-6 m/s. The trial log is what made
//     this unmistakable: a measured drift of +0.39 m/s at essentially zero inflow is kinematically
//     impossible for a front, so what was being tracked was a collapsing profile, not a flame.
// A = 3e7 with Ea = 1.2e5 is the fixture the window scan SELECTED, on measured grounds: it is the
// setting that held the peak temperature at exactly the adiabatic 1800 K while keeping both zones
// resolved. From that scan at 600 cells over 30 mm:
//
//     delta = 2.285 mm = 45.7 cells, reaction zone 6.84 cells, x_anchor/delta = 4.59
//
// The domain is widened to 40 mm here so x_anchor/delta rises to ~6, giving the eigenvalue room to
// plateau away from the cold inlet. The scan is kept as DISABLED_ParameterWindowScan so the choice
// can be re-derived rather than taken on trust.
constexpr Real kPreExponential = 3.0e7;  // [1/s], first order

// Chosen so T_ad = 1800 K exactly under the solver's own balance:
//   T_ad = T_u - dHr Y_F,u / (nu_F M_F cp) = 300 + 1.044e6 * 0.05 / (0.029 * 1200) = 1800
constexpr Real kMolarFormationP = -1.044e6;  // [J/mol]
constexpr Real kExpectedAdiabatic = 1800.0;  // [K]

SpeciesThermodynamics makeSpecies(const std::string& name) {
  return SpeciesThermodynamics(
      kWeight,
      cfd::thermal::SensibleEnthalpy(
          cfd::thermal::TemperatureDependentProperty::constant(kSpecificHeat, name), 298.15, 0.0));
}

struct Problem {
  SpeciesRegistry registry;
  MixtureThermodynamics mixture;
  FormationEnthalpies formation;
  ReactionMechanism mechanism;
  ReactionKinetics kinetics;
};

Problem makeProblem(Real preExponentialScale = 1.0) {
  SpeciesRegistry registry({"F", "P"});
  std::vector<SpeciesThermodynamics> thermo;
  thermo.push_back(makeSpecies("cp_F"));
  thermo.push_back(makeSpecies("cp_P"));
  MixtureThermodynamics mixture(registry, std::move(thermo));

  FormationEnthalpies formation = FormationEnthalpies::fromMolar(mixture, {0.0, kMolarFormationP});

  std::vector<Reaction> reactions;
  reactions.push_back(Reaction("F->P", registry, {{"F", 1.0}}, {{"P", 1.0}}));
  ReactionMechanism mechanism(registry, std::move(reactions), mixture);

  std::vector<ReactionRateParameters> parameters;
  parameters.push_back(ReactionRateParameters{
      "F->P",
      cfd::species::ArrheniusParameters(kPreExponential * preExponentialScale, 0.0, kActivation),
      std::nullopt});
  ReactionKinetics kinetics(mechanism, std::move(parameters));

  return Problem{registry, std::move(mixture), std::move(formation), std::move(mechanism),
                 std::move(kinetics)};
}

PremixedFlameChemistry chemistryOf(const Problem& problem) {
  return PremixedFlameChemistry{&problem.mechanism, &problem.mixture, &problem.formation,
                                &problem.kinetics};
}

PremixedFlameConditions makeConditions(const cfd::physics::TemperatureProperty& conductivity) {
  PremixedFlameConditions conditions;
  conditions.pressure = kPressure;
  conditions.unburntTemperature = kUnburntT;
  conditions.unburntComposition = {kFuelFraction, 1.0 - kFuelFraction};
  conditions.conductivity = &conductivity;
  conditions.specificHeat = kSpecificHeat;
  conditions.lewisNumber = 1.0;
  conditions.fuelIndex = 0;
  return conditions;
}

PremixedFlameSettings makeSettings() {
  PremixedFlameSettings settings;
  // SIZED FROM BOTH FLAME SCALES, which was the error that took longest to find. The preheat zone
  // is alpha_u/S_L ~ 0.6 mm and the REACTION zone is delta_preheat/Ze ~ 0.09 mm, so:
  //
  //     dx       <= delta_reaction/4 = 0.022 mm  -> 0.02 mm
  //     domain   >= ~12 delta_preheat = 7 mm     -> 10 mm
  //     x_anchor  = 0.35 * 10 mm = 3.5 mm = 5.9 delta_preheat, enough for the plateau
  //
  // The previous 600 cells over 40 mm had the domain four times too LONG and the mesh four times
  // too COARSE at the same time, which is why the resolution and plateau criteria failed together.
  settings.cells = 500;
  settings.domainLength = 0.010;
  settings.anchorFraction = 0.35;
  // The eigenvalue is BRACKETED, so the tolerance is the width of that bracket. 1e-3 is far
  // tighter than any criterion here needs and keeps each bisection ladder short; the width
  // actually achieved is reported, so nothing is taken on trust.
  settings.tolerance = 1.0e-3;
  settings.maxOuterIterations = 40;
  // The drift carries grid-locking noise: the interpolated crossing wobbles by a fraction of a
  // cell as the front moves through the mesh, so a settling bound tighter than that noise can
  // never be met. 0.05 * u_in sits above it; the sensitivity test varies the window instead.
  settings.driftSettleTolerance = 0.05;
  settings.maximumStepsPerTrial = 20000;
  return settings;
}

void reportFlame(const char* label, const cfd::species::PremixedFlameResult& flame) {
  std::cout << "    " << label << ": S_L = " << flame.flameSpeed * 100.0 << " cm/s"
            << " (seed " << flame.massFluxEstimate / flame.unburntDensity * 100.0 << ")"
            << ", S_c = " << flame.consumptionSpeed * 100.0 << " cm/s"
            << ", T_peak = " << flame.peakTemperature << " K"
            << ", delta = " << flame.flameThickness * 1000.0 << " mm"
            << ", bracket = " << flame.massFluxResidual << ", bisect " << flame.outerIterations
            << " after " << flame.bracketSteps << " brackets, " << flame.totalTimeSteps
            << " steps, minY = " << flame.minMassFraction
            << ", |sumY-1| = " << flame.maxCompositionSumDeviation
            << "\n              fuel balance = " << flame.fuelBalanceResidual
            << ", energy balance = " << flame.energyBalanceResidual << ", final march "
            << flame.finalMarchSteps << (flame.steadyStateReached ? " (steady)" : " (NOT steady)")
            << "\n";
  if (!flame.diagnostic.empty()) {
    std::cout << "        diagnostic: " << flame.diagnostic << "\n";
  }
  for (const auto& trial : flame.trials) {
    std::cout << "        trial mdot = " << trial.massFlux << " drift = " << trial.drift
              << " dt = " << trial.timeStep << " steps = " << trial.steps
              << (trial.measured ? " (measured" : " (UNDECIDED")
              << (trial.settled ? ", settled" : ", UNSETTLED")
              << (trial.extinguished ? ", extinguished)" : ")") << " -> "
              << (trial.tooLarge ? "too large" : "too small") << "\n";
  }
}

}  // namespace

// =================================================================================================
// 1. It converges, and the solved peak temperature meets the INDEPENDENT enthalpy balance
// =================================================================================================

TEST(G8PremixedFlameTest, ItConvergesAndReachesTheIndependentlyDerivedAdiabaticTemperature) {
  const Problem problem = makeProblem();
  const ConstantProperty conductivity(kConductivity);
  const auto conditions = makeConditions(conductivity);

  // The analytic balance first, so the number the solve is judged against was fixed before it ran.
  const Real adiabatic = cfd::species::flameAdiabaticTemperature(chemistryOf(problem), conditions);
  EXPECT_NEAR(adiabatic, kExpectedAdiabatic, 1.0e-9 * kExpectedAdiabatic)
      << "the solver's own T_ad disagrees with the hand-derived 1800 K, so cp or the formation "
         "enthalpy is not what this test believes";

  const auto flame =
      cfd::species::solvePremixedFlame(chemistryOf(problem), conditions, makeSettings());
  reportFlame("baseline", flame);

  ASSERT_EQ(flame.status, PremixedFlameStatus::Converged);
  EXPECT_GT(flame.flameSpeed, 0.0);
  // STEADINESS IS REPORTED, NOT GATED, and that is a derivation rather than a concession.
  //
  // This test first asserted steadyStateReached, against a per-step tolerance of 1e-9. That bound
  // is unreachable BY CONSTRUCTION: the eigenvalue is bracketed, not solved exactly, so a residual
  // front drift of order 2.5e-5 m/s survives, and a front creeping at that speed changes a cell's
  // temperature by (drift * dt)/delta ~ 1.6e-5 of the rise per step -- four orders ABOVE the bound.
  // It could only ever report false, and it did, after 1120 flame transit times.
  //
  // The gate is the conservation balance instead. That is not a looser version of the same check:
  // it is an integral identity rather than a chosen constant, it cannot be satisfied by the seed
  // (see TheObviousChecksAreVacuousOnTheSeededProfile), and a transient state does not close a
  // steady balance -- so a balance that closes is itself the evidence that the state is steady
  // enough to report.
  std::cout << "    steady = " << flame.steadyStateReached << " after " << flame.finalMarchSteps
            << " final-march steps, dT residual = " << flame.temperatureResidual << "\n";

  // THE CONSERVATION BALANCES ARE THE REAL CHECKS HERE.
  //
  // What this test USED to assert -- that the peak reaches T_ad, and that the outlet fuel is
  // consumed -- is VACUOUS on this solver, and the proof is the companion test
  // TheObviousChecksAreVacuousOnTheSeededProfile below. The seeded profile blends to the burnt
  // composition and the adiabatic temperature downstream, so at step zero the outlet already holds
  // Y_fuel == 0.0 exactly and T == T_ad exactly. Both assertions passed while the consumption
  // integral was 43% out, which is the worst possible arrangement: the reassuring checks were the
  // ones measuring nothing.
  //
  // The balances cannot be satisfied by the seed. They are integral identities of a steady flame.
  EXPECT_LT(flame.fuelBalanceResidual, 0.02)
      << "fuel is not conserved: mdot (Y_out - Y_u) disagrees with the reaction integral plus the "
         "inlet diffusive influx by "
      << flame.fuelBalanceResidual * 100.0 << "% of the through-flux";

  // THE ENERGY BALANCE IS NOT INDEPENDENT EVIDENCE HERE, and must not be counted as a second
  // conservation check. For a single global reaction with constant cp, integral S_E dx is exactly
  // proportional to integral omega_F dx, and mdot cp (T_ad - T_u) is exactly
  // mdot Y_u |dHr|/(nu_F M_F) BY THE DEFINITION of T_ad -- so both residuals reduce to the same
  // ratio. Measured: 0.41766725 and 0.41766720, identical to seven figures. It is asserted anyway
  // because it stops being redundant as soon as cp varies or the mechanism gains a reaction.
  EXPECT_LT(flame.energyBalanceResidual, 0.02)
      << "energy is not conserved: mdot cp (T_out - T_u) disagrees with the heat-release integral "
         "plus the inlet conductive influx by "
      << flame.energyBalanceResidual * 100.0 << "%";

  // The peak still may not EXCEED T_ad -- that bound is not vacuous, since nothing in the seed
  // prevents the march from overshooting it, and overshoot is exactly what the explicit heat
  // release used to do (17646 K, 38511 K, 144482 K).
  EXPECT_LT(flame.peakTemperature, adiabatic * 1.001);

  // THE THERMAL-EXPANSION ASSERTION WAS DELETED, NOT WEAKENED, and the reason is worth stating so
  // it does not come back.
  //
  // It read `EXPECT_GT(velocity[last], 3.0 * velocity[0])`. In THIS formulation the velocity is not
  // a solved field at all: the mass flux is imposed uniform and `velocity = mdot/rho` with `rho`
  // from the equation of state, so the ratio is just `rho_u/rho_b = T_ad/T_u = 6`. The SEED already
  // satisfies it, exactly as the peak-temperature and outlet-fuel checks did -- it cannot
  // distinguish a converged flame from the initial condition, so it measures nothing.
  //
  // There is no non-vacuous version of it here, because thermal expansion in this formulation is a
  // CONSEQUENCE of the equation of state and the imposed flux rather than something the solve
  // determines. A mis-specified check is deleted and recorded rather than kept with an exception
  // that makes it look satisfied.
}

// =================================================================================================
// 1b. THE NON-VACUITY PROOF for the two checks test 1 no longer makes
// =================================================================================================

TEST(G8PremixedFlameTest, TheObviousChecksAreVacuousOnTheSeededProfile) {
  const Problem problem = makeProblem();
  const ConstantProperty conductivity(kConductivity);
  const auto conditions = makeConditions(conductivity);

  // One step: the reported state is the SEEDED profile, which no flame has shaped.
  auto settings = makeSettings();
  settings.maximumStepsPerTrial = 1;
  settings.maximumBracketSteps = 1;

  const auto seeded = cfd::species::solvePremixedFlame(chemistryOf(problem), conditions, settings);
  ASSERT_FALSE(seeded.temperature.empty());
  const Index last = static_cast<Index>(seeded.temperature.size()) - 1;

  std::cout << "    seeded state: T_peak = " << seeded.peakTemperature
            << " K, outlet Y_fuel = " << seeded.massFractions[0][last]
            << ", fuel balance = " << seeded.fuelBalanceResidual
            << ", energy balance = " << seeded.energyBalanceResidual << "\n";

  // THE TWO OLD CHECKS PASS ON A STATE NO FLAME HAS TOUCHED. That is what makes them worthless,
  // and asserting it here is what stops them being reintroduced as evidence.
  EXPECT_LT(seeded.peakTemperature, kExpectedAdiabatic * 1.001);
  EXPECT_GT(seeded.peakTemperature, kExpectedAdiabatic * 0.98)
      << "the seed no longer supplies T_ad downstream, so this proof needs rewriting";
  EXPECT_LT(seeded.massFractions[0][last], 1.0e-3 * kFuelFraction)
      << "the seed no longer supplies the burnt composition downstream";

  // AND THE BALANCES FAIL ON IT, which is what makes THEM worth asserting. A check that cannot
  // distinguish the initial condition from a converged flame is not a check.
  EXPECT_GT(seeded.fuelBalanceResidual + seeded.energyBalanceResidual, 0.02)
      << "the conservation balances are satisfied by the raw seed too, so they are no better than "
         "the checks they replaced";
}

// =================================================================================================
// 2. THE DEGENERACY TRAP: the eigenvalue must not depend on the initial guess
// =================================================================================================

TEST(G8PremixedFlameTest, TheEigenvalueIsIndependentOfTheInitialMassFluxGuess) {
  const Problem problem = makeProblem();
  const ConstantProperty conductivity(kConductivity);
  const auto conditions = makeConditions(conductivity);

  // A consumption-integral fixed point would reproduce whatever it was given (see the header of
  // PremixedFlame.hpp), so two guesses a factor of 9 apart would come back a factor of 9 apart.
  // This is the test that distinguishes a real eigenvalue solve from that vacuous one.
  auto low = makeSettings();
  low.initialMassFlux = 0.03;
  auto high = makeSettings();
  high.initialMassFlux = 0.25;

  const auto flameLow = cfd::species::solvePremixedFlame(chemistryOf(problem), conditions, low);
  const auto flameHigh = cfd::species::solvePremixedFlame(chemistryOf(problem), conditions, high);
  reportFlame("mdot_0 = 0.1", flameLow);
  reportFlame("mdot_0 = 0.9", flameHigh);

  ASSERT_EQ(flameLow.status, PremixedFlameStatus::Converged);
  ASSERT_EQ(flameHigh.status, PremixedFlameStatus::Converged);

  // NON-VACUITY: the guesses really were far apart, so agreement cannot be an accident. A
  // consumption-integral fixed point would have returned each guess essentially unchanged.
  ASSERT_GT(high.initialMassFlux / low.initialMassFlux, 5.0);

  const Real spread = std::abs(flameHigh.flameSpeed - flameLow.flameSpeed) /
                      (0.5 * (flameHigh.flameSpeed + flameLow.flameSpeed));
  std::cout << "    initial-guess spread: " << spread << "\n";
  EXPECT_LT(spread, 1.0e-3) << "S_L depends on the initial guess, so this is not an eigenvalue";
}

// =================================================================================================
// 3. THE COLD-BOUNDARY DIFFICULTY: the eigenvalue must plateau in the anchor position
// =================================================================================================

TEST(G8PremixedFlameTest, TheEigenvaluePlateausInTheAnchorPositionAndTheDomainLength) {
  const Problem problem = makeProblem();
  const ConstantProperty conductivity(kConductivity);
  const auto conditions = makeConditions(conductivity);

  std::vector<Real> speeds;
  for (const Real fraction : {0.30, 0.40, 0.50}) {
    auto settings = makeSettings();
    settings.anchorFraction = fraction;
    const auto flame = cfd::species::solvePremixedFlame(chemistryOf(problem), conditions, settings);
    ASSERT_EQ(flame.status, PremixedFlameStatus::Converged) << "anchor " << fraction;
    reportFlame(("anchor " + std::to_string(fraction)).c_str(), flame);
    speeds.push_back(flame.flameSpeed);

    // The controller really did hold the front where it was told to.
    //
    // THIS ASSERTION WAS VACUOUS UNTIL TD-005, and the threshold is deliberately unchanged.
    // `result.flamePosition` was assigned `anchorFraction * domainLength`, so with
    // `fraction == anchorFraction` both sides of this EXPECT_NEAR were the SAME expression: an
    // identity that passed for any solver behaviour, including one that never ran. TD-005 made
    // `flamePosition` a measurement of the reported field, which turns this line into the check it
    // always claimed to be -- so it is left exactly as written, at the same tolerance, rather than
    // rewritten. `0.02 * domainLength` is 10 cells on this fixture, against a 3-cell recentring
    // dead band. Evidence: results/technical_debt/TD005_flame_speed_extraction/logs/02.
    EXPECT_NEAR(flame.flamePosition, fraction * settings.domainLength,
                0.02 * settings.domainLength);
  }

  const Real lo = *std::min_element(speeds.begin(), speeds.end());
  const Real hi = *std::max_element(speeds.begin(), speeds.end());
  std::cout << "    anchor plateau: " << lo * 100.0 << " .. " << hi * 100.0 << " cm/s\n";
  EXPECT_LT((hi - lo) / lo, 0.02)
      << "S_L still depends on where the front is pinned, so the domain is too short for the "
         "eigenvalue to have plateaued and a quoted flame speed would be quoting the anchor";

  // And a longer domain must not move it either.
  auto longer = makeSettings();
  longer.domainLength = 0.015;
  longer.cells = 750;  // the same cell size, so this varies the DOMAIN and not the resolution
  const auto flameLong = cfd::species::solvePremixedFlame(chemistryOf(problem), conditions, longer);
  ASSERT_EQ(flameLong.status, PremixedFlameStatus::Converged);
  reportFlame("domain 40 mm", flameLong);
  EXPECT_LT(std::abs(flameLong.flameSpeed - speeds[0]) / speeds[0], 0.02);
}

// =================================================================================================
// 4. Discrete conservation: the consumption integral must reproduce mdot/rho_u
// =================================================================================================

TEST(G8PremixedFlameTest, TheConsumptionIntegralAgreesWithTheEigenvalue) {
  const Problem problem = makeProblem();
  const ConstantProperty conductivity(kConductivity);
  const auto conditions = makeConditions(conductivity);

  const auto flame =
      cfd::species::solvePremixedFlame(chemistryOf(problem), conditions, makeSettings());
  ASSERT_EQ(flame.status, PremixedFlameStatus::Converged);

  // This is a CONSISTENCY check, not validation: the identity is an exact consequence of the
  // conservation law, so it tests the discretisation and the convergence, not the physics.
  //
  // It is also the check that caught the vacuity of the two assertions test 1 used to make: it
  // read 2.58 cm/s against a flame speed of 4.50 cm/s while both of those passed.
  // The identity holds only for a steady flame -- but `steadyStateReached` is NOT the way to test
  // that, because its tolerance is unreachable by construction (F13: a bracketed eigenvalue leaves
  // a residual front drift whose per-step effect is four orders above the bound). Asserting it here
  // would fail for a reason that has nothing to do with the identity. The fuel balance is the
  // steadiness evidence, for the same reason it is in test 1: a transient state does not close a
  // steady balance.
  std::cout << "    steady = " << flame.steadyStateReached
            << ", fuel balance = " << flame.fuelBalanceResidual << "\n";
  ASSERT_LT(flame.fuelBalanceResidual, 0.05)
      << "the state does not conserve fuel well enough for the consumption identity to mean "
         "anything, so the mismatch below would be measuring the wrong state";

  const Real mismatch = std::abs(flame.consumptionSpeed - flame.flameSpeed) / flame.flameSpeed;
  std::cout << "    S_L = " << flame.flameSpeed * 100.0
            << " cm/s, S_c = " << flame.consumptionSpeed * 100.0 << " cm/s, mismatch = " << mismatch
            << "\n";
  EXPECT_LT(mismatch, 0.01);

  // The two invariants the iterate cannot hold to a state boundary's tolerance stay negligible.
  EXPECT_GT(flame.minMassFraction, -1.0e-8);
  EXPECT_LT(flame.maxCompositionSumDeviation, 1.0e-8);
}

// =================================================================================================
// 5. S_L converges under mesh refinement
// =================================================================================================

TEST(G8PremixedFlameTest, TheFlameSpeedConvergesUnderMeshRefinement) {
  const Problem problem = makeProblem();
  const ConstantProperty conductivity(kConductivity);
  const auto conditions = makeConditions(conductivity);

  // THE TRIPLET WAS CHOSEN BY MEASUREMENT, and the measurement is recorded so it can be
  // re-derived rather than trusted: results/technical_debt/TD004_premixed_flame/
  // logs/08_fixture_selection.log, from tools/fixture_probe.cpp.
  //
  // {250, 500, 1000} did not finish in 1750 s (logs/07). The cost scales roughly as nc^3 -- the
  // march length goes as 1/dt with dt diffusion-limited (dt ~ dx^2, so steps ~ nc^2) and the
  // per-step solve cost goes as nc -- so the 1000-cell level alone costs about 1920 s.
  //
  // A halving family was swept and two constraints decided the choice. Measured:
  //
  //     cells   S_L cm/s   res cells   wall s   fuel bal
  //        63    3.88104        4.09     13.8     0.8021
  //       125    4.40773        7.82     17.6     0.4703
  //       250    4.77424       13.42     41.6     0.5457
  //       500    4.86230       61.45    247.5     0.0433
  //
  //     {63, 125, 250}    p = 0.523    <- NOT asymptotic; excluded
  //     {125, 250, 500}   p = 2.057    <- asymptotic, and close to the 1.93 on record
  //
  // So {125, 250, 500} costs 307 s against the 900 s per-test budget, every level keeps the front
  // above the 4-cell guard, the differences shrink monotonically with no sign change, and the
  // observed order sits near 2. Dropping to 63 cells leaves the asymptotic range outright, which
  // is why the family is not shrunk further -- the p criterion caught that, not a guess.
  //
  // PROVISIONAL, AND THE REASON IS RECORDED. Every level above was measured on a solution whose
  // fuel balance is 47-80 % broken at the coarse end -- the non-conservative recentring TD-004 is
  // authorized to fix. A convergence study on non-conserving states can produce a tidy order from
  // a sequence of wrong answers, so p = 2.057 is NOT yet evidence of a correct second-order
  // method. The gate requires this selection to be RE-DERIVED after the recentring fix, and the
  // triplet re-confirmed, before any order claim rests on it.
  std::vector<Real> speeds;
  for (const Index cells : {125, 250, 500}) {
    auto settings = makeSettings();
    settings.cells = cells;
    const auto flame = cfd::species::solvePremixedFlame(chemistryOf(problem), conditions, settings);
    ASSERT_EQ(flame.status, PremixedFlameStatus::Converged) << "cells " << cells;
    reportFlame((std::to_string(cells) + " cells").c_str(), flame);
    speeds.push_back(flame.flameSpeed);

    // The front must be RESOLVED, or a converging sequence means nothing.
    const Real cellSize = settings.domainLength / static_cast<Real>(cells);
    EXPECT_GT(flame.flameThickness / cellSize, 4.0)
        << "the flame thickness spans only " << flame.flameThickness / cellSize << " cells";
  }

  // Successive differences must SHRINK. Gated on the error falling under refinement, exactly as
  // G8 specifies for the profiles -- not on an invented absolute bound.
  const Real coarse = std::abs(speeds[1] - speeds[0]);
  const Real fine = std::abs(speeds[2] - speeds[1]);
  std::cout << "    refinement differences: " << coarse * 100.0 << " then " << fine * 100.0
            << " cm/s\n";
  EXPECT_LT(fine, coarse) << "S_L is not converging under refinement";
}

// =================================================================================================
// 6. G8's FIRST NEGATIVE CONTROL: reactions disabled must not propagate a flame
// =================================================================================================

TEST(G8PremixedFlameTest, WithReactionsDisabledNoFlamePropagates) {
  const Problem problem = makeProblem();
  const ConstantProperty conductivity(kConductivity);
  const auto conditions = makeConditions(conductivity);

  auto settings = makeSettings();
  settings.reactionsEnabled = false;
  settings.maxOuterIterations = 2000;

  const auto flame = cfd::species::solvePremixedFlame(chemistryOf(problem), conditions, settings);
  Real peakHeat = 0.0;
  for (const Real q : flame.heatRelease) {
    peakHeat = std::max(peakHeat, std::abs(q));
  }
  std::cout << "    reactions off: status " << static_cast<int>(flame.status)
            << ", S_L = " << flame.flameSpeed * 100.0
            << " cm/s, S_c = " << flame.consumptionSpeed * 100.0
            << " cm/s, peak |S_E| = " << peakHeat << ", brackets = " << flame.bracketSteps << "\n";

  // WHAT "NO FLAME" LOOKS LIKE TO THIS SOLVER, stated precisely, because the first version of this
  // control asserted the wrong thing.
  //
  // It asserted status == NoFlame. But with the chemistry off the front cannot propagate upstream
  // at all, so the drift is positive at EVERY mass flux: the bracketing phase halves mdot until its
  // budget runs out and never finds a sign change. The status is then MaxIterations -- an honest
  // report that no eigenvalue exists -- and the test failed while the solver was behaving exactly
  // as it should.
  //
  // What G8 actually requires is that no flame speed be produced. That is asserted directly, and it
  // is NOT vacuous: the identical assertions on the identical solver with the chemistry ON are what
  // tests 1 to 5 measure, and they converge.
  EXPECT_NE(flame.status, PremixedFlameStatus::Converged)
      << "an eigenvalue was reported with the chemistry switched off, so the search is finding a "
         "flame speed in a problem that has none";

  // The rate is an explicit zero everywhere, so these are exact, not small.
  EXPECT_EQ(peakHeat, 0.0) << "heat was released with the chemistry switched off";
  EXPECT_EQ(flame.consumptionSpeed, 0.0) << "fuel was consumed with the chemistry switched off";
  EXPECT_EQ(flame.chemistrySubSteps, 0u) << "the chemistry sub-stepper ran at all";
}

// =================================================================================================
// 7. G8's SECOND NEGATIVE CONTROL: a 10x pre-exponential must move S_L far
// =================================================================================================

TEST(G8PremixedFlameTest, TenTimesThePreExponentialMovesTheFlameSpeedFar) {
  const ConstantProperty conductivity(kConductivity);

  const Problem baseline = makeProblem(1.0);
  const Problem perturbed = makeProblem(10.0);
  const auto conditions = makeConditions(conductivity);

  const auto flameBaseline =
      cfd::species::solvePremixedFlame(chemistryOf(baseline), conditions, makeSettings());
  auto fineSettings = makeSettings();
  fineSettings.cells = 1500;  // 10x A thins the front ~3.2x, so the mesh has to keep up
  const auto flamePerturbed =
      cfd::species::solvePremixedFlame(chemistryOf(perturbed), conditions, fineSettings);
  reportFlame("A x 1", flameBaseline);
  reportFlame("A x 10", flamePerturbed);

  ASSERT_EQ(flameBaseline.status, PremixedFlameStatus::Converged);
  ASSERT_EQ(flamePerturbed.status, PremixedFlameStatus::Converged);

  // For a one-step reaction S_L ~ sqrt(A), so 10x in A is roughly 3.2x in S_L. Gated loosely at 2x
  // because the asymptotic scaling is not exact, but the point is that the control is DECISIVE:
  // it cannot be mistaken for a 1-2% band.
  const Real ratio = flamePerturbed.flameSpeed / flameBaseline.flameSpeed;
  std::cout << "    S_L ratio for 10x A: " << ratio << "\n";
  EXPECT_GT(ratio, 2.0) << "a 10x error in the pre-exponential barely moved the flame speed, so "
                           "this control could not detect a mechanism error";
}

// =================================================================================================
// 8. lambda(T) is really consumed, not silently frozen at a single value
// =================================================================================================

TEST(G8PremixedFlameTest, AVariableConductivityChangesTheAnswerAndIsNotFrozenAtTheInletValue) {
  const Problem problem = makeProblem();

  // A law that EQUALS the constant at the unburnt temperature and rises with T. If lambda were
  // evaluated once at the inlet and frozen -- the easiest possible defect -- these two would agree
  // exactly. The flame lives at high temperature, so a correct solver must report a FASTER flame.
  const ConstantProperty constantConductivity(kConductivity);
  const LinearProperty risingConductivity(kConductivity, kUnburntT, 4.0e-5);
  ASSERT_NEAR(risingConductivity.value(kUnburntT), kConductivity, 1.0e-15);
  ASSERT_GT(risingConductivity.value(kExpectedAdiabatic), 3.0 * kConductivity);

  const auto flameConstant = cfd::species::solvePremixedFlame(
      chemistryOf(problem), makeConditions(constantConductivity), makeSettings());
  const auto flameRising = cfd::species::solvePremixedFlame(
      chemistryOf(problem), makeConditions(risingConductivity), makeSettings());
  reportFlame("lambda constant", flameConstant);
  reportFlame("lambda rising", flameRising);

  ASSERT_EQ(flameConstant.status, PremixedFlameStatus::Converged);
  ASSERT_EQ(flameRising.status, PremixedFlameStatus::Converged);

  EXPECT_GT(flameRising.flameSpeed, 1.3 * flameConstant.flameSpeed)
      << "a conductivity that triples across the front produced the same flame speed as a "
         "constant one, so lambda(T) is being evaluated once and frozen";
  // ... and a thicker front, which is the other signature of a larger diffusivity.
  EXPECT_GT(flameRising.flameThickness, flameConstant.flameThickness);
}

// =================================================================================================
// 9. Malformed configurations are REFUSED, with the dangerous ones named explicitly
// =================================================================================================

TEST(G8PremixedFlameTest, MalformedConfigurationsAreRefused) {
  const Problem problem = makeProblem();
  const ConstantProperty conductivity(kConductivity);

  // A null lambda(T) must not fall back to a default: a defaulted conductivity would be an
  // uncited number in the most sensitive position of the measurement.
  {
    auto conditions = makeConditions(conductivity);
    conditions.conductivity = nullptr;
    EXPECT_THROW(static_cast<void>(cfd::species::solvePremixedFlame(chemistryOf(problem),
                                                                    conditions, makeSettings())),
                 cfd::InvalidArgumentError);
  }
  // Naming a PRODUCT as the fuel would silently measure the wrong integral.
  {
    auto conditions = makeConditions(conductivity);
    conditions.fuelIndex = 1;  // "P"
    EXPECT_THROW(static_cast<void>(cfd::species::solvePremixedFlame(chemistryOf(problem),
                                                                    conditions, makeSettings())),
                 cfd::InvalidArgumentError);
  }
  // Null chemistry.
  {
    PremixedFlameChemistry broken = chemistryOf(problem);
    broken.kinetics = nullptr;
    EXPECT_THROW(static_cast<void>(cfd::species::solvePremixedFlame(
                     broken, makeConditions(conductivity), makeSettings())),
                 cfd::InvalidArgumentError);
  }
  // An out-of-range anchor, which would place the front outside the domain.
  {
    auto settings = makeSettings();
    settings.anchorFraction = 1.5;
    EXPECT_THROW(static_cast<void>(cfd::species::solvePremixedFlame(
                     chemistryOf(problem), makeConditions(conductivity), settings)),
                 cfd::InvalidArgumentError);
  }
}

// =================================================================================================
// 10. The burnt composition is the stoichiometric one, and it closes exactly
// =================================================================================================

TEST(G8PremixedFlameTest, TheBurntCompositionConsumesTheFuelExactlyAndConservesMass) {
  const Problem problem = makeProblem();
  const ConstantProperty conductivity(kConductivity);
  const auto conditions = makeConditions(conductivity);

  const auto burnt = cfd::species::flameBurntComposition(chemistryOf(problem), conditions);
  ASSERT_EQ(burnt.size(), 2u);

  // The fuel term is constructed to cancel exactly, so this is 0 and not merely small.
  EXPECT_EQ(burnt[0], 0.0) << "the fuel is not exactly consumed: " << burnt[0];
  // sum M_i nu_i = 0 per reaction (P18.4 enforces it), so the mixture sum is preserved.
  EXPECT_NEAR(burnt[0] + burnt[1], 1.0, 1.0e-15);
  EXPECT_NEAR(burnt[1], 1.0, 1.0e-15);
}

// =================================================================================================
// =================================================================================================
// 11. The one method knob the search has left must not change the answer
// =================================================================================================

TEST(G8PremixedFlameTest, TheAnswerIsInsensitiveToTheStepsPerTrial) {
  const Problem problem = makeProblem();
  const ConstantProperty conductivity(kConductivity);
  const auto conditions = makeConditions(conductivity);

  // stepsPerTrial is the one genuinely method-specific knob the search has left. A trial too
  // short to let the flame structure respond reads the transient rather than the propagation, so
  // the answer MUST be shown insensitive to it rather than assumed to be. This is the check that
  // the continuous-controller version failed catastrophically: there, the update ran 360x faster
  // than the structure and the measured drift stopped meaning propagation at all.
  std::vector<Real> speeds;
  for (const Index steps : {16, 32, 64}) {
    auto settings = makeSettings();
    settings.driftWindow = steps;
    const auto flame = cfd::species::solvePremixedFlame(chemistryOf(problem), conditions, settings);
    ASSERT_EQ(flame.status, PremixedFlameStatus::Converged) << "steps " << steps;
    reportFlame((std::to_string(steps) + " drift window").c_str(), flame);
    speeds.push_back(flame.flameSpeed);
  }

  const Real lo = *std::min_element(speeds.begin(), speeds.end());
  const Real hi = *std::max_element(speeds.begin(), speeds.end());
  std::cout << "    drift-window spread: " << (hi - lo) / lo << "\n";
  EXPECT_LT((hi - lo) / lo, 0.02)
      << "S_L depends on how long each trial marches, so the trials are too short for the flame "
         "structure to have responded and the drift sign is reading a transient";
}

// =================================================================================================
// 12. DIAGNOSTIC: does the synthetic flame PROPAGATE at all, and for how long does it live?
// =================================================================================================

TEST(G8PremixedFlameTest, DISABLED_PropagationHistory) {
  const Problem problem = makeProblem();
  const ConstantProperty conductivity(kConductivity);
  const auto conditions = makeConditions(conductivity);

  // One trial, never settling, run to a step budget: a fresh march of known length from the same
  // seed each time, so varying the budget gives the time history. At a mass flux well BELOW the
  // eigenvalue the inflow is negligible and the measured drift is minus the propagation speed.
  const Real rhoU = 1.178;
  for (const Index budget : {50, 100, 200, 400, 800, 1600, 3200}) {
    auto settings = makeSettings();
    settings.initialMassFlux = 0.004;  // u_in = 3.4 mm/s, far below any plausible S_L
    settings.maximumBracketSteps = 1;
    settings.driftSettleTolerance = 1.0e-12;  // never settles
    settings.maximumStepsPerTrial = budget;

    const auto flame = cfd::species::solvePremixedFlame(chemistryOf(problem), conditions, settings);
    Real peakHeat = 0.0;
    Real fuelMin = 1.0;
    for (const Real q : flame.heatRelease) {
      peakHeat = std::max(peakHeat, q);
    }
    for (const Real y :
         (flame.massFractions.empty() ? std::vector<Real>{1.0} : flame.massFractions[0])) {
      fuelMin = std::min(fuelMin, y);
    }
    const Real drift = flame.trials.empty() ? 0.0 : flame.trials[0].drift;
    const Real dt = flame.trials.empty() ? 0.0 : flame.trials[0].timeStep;
    std::cout << "    budget " << budget << ": dt = " << dt << " t = " << budget * dt
              << " s, drift = " << drift << " m/s, implied S_L = " << (0.004 / rhoU - drift) * 100.0
              << " cm/s, T_peak = " << flame.peakTemperature << " K, peak S_E = " << peakHeat
              << " W/m3, min Y_F = " << fuelMin << ", status " << static_cast<int>(flame.status)
              << "\n";
    if (!flame.diagnostic.empty()) {
      std::cout << "        " << flame.diagnostic << "\n";
    }
  }
}

// =================================================================================================
// 13. DIAGNOSTIC: which (A, Ea) actually leave a resolvable, surviving flame in this domain?
// =================================================================================================

TEST(G8PremixedFlameTest, DISABLED_ParameterWindowScan) {
  // The synthetic flame has to sit inside a window with THREE walls, and the third was the one
  // that kept being missed:
  //
  //   delta       = alpha/S_L          the PREHEAT zone, must FIT upstream of the anchor
  //   delta/Ze    = the REACTION zone, must be RESOLVED by the mesh
  //   Ze          = Ea (T_ad - T_u) / (R T_ad^2)
  //
  // Lowering Ea shrinks Ze and so widens the gap between the two, which is why Ea is scanned here
  // as well as A. Nothing about the SOLVER is being tuned: this picks a test fixture that the mesh
  // and domain can represent, and the window it reports is recorded with the fixture.
  const ConstantProperty conductivity(kConductivity);

  for (const Real activation : {6.0e4, 9.0e4, 1.2e5}) {
    for (const Real scale : {1.0, 10.0, 100.0, 1000.0}) {
      SpeciesRegistry registry({"F", "P"});
      std::vector<SpeciesThermodynamics> thermo;
      thermo.push_back(makeSpecies("cp_F"));
      thermo.push_back(makeSpecies("cp_P"));
      MixtureThermodynamics mixture(registry, std::move(thermo));
      FormationEnthalpies formation =
          FormationEnthalpies::fromMolar(mixture, {0.0, kMolarFormationP});
      std::vector<Reaction> reactions;
      reactions.push_back(Reaction("F->P", registry, {{"F", 1.0}}, {{"P", 1.0}}));
      ReactionMechanism mechanism(registry, std::move(reactions), mixture);
      std::vector<ReactionRateParameters> parameters;
      parameters.push_back(ReactionRateParameters{
          "F->P", cfd::species::ArrheniusParameters(kPreExponential * scale, 0.0, activation),
          std::nullopt});
      ReactionKinetics kinetics(mechanism, std::move(parameters));

      const PremixedFlameChemistry chemistry{&mechanism, &mixture, &formation, &kinetics};
      auto conditions = makeConditions(conductivity);

      auto settings = makeSettings();
      settings.cells = 600;
      settings.domainLength = 0.03;
      settings.tolerance = 0.02;  // a coarse bracket: this is a window scan, not a measurement
      settings.maxOuterIterations = 8;

      const Real ze = activation * (kExpectedAdiabatic - kUnburntT) /
                      (8.31446261815324 * kExpectedAdiabatic * kExpectedAdiabatic);
      const auto flame = cfd::species::solvePremixedFlame(chemistry, conditions, settings);
      const Real cellSize = settings.domainLength / static_cast<Real>(settings.cells);
      Real peakHeat = 0.0;
      for (const Real q : flame.heatRelease) {
        peakHeat = std::max(peakHeat, q);
      }
      std::cout << "    Ea = " << activation << " (Ze = " << ze
                << "), A = " << kPreExponential * scale << ": status "
                << static_cast<int>(flame.status) << ", S_L = " << flame.flameSpeed * 100.0
                << " cm/s, T_peak = " << flame.peakTemperature << " K, peak S_E = " << peakHeat
                << ", delta = " << flame.flameThickness * 1000.0
                << " mm (= " << flame.flameThickness / cellSize << " cells, reaction zone "
                << flame.flameThickness / (ze * cellSize) << " cells), x_anchor/delta = "
                << (settings.anchorFraction * settings.domainLength) / flame.flameThickness << "\n";
    }
  }
}

// =================================================================================================
// 14. DIAGNOSTIC: the SETTLING CURVE -- drift as a function of how long the trial marches
// =================================================================================================

TEST(G8PremixedFlameTest, DISABLED_SettlingCurve) {
  const Problem problem = makeProblem();
  const ConstantProperty conductivity(kConductivity);
  const auto conditions = makeConditions(conductivity);

  // Each run re-seeds from the same profile and marches a KNOWN number of steps without ever
  // settling, so the reported drift is drift(march length). That curve answers the one question
  // blocking the measurement, and it distinguishes the two candidate causes outright:
  //
  //   * converging to a plateau  => a genuinely slow evolution; more steps is the answer
  //   * oscillating about a value => a noise floor, and its AMPLITUDE is what the settling bound
  //                                  has to clear. Choosing the bound from that amplitude is
  //                                  deriving it from the instrument; choosing it from what makes
  //                                  a test pass is not, and is what CLAUDE.md section 5 forbids.
  std::cout << "    (drift in m/s; u_in at the seed is mdot/rho_u)\n";
  for (const Index budget : {500, 1000, 2000, 4000, 8000}) {
    auto settings = makeSettings();
    settings.maximumBracketSteps = 1;
    settings.driftSettleTolerance = 1.0e-12;  // never settles: the budget is what ends it
    settings.maximumStepsPerTrial = budget;
    settings.minimumStepsPerTrial = 1;

    const auto flame = cfd::species::solvePremixedFlame(chemistryOf(problem), conditions, settings);
    ASSERT_FALSE(flame.trials.empty()) << "budget " << budget << " produced no trial at all";
    const auto& first = flame.trials.front();
    std::cout << "    budget " << budget << ": mdot = " << first.massFlux
              << " (u_in = " << first.massFlux / flame.unburntDensity
              << " m/s), drift = " << first.drift << ", measured = " << first.measured
              << ", steps = " << first.steps << ", dt = " << first.timeStep
              << ", worst |drho|/rho = " << flame.worstDensityChange
              << ", worst ||r||/||b|| = " << flame.worstLinearResidual << "\n";
    if (!flame.diagnostic.empty()) {
      std::cout << "        " << flame.diagnostic << "\n";
    }
  }
}

// =================================================================================================
// 15. DIAGNOSTIC: is the 42% fuel-balance gap a TRANSIENT or a STRUCTURAL leak?
// =================================================================================================

TEST(G8PremixedFlameTest, DISABLED_BalanceVersusMarchLength) {
  const Problem problem = makeProblem();
  const ConstantProperty conductivity(kConductivity);
  const auto conditions = makeConditions(conductivity);

  // The reaction integral accounts for only 58% of the fuel entering, while the outlet fuel is
  // zero -- so fuel is disappearing without reacting. Two candidate causes, and the march length
  // separates them outright:
  //
  //   * a TRANSIENT: the state is still filling in, so the steady balance does not apply yet. The
  //     residual must then SHRINK with a longer march.
  //   * a STRUCTURAL leak -- the recentring discarding fresh fuel from the upstream end, or the
  //     non-conservative time term creating/destroying mass. The residual then stays PUT, and the
  //     recentring count says which.
  //
  // Recentring is the prime suspect to rule out first: shifting the profile upstream DISCARDS the
  // cells at the upstream end, and those cells hold fresh fuel.
  // THE KNOB THIS SWEEP VARIES WAS THE WRONG ONE, and that is recorded rather than quietly
  // corrected. `finalMarchSteps` is a SAFETY CEILING (default 200000); the march length is
  //
  //     min(finalMarchSteps, max(minimumStepsPerTrial,
  //                              finalMarchRelaxationTimes * finalRelaxation / finalTarget))
  //
  // and the derived bound is 3968 for this fixture, so the original 1000 / 4000 / 16000 ladder
  // bound the min() only at 1000 -- the other two rows would have been IDENTICAL and the sweep
  // would have reported a plateau, pointing at a structural leak for the wrong reason. Being right
  // by accident. The same mistake was made independently in
  // results/technical_debt/TD004_premixed_flame/tools/balance_probe.cpp and is preserved in that
  // directory's logs/02, which is how it was found.
  //
  // `finalMarchRelaxationTimes` is what governs, and the guard below asserts the march length
  // actually moved -- because a sweep whose knob is disconnected prints exactly what a real
  // plateau prints.
  std::cout << "    (fuel balance residual; outlet Y_fuel should be ~0 throughout)\n";
  std::vector<cfd::Index> marchLengths;
  for (const Real relaxationTimes : {10.0, 30.0, 90.0}) {
    auto settings = makeSettings();
    settings.finalMarchRelaxationTimes = relaxationTimes;
    const Index march = relaxationTimes;  // reported below; the achieved length is what matters
    static_cast<void>(march);

    const auto flame = cfd::species::solvePremixedFlame(chemistryOf(problem), conditions, settings);
    const Index last = static_cast<Index>(flame.temperature.size()) - 1;
    marchLengths.push_back(flame.finalMarchSteps);
    std::cout << "    relaxT " << relaxationTimes << " -> march " << flame.finalMarchSteps
              << ": fuel balance = " << flame.fuelBalanceResidual
              << ", S_c/S_L = " << flame.consumptionSpeed / flame.flameSpeed
              << ", outlet Y_fuel = " << flame.massFractions[0][last]
              << ", T_out = " << flame.temperature[last]
              << " K, recentrings = " << flame.recentrings << " (final march "
              << flame.finalMarchRecentrings << ", worst offset " << flame.worstFinalOffsetCells
              << " cells)"
              << ", steps used = " << flame.finalMarchSteps
              << ", dT residual = " << flame.temperatureResidual
              << ", steady = " << flame.steadyStateReached << "\n";
  }

  // FAIL CLOSED. Without this the sweep cannot tell a real plateau from a disconnected knob, which
  // is the defect it was itself carrying.
  bool moved = false;
  for (std::size_t i = 1; i < marchLengths.size(); ++i) {
    if (marchLengths[i] != marchLengths[0]) moved = true;
  }
  EXPECT_TRUE(moved) << "the march length did not change across the sweep, so none of the rows "
                        "above is a trend -- the independent variable is not connected";
}

// =================================================================================================
// 16. DIAGNOSTIC: the actual final profile, and where the anchor crossing really is
// =================================================================================================

TEST(G8PremixedFlameTest, DISABLED_FinalProfileDump) {
  const Problem problem = makeProblem();
  const ConstantProperty conductivity(kConductivity);
  const auto conditions = makeConditions(conductivity);

  auto settings = makeSettings();
  settings.finalMarchSteps = 1000;
  const auto flame = cfd::species::solvePremixedFlame(chemistryOf(problem), conditions, settings);

  const Real anchorT = kUnburntT + 0.5 * (kExpectedAdiabatic - kUnburntT);
  std::cout << "    anchor T = " << anchorT
            << " K, anchor x = " << settings.anchorFraction * settings.domainLength * 1000.0
            << " mm, recentrings = " << flame.recentrings << " (final march "
            << flame.finalMarchRecentrings << ", worst offset " << flame.worstFinalOffsetCells
            << " cells)\n"
            << "    status " << static_cast<int>(flame.status)
            << ", S_L = " << flame.flameSpeed * 100.0
            << " cm/s, S_c = " << flame.consumptionSpeed * 100.0
            << " cm/s, S_c/S_L = " << flame.consumptionSpeed / flame.flameSpeed
            << ", fuel balance = " << flame.fuelBalanceResidual
            << ", |sumY-1| = " << flame.maxCompositionSumDeviation
            << ", steady = " << flame.steadyStateReached
            << ", final march steps = " << flame.finalMarchSteps << "\n";

  // EVERY crossing of the anchor temperature, not just the first. If the profile is monotone there
  // is exactly one; more than one means locateFront can jump between them step to step, which
  // would make the front position -- and therefore the drift and the recentring -- meaningless.
  Index crossings = 0;
  for (Index c = 0; c + 1 < static_cast<Index>(flame.temperature.size()); ++c) {
    const Real a = flame.temperature[c] - anchorT;
    const Real b = flame.temperature[c + 1] - anchorT;
    if (a * b <= 0.0 && flame.temperature[c] != flame.temperature[c + 1]) {
      ++crossings;
      std::cout << "      crossing " << crossings << " at x = " << flame.x[c] * 1000.0 << " mm"
                << " (cell " << c << "), T " << flame.temperature[c] << " -> "
                << flame.temperature[c + 1] << "\n";
    }
  }
  std::cout << "    total crossings = " << crossings << "\n";

  // A coarse sample of the profile, enough to see a front, a plateau, or an oscillation.
  const Index n = static_cast<Index>(flame.temperature.size());
  std::cout << "    x[mm]    T[K]       Y_fuel        S_E[W/m3]     rho\n";
  for (Index c = 0; c < n; c += std::max<Index>(1, n / 40)) {
    std::cout << "    " << flame.x[c] * 1000.0 << "   " << flame.temperature[c] << "   "
              << flame.massFractions[0][c] << "   " << flame.heatRelease[c] << "   "
              << flame.density[c] << "\n";
  }
}
