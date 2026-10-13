// TD-004 AUDIT PROBE -- the two questions the gate design depends on, answered in one run.
//
// INVESTIGATION ONLY. No production file is changed, nothing is asserted; this measures.
//
// Q1  IS THE INNER LINEAR SOLVE THE PRECISION LIMIT?
//     §1 of audit.md cleared TD-003 as a blocker for this path on the grounds that PremixedFlame
//     judges ||r||/||b|| rather than BiCGSTAB's status, preconditions, and reports
//     `worstLinearResidual`. That reasoning is only as good as the number, and the three tests run
//     for logs/01 do not print it. If it sits at or below the 1e-9 acceptance fraction the solves
//     are not the limit and the clearing stands. If it does not, the precision problem is partly
//     TD-003's and the dependency has to be revisited BEFORE a gate is frozen.
//
// Q2  IS THE 4.33% BALANCE RESIDUAL A STOPPING-CRITERION ARTEFACT OR A REAL CONSERVATION DEFECT?
//     logs/01 measured `steady = 0 after 3968 final-march steps, dT residual = 0.166188` together
//     with a 4.33% fuel balance against a 2% gate. A march stopped before the profile settles does
//     not close a balance, so the two may be one fact. The discriminator is to lengthen ONLY the
//     final march and watch whether the balance falls:
//
//       balance falls towards 0 as the march lengthens  ->  stopping criterion. The gate should
//                                                           require steadiness, and the balance
//                                                           threshold stays where it is.
//       balance plateaus well above 2%                  ->  a real conservation defect in the
//                                                           discretisation, which no march length
//                                                           fixes and which TD-004 must repair.
//
//     Those demand different work, so guessing between them would pick the gate at random.
//
// Q3  ARE THE FUEL AND ENERGY BALANCES ONE MEASUREMENT? (free, from the same sweep)
//     logs/01 had them agreeing to four significant figures (0.0432796 vs 0.0432805), which F12
//     already identified once. If they track digit-for-digit across every march length then they
//     are structurally one number -- for a single-step F->P with constant cp the energy balance is
//     the fuel balance scaled by Delta h / cp -- and the gate must not count them as two
//     independent checks. The ratio is printed so this is read off rather than argued.
//
// COST. Each solve is a full eigenvalue search plus a final march; logs/01 took ~240 s per test at
// 500 cells. The sweep below is ordered cheapest-first and prints after every point, so a run that
// is cut short still yields the trend.
//
// ---------------------------------------------------------------------------------------------
// THE FIRST VERSION OF THIS PROBE WAS FAIL-OPEN, and logs/02_balance_probe.log preserves it.
//
// It swept `finalMarchSteps` over 4000 / 12000 / 36000 and printed THREE IDENTICAL ROWS -- same
// balance to seven figures, same `totalTimeSteps = 19264`. Read carelessly that is "the balance
// plateaus", which is one of the two answers the probe exists to choose between, and it would have
// sent TD-004 off to repair a discretisation defect that this measurement never demonstrated.
//
// What it actually showed is that the independent variable never moved. `finalMarchSteps` defaults
// to 200000 and is a SAFETY CEILING; the march length is
//
//     finalBudget = min(finalMarchSteps,
//                       max(minimumStepsPerTrial,
//                           finalMarchRelaxationTimes * finalRelaxation / finalTarget))
//
// and the derived bound gives 3968, so every value above it leaves the `min()` unchanged. The knob
// that governs is `finalMarchRelaxationTimes`.
//
// Two changes follow, and the second matters more than the first:
//   * sweep `finalMarchRelaxationTimes`, which actually controls the march;
//   * ASSERT THAT THE INDEPENDENT VARIABLE MOVED. A probe whose knob is disconnected prints
//     exactly what a real plateau prints, so it has to fail closed: if the march length does not
//     change between rows, say so loudly instead of reporting a trend.
// ---------------------------------------------------------------------------------------------
#include <cstdio>
#include <string>
#include <vector>

#include "cfd/physics/TemperatureProperty.hpp"
#include "cfd/species/MixtureThermodynamics.hpp"
#include "cfd/species/PremixedFlame.hpp"
#include "cfd/species/ReactionMechanism.hpp"
#include "cfd/species/SpeciesRegistry.hpp"
#include "cfd/thermal/Enthalpy.hpp"
#include "cfd/thermal/TemperatureDependentProperty.hpp"

using cfd::Index;
using cfd::Real;
using namespace cfd::species;

namespace {

// The test fixture's SYNTHETIC fuel, value for value from tests/unit/species/test_premixed_flame.cpp.
// Synthetic deliberately: debugging against methane would mean every decision was taken with G8's
// gated answer visible, which g8_reference.md §2 calls the integrity crux of the gate.
constexpr Real kWeight = 0.029;
constexpr Real kSpecificHeat = 1200.0;
constexpr Real kFuelFraction = 0.05;
constexpr Real kUnburntT = 300.0;
constexpr Real kPressure = 101325.0;
constexpr Real kConductivity = 0.05;
constexpr Real kActivation = 1.2e5;
constexpr Real kPreExponential = 3.0e7;
constexpr Real kMolarFormationP = -1.044e6;

SpeciesThermodynamics makeSpecies(const std::string& name) {
  return SpeciesThermodynamics(
      kWeight, cfd::thermal::SensibleEnthalpy(
                   cfd::thermal::TemperatureDependentProperty::constant(kSpecificHeat, name),
                   298.15, 0.0));
}

struct Problem {
  SpeciesRegistry registry;
  MixtureThermodynamics mixture;
  FormationEnthalpies formation;
  ReactionMechanism mechanism;
  ReactionKinetics kinetics;
};

Problem makeProblem() {
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
      "F->P", ArrheniusParameters(kPreExponential, 0.0, kActivation), std::nullopt});
  ReactionKinetics kinetics(mechanism, std::move(parameters));

  return Problem{registry, std::move(mixture), std::move(formation), std::move(mechanism),
                 std::move(kinetics)};
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
  settings.cells = 500;
  settings.domainLength = 0.010;
  settings.anchorFraction = 0.35;
  settings.tolerance = 1.0e-3;
  settings.maxOuterIterations = 40;
  settings.driftSettleTolerance = 0.05;
  settings.maximumStepsPerTrial = 20000;
  return settings;
}

}  // namespace

int main() {
  const Problem problem = makeProblem();
  const PremixedFlameChemistry chemistry{&problem.mechanism, &problem.mixture, &problem.formation,
                                         &problem.kinetics};
  // ConstantProperty, the same type the test uses. PremixedFlameConditions requires a
  // TemperatureProperty with no default, because a default would be an uncited constant in the
  // most sensitive position in the whole measurement -- S_L ~ sqrt(lambda).
  const cfd::physics::ConstantProperty conductivity(kConductivity);
  const PremixedFlameConditions conditions = makeConditions(conductivity);

  std::printf("TD-004 balance probe -- 500 cells, 10 mm domain, synthetic F->P\n");
  std::printf("test gates for reference:  fuel balance < 0.02   |sum Y - 1| < 1e-8\n");
  std::printf("linear acceptance fraction in PremixedFlame.cpp: 1e-9 of ||b||\n\n");

  std::printf("%8s %10s %7s %11s %12s %8s %12s %9s %9s %9s %8s %7s\n", "relaxT", "marchSteps",
              "steady", "dT resid", "fuel bal", "e/f", "|sumY-1|", "S_L cm/s", "x_front",
              "T_peak", "offCells", "recentr");

  // Cheapest first, so a run cut short still shows the trend. finalMarchTolerance is left at its
  // default: the question is whether MORE STEPS reach steadiness, not whether a looser bound
  // declares it -- loosening the bound to obtain a pass is what CLAUDE.md §5 forbids.
  // A 3x ladder on the knob that GOVERNS. The derived bound is
  // `finalMarchRelaxationTimes * finalRelaxation / finalTarget`, so tripling the relaxation times
  // triples the march -- provided it stays under the 200000 ceiling, which the guard below checks
  // by watching the march length the solver reports rather than the value requested.
  std::vector<Index> marchLengths;
  std::vector<Real> balances;

  for (const Real relaxationTimes : {10.0, 30.0, 90.0}) {
    PremixedFlameSettings settings = makeSettings();
    settings.finalMarchRelaxationTimes = relaxationTimes;
    // finalMarchTolerance is left at its default on purpose: the question is whether MORE STEPS
    // reach steadiness, not whether a looser bound declares it. Loosening a bound to obtain a pass
    // is what CLAUDE.md section 5 forbids.

    const PremixedFlameResult flame = solvePremixedFlame(chemistry, conditions, settings);

    const Real ratio = (flame.fuelBalanceResidual != 0.0)
                           ? flame.energyBalanceResidual / flame.fuelBalanceResidual
                           : 0.0;
    std::printf("%8.1f %10lld %7s %11.4e %12.6e %8.5f %12.4e %9.5f %9.5f %9.2f %8.2f %7lld\n",
                relaxationTimes, static_cast<long long>(flame.finalMarchSteps),
                flame.steadyStateReached ? "YES" : "no", flame.temperatureResidual,
                flame.fuelBalanceResidual, ratio, flame.maxCompositionSumDeviation,
                flame.flameSpeed * 100.0, flame.flamePosition * 1000.0, flame.peakTemperature,
                flame.worstFinalOffsetCells,
                static_cast<long long>(flame.finalMarchRecentrings));
    std::fflush(stdout);
    marchLengths.push_back(flame.finalMarchSteps);
    // TD-004: what the ledger says. The question this answers is whether the recentring's content
    // change ACCOUNTS for the balance residual, or is a separate smaller effect. `ledger rate` is
    // the apparent flux it represents; `fuelThrough` is what the balance normalises by, so the
    // ratio is directly the balance error the frame motion can explain.
    std::printf("        front: worst single-step JUMP %.2f cells, largest SHIFT applied %lld cells"
                " (dead band 3.0, physical motion ~0.09 cells/step)' + NL + '",
                flame.worstFrontJumpCells, static_cast<long long>(flame.largestShiftCells));
    std::printf("        refused: %lld front crossings, %lld shifts  (a refusal is the fix working;"
                " a flood of them means the profile has multiple crossings)' + NL + '",
                static_cast<long long>(flame.rejectedFrontCrossings),
                static_cast<long long>(flame.rejectedShifts));
    std::printf("        crossings: max %lld in one locate (1 => never more than one crossing)' + NL + '",
                static_cast<long long>(flame.maxFrontCrossings));
    std::printf("        rejected jumps: worst %.2f cells | <10c: %lld | 10-50c: %lld | >50c: %lld"
                "   (bound ~5.1 cells; dead band 3.0 so a legitimate jump can reach 6)' + NL + '",
                flame.worstRejectedJumpCells,
                static_cast<long long>(flame.rejectedJumpsUnder10Cells),
                static_cast<long long>(flame.rejectedJumps10To50Cells),
                static_cast<long long>(flame.rejectedJumpsOver50Cells));
    std::printf("        worst jump LANDED at x = %.4f mm | rejections landing ON the anchor: %lld"
                "   (anchor = 3.5000 mm; seedProfile() puts the front exactly there)' + NL + '",
                flame.worstRejectedJumpPosition * 1000.0,
                static_cast<long long>(flame.rejectedJumpsLandingAtAnchor));
    std::printf("        ledger: fuel %+.6e kg/m2  rate %+.6e kg/(m2 s)  energy %+.6e J/m2%s",
                flame.recentringFuelLedger, flame.recentringFuelLedgerRate,
                flame.recentringEnergyLedger, "\n");
    balances.push_back(flame.fuelBalanceResidual);
  }

  // THE GUARD. Without it this probe cannot tell a real plateau from a disconnected knob -- which
  // is exactly the mistake logs/02 records.
  std::printf("\n");
  bool moved = false;
  for (std::size_t i = 1; i < marchLengths.size(); ++i) {
    if (marchLengths[i] != marchLengths[0]) moved = true;
  }
  if (!moved) {
    std::printf("*** PROBE INVALID: the final march length did not change (%lld every time).\n",
                static_cast<long long>(marchLengths.empty() ? 0 : marchLengths[0]));
    std::printf("*** The independent variable is not connected, so NOTHING below is a trend.\n");
    return 1;
  }
  std::printf("independent variable MOVED: march length %lld -> %lld -> %lld  (guard passed)\n",
              static_cast<long long>(marchLengths[0]), static_cast<long long>(marchLengths[1]),
              static_cast<long long>(marchLengths[2]));
  const Real fall = (balances[0] != 0.0) ? balances[2] / balances[0] : 0.0;
  std::printf("fuel balance %.6e -> %.6e   ratio %.4f over a %.1fx longer march\n", balances[0],
              balances[2],
              fall, static_cast<Real>(marchLengths[2]) / static_cast<Real>(marchLengths[0]));

  std::printf("\nHOW TO READ THIS\n");
  std::printf("  Q1  worstLinRes <= ~1e-9      -> the inner solves are NOT the precision limit,\n");
  std::printf("                                   and audit.md section 1's clearing of TD-003 stands.\n");
  std::printf("  Q2  fuel bal falling with     -> the 4.33%% is a STOPPING-CRITERION artefact; the\n");
  std::printf("      march length                 gate should require steadiness.\n");
  std::printf("      fuel bal plateauing       -> a REAL conservation defect; no march length fixes\n");
  std::printf("                                   it and TD-004 must repair the discretisation.\n");
  std::printf("  Q3  e/f staying at ~1.0000    -> the two balances are ONE measurement (F12), and\n");
  std::printf("                                   the gate must not count them as two.\n");
  return 0;
}
