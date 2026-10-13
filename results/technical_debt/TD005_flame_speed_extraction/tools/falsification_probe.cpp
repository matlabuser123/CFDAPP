// TD-005 C3 -- FALSIFICATION TESTS FOR THE INSTABILITY READING (dry_run.md section G.3).
//
// INVESTIGATION ONLY. No production file is changed by this probe.
//
// THE READING UNDER TEST, from dry_run.md G.3:
//   (a) the front sits at an ill-conditioned equilibrium
//   (b) with mdot slightly low it creeps upstream linearly while near the anchor
//   (c) past the anchor it runs away exponentially, e-folding ~530 steps
//   (d) the +-3 cell DEAD BAND gives that growth a free window in which recentring never acts
//   (e) once the band is exceeded the needed shift already exceeds the ~5.1-cell admissible-jump
//       bound, so the correction is refused
//   (f) a longer march buys more e-foldings, so EVERY mdot in the bracket eventually departs,
//       which is also TD-004's C2
//
// PREDICTIONS ARE PRE-REGISTERED WITH A FALSIFIER FOR EACH DIRECTION. The last time this item
// pre-registered a prediction (dry_run.md A.5) it named a falsifier for only ONE of the two
// directions, and the hypothesis then died by the direction that had not been nominated. That
// mistake is not repeated here.
//
// =================================================================================================
// TEST A -- THE DEAD BAND.  Tests (d) and (e).
// =================================================================================================
//
//   recentreDeadBandCells in {1.0, 3.0 (default), 6.0, 10.0}, bad mode, relaxT 10.
//
//   *** 0.5 WAS IN THE FIRST LADDER AND THE SOLVER REFUSED IT. PremixedFlame.cpp:147 enforces
//   `recentreDeadBandCells >= 1.0`, and its message states the very confound named below:
//
//       "must be >= 1. Below one cell the front is translated on essentially every step, and the
//        translation is not conservative -- it leaks fresh fuel off the upstream end."
//
//   So the guard already exists by design, the shrink direction is bounded at 1.0, and the test is
//   correspondingly weaker: a 3x reduction from the default rather than a 6x one. This is the
//   SECOND test level in this item that the solver validated away (the first was
//   anchorTemperatureFraction = 1.5 for C5's no-flame control), which is why the whole
//   validateSettings block was read before this re-run instead of one bound at a time.
//
//   IF (d) HOLDS   the departure scales with the band: a LARGER band gives the instability a
//                  wider free window, so |offset| and the imbalance should GROW with it; a
//                  SMALLER band should suppress or delay the departure.
//   FALSIFIED IF   |offset| is flat in the band (the band is not implicated at all), OR a smaller
//                  band makes the departure WORSE rather than better.
//
//   A CONFOUND THAT MUST BE NAMED, because it would otherwise look like confirmation. A band near
//   zero recentres almost every step, and TD-004 measured that a +1/-1 recentring oscillation
//   leaks fuel at about one cell every two steps -- a BAD BALANCE for an entirely different
//   reason. The two are separable by the FRONT OFFSET:
//
//       balance bad  AND  offset SMALL  ->  TD-004's recentring leak, not the instability
//       balance bad  AND  offset LARGE  ->  the instability
//
//   so both are reported side by side and neither verdict is read from the balance alone.
//
// =================================================================================================
// TEST B -- THE GOOD MODE PAST ~7 E-FOLDINGS.  Tests (c) and (f), and reproduces TD-004's C2.
// =================================================================================================
//
//   driftWindow 32 (the admissible mdot) at relaxT in {10, 20, 30, 40}.
//
//   IF (f) HOLDS   the good mode DEPARTS TOO, just later: |offset| grows along the ladder and the
//                  balance degrades with it. TD-004's C2 ladder (0.0153 / 0.6022 / 0.7188 at
//                  relaxT 10/30/90) should reappear -- but now with a MEASURED front position
//                  beside it, which TD-004 could not have had.
//   FALSIFIED IF   the good mode's front stays within a few cells of the anchor at relaxT 40 while
//                  its balance degrades anyway. That would mean the imbalance is NOT the front
//                  running away, and (f) -- the unification of C2 with C3 -- is wrong.
//
//   This also serves as the small-displacement test: the good mode's front sits at about +1.2
//   cells, so watching whether that grows or decays over four times the march length IS the
//   "does a small displacement grow?" experiment.
//
// =================================================================================================
// WHAT IS NOT TESTABLE HERE, and is reported rather than approximated
// =================================================================================================
//
//   G.5's first test -- displace the SEEDED front by a chosen number of cells and watch it grow or
//   decay -- is NOT RUNNABLE without a production change. The seed is built around xAnchor
//   (`tanh((xCentre[c] - xAnchor)/ramp)`), and no setting displaces the seeded front RELATIVE to
//   the anchor: `anchorFraction` moves both together, and `seedWidthFactor` changes the width
//   rather than the position. Adding a seed-offset setting is a production change and is not
//   authorized, so this test is declared unrunnable instead of being replaced by a confounded
//   proxy. Test B's +1.2-cell starting offset is the closest available substitute and is weaker,
//   because the displacement is not chosen.
#include <chrono>
#include <cmath>
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

constexpr Real kWeight = 0.029;
constexpr Real kSpecificHeat = 1200.0;
constexpr Real kFuelFraction = 0.05;
constexpr Real kUnburntT = 300.0;
constexpr Real kPressure = 101325.0;
constexpr Real kConductivity = 0.05;
constexpr Real kActivation = 1.2e5;
constexpr Real kPreExponential = 3.0e7;
constexpr Real kMolarFormationP = -1.044e6;
constexpr Real kDomain = 0.010;
constexpr Index kCells = 500;
constexpr Real kAnchorFraction = 0.35;

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
  FormationEnthalpies formation = FormationEnthalpies::fromMolar(mixture, {0.0, kMolarFormationP});
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

PremixedFlameSettings baseSettings() {
  PremixedFlameSettings settings;
  settings.cells = kCells;
  settings.domainLength = kDomain;
  settings.anchorFraction = kAnchorFraction;
  settings.tolerance = 1.0e-3;
  settings.maxOuterIterations = 40;
  settings.driftSettleTolerance = 0.05;
  settings.maximumStepsPerTrial = 20000;
  return settings;
}

struct Row {
  Real knob{};
  Real offsetCells{};
  Real fuelBalance{};
  Real consumption{};
  Real thickness{};
  Real flameSpeed{};
  Index marchSteps{};
  Index finalRecentrings{};
  Index rejects{};
  Real worstJump{};
  Real wall{};
};

Row solveRow(const PremixedFlameChemistry& chem, const PremixedFlameConditions& cond,
             const PremixedFlameSettings& s, Real knob) {
  const Real cellSize = kDomain / static_cast<Real>(kCells);
  const Real xAnchor = kAnchorFraction * kDomain;
  const auto t0 = std::chrono::steady_clock::now();
  const PremixedFlameResult f = solvePremixedFlame(chem, cond, s);
  Row r;
  r.knob = knob;
  r.offsetCells = (f.flamePosition - xAnchor) / cellSize;
  r.fuelBalance = f.fuelBalanceResidual;
  r.consumption = f.consumptionSpeed;
  r.thickness = f.flameThickness;
  r.flameSpeed = f.flameSpeed;
  r.marchSteps = f.finalMarchSteps;
  r.finalRecentrings = f.finalMarchRecentrings;
  r.rejects = f.rejectedFrontCrossings;
  r.worstJump = f.worstFrontJumpCells;
  r.wall = std::chrono::duration<Real>(std::chrono::steady_clock::now() - t0).count();
  return r;
}

void printRow(const char* label, const Row& r) {
  std::printf("%10s %12.2f %11.3e %10.5f %9.4f %9.5f %8lld %8lld %8lld %8.2f %8.0f\n", label,
              r.offsetCells, r.fuelBalance, r.consumption * 100.0, r.thickness * 1000.0,
              r.flameSpeed * 100.0, static_cast<long long>(r.marchSteps),
              static_cast<long long>(r.finalRecentrings), static_cast<long long>(r.rejects),
              r.worstJump, r.wall);
  std::fflush(stdout);
}

void header(const char* knobName) {
  std::printf("%10s %12s %11s %10s %9s %9s %8s %8s %8s %8s %8s\n", knobName, "offset cell",
              "fuel bal", "S_c cm/s", "delta mm", "S_L cm/s", "march", "recentr", "rejects",
              "worstJmp", "wall s");
  std::fflush(stdout);
}

}  // namespace

int main() {
  const Problem problem = makeProblem();
  const cfd::physics::ConstantProperty conductivity(kConductivity);
  const PremixedFlameChemistry chemistry{&problem.mechanism, &problem.mixture, &problem.formation,
                                         &problem.kinetics};
  PremixedFlameConditions conditions;
  conditions.pressure = kPressure;
  conditions.unburntTemperature = kUnburntT;
  conditions.unburntComposition = {kFuelFraction, 1.0 - kFuelFraction};
  conditions.conductivity = &conductivity;
  conditions.specificHeat = kSpecificHeat;
  conditions.lewisNumber = 1.0;
  conditions.fuelIndex = 0;

  std::printf("TD-005 C3 -- FALSIFICATION TESTS for the instability reading (dry_run G.3)\n");
  std::printf("baseline for comparison, bad mode at the default band 3.0, relaxT 10:\n");
  std::printf("  offset -46.58 cells   fuel bal 3.647e-01   S_c 3.04347   delta 0.7365\n\n");

  // ===================================================================== TEST A -- the dead band
  std::printf("=== TEST A -- recentreDeadBandCells, bad mode (driftWindow 16), relaxT 10\n");
  std::printf("    (d) predicts |offset| GROWS with the band and shrinks as the band shrinks.\n");
  std::printf("    FALSIFIED IF flat in the band, or if a SMALLER band is WORSE.\n");
  std::printf("    CONFOUND: a tiny band recentres nearly every step and leaks fuel (TD-004).\n");
  std::printf("    Separate by offset: bad balance + SMALL offset = leak; + LARGE offset = instability.\n\n");
  header("deadBand");
  std::vector<Row> testA;
  for (const Real band : {1.0, 3.0, 6.0, 10.0}) {
    auto s = baseSettings();
    s.driftWindow = 16;
    s.finalMarchRelaxationTimes = 10.0;
    s.recentreDeadBandCells = band;
    char label[16];
    std::snprintf(label, sizeof(label), "%.1f", band);
    testA.push_back(solveRow(chemistry, conditions, s, band));
    printRow(label, testA.back());
  }
  {
    Real lo = std::abs(testA.front().offsetCells);
    Real hi = lo;
    for (const Row& r : testA) {
      lo = std::min(lo, std::abs(r.offsetCells));
      hi = std::max(hi, std::abs(r.offsetCells));
    }
    std::printf("\n    |offset| range over the band sweep: %.2f .. %.2f cells\n", lo, hi);
    if (hi - lo < 5.0) {
      std::printf("    => FLAT in the dead band (range < 5 cells against a 46-cell departure).\n");
      std::printf("       (d) IS FALSIFIED: the band is not what gives the instability its window,\n");
      std::printf("       and the departure happens regardless of it.\n");
    } else if (std::abs(testA.front().offsetCells) < std::abs(testA.back().offsetCells)) {
      std::printf("    => |offset| grows with the band, as (d) predicts.\n");
    } else {
      std::printf("    => |offset| SHRINKS as the band grows -- the OPPOSITE of (d). FALSIFIED.\n");
    }
    std::fflush(stdout);
  }

  // ======================================================= TEST B -- the good mode, longer march
  std::printf("\n=== TEST B -- the GOOD mode (driftWindow 32) past ~7 e-foldings\n");
  std::printf("    (f) predicts it departs too, later: |offset| grows and the balance follows.\n");
  std::printf("    FALSIFIED IF the front stays near the anchor at relaxT 40 while the balance\n");
  std::printf("    degrades anyway -- then the imbalance is not the front running away.\n");
  std::printf("    TD-004's C2 for reference: 0.0153 / 0.6022 / 0.7188 at relaxT 10 / 30 / 90.\n\n");
  header("relaxT");
  std::vector<Row> testB;
  for (const Real times : {10.0, 20.0, 30.0, 40.0}) {
    auto s = baseSettings();
    s.driftWindow = 32;
    s.finalMarchRelaxationTimes = times;
    char label[16];
    std::snprintf(label, sizeof(label), "%.0f", times);
    testB.push_back(solveRow(chemistry, conditions, s, times));
    printRow(label, testB.back());
  }
  {
    const Real first = std::abs(testB.front().offsetCells);
    const Real last = std::abs(testB.back().offsetCells);
    std::printf("\n    |offset| %.2f -> %.2f cells;  fuel balance %.3e -> %.3e\n", first, last,
                testB.front().fuelBalance, testB.back().fuelBalance);
    const bool balanceDegraded = testB.back().fuelBalance > 10.0 * testB.front().fuelBalance;
    const bool frontDeparted = last > 10.0;
    if (frontDeparted && balanceDegraded) {
      std::printf("    => the GOOD mode departs too, and the balance follows it. (f) HOLDS, and\n");
      std::printf("       TD-004's C2 and TD-005's C3 are ONE mechanism.\n");
    } else if (!frontDeparted && balanceDegraded) {
      std::printf("    => (f) IS FALSIFIED: the balance degrades while the front stays put, so the\n");
      std::printf("       imbalance is NOT the front running away and C2 is a different defect.\n");
    } else if (!frontDeparted && !balanceDegraded) {
      std::printf("    => the good mode holds at relaxT 40. (f) is NOT CONFIRMED here -- it may\n");
      std::printf("       need more e-foldings, which is a weaker statement than a refutation.\n");
    } else {
      std::printf("    => the front departed but the balance did NOT follow. That breaks the link\n");
      std::printf("       between offset and imbalance that G.2.4 measured, and needs explaining.\n");
    }
    std::fflush(stdout);
  }

  std::printf("\nNOT RUN: G.5's seed-displacement test. The seed is built around xAnchor and no\n");
  std::printf("setting displaces it RELATIVE to the anchor, so that test needs a production change\n");
  std::printf("and is declared unrunnable rather than replaced by a confounded proxy.\n");
  return 0;
}
