// TD-005 C3 -- THE FINE LADDER. Is it a periodic oscillation, and what is the period?
//
// INVESTIGATION ONLY. No production file is changed by this probe.
//
// THE TEST, from dry_run.md H.6, first item:
//
//   "a FINE ladder in finalMarchRelaxationTimes must trace a smooth, roughly periodic curve in the
//    front offset. IF the pattern is irregular or non-repeating, this is not a simple oscillation
//    and section H is wrong."
//
// WHY THE LADDER IS 10..46 IN STEPS OF 4 AND NOT 10..44 IN STEPS OF 2. H.6 proposed step 2, which
// is 18 solves. Measured cost is ~0.021 s per marched step and each solve also re-runs ~15 300
// trial steps, so step 2 is ~2.7 HOURS -- past this session's budget for one run. The oscillation's
// apparent half-period is ~8000 steps = ~20 relaxT units (397 march steps per unit), so step 4
// still gives ~7 points per period, which is enough to tell a smooth curve from an irregular one.
// Step 2 would refine the period estimate, not change the verdict. The reduction is recorded rather
// than silently applied.
//
// WHAT EACH OUTCOME MEANS
//   SMOOTH AND REPEATING    section H holds; the period can be read off and compared with the
//                           flame's own timescale alpha/S_L^2 (~0.55 s for this fixture).
//   IRREGULAR               section H is WRONG -- it is not a simple oscillation, and the
//                           phase-at-stop explanation for C3, C2 and the sweeps collapses with it.
//   AMPLITUDE DECAYING      a transient, not a limit cycle: a long enough march WOULD converge,
//                           and the fix is a march-length criterion.
//   AMPLITUDE SUSTAINED     a limit cycle: NO march length converges, and "march until steady" is
//                           unsound as a design. This is the outcome with the largest consequence.
//
// A CROSS-CHECK THAT IS WORTH THE COLUMN. At the bad phase the offset is -163 cells from an anchor
// at cell 175, putting the front at cell ~12, x ~ 0.24 mm. TD-004's logs/15 measured its worst
// rejected crossing landing at 0.1969 mm -- cell 9. THE SAME PLACE. So TD-004's "flashback" reading
// was a correct observation of the oscillation's extreme phase and wrong only as a description of
// the steady state. The front cell index is printed so this stays checkable.
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

  const Real cellSize = kDomain / static_cast<Real>(kCells);
  const Real xAnchor = kAnchorFraction * kDomain;

  std::printf("TD-005 C3 -- IS IT PERIODIC, AND WHAT IS THE PERIOD?\n\n");
  std::printf("  driftWindow 32 (the admissible mdot), 500 cells / 10 mm, anchor cell %.0f\n",
              xAnchor / cellSize);
  std::printf("  known points: relaxT 10 -> +1.17 cells, 20 -> -163.08, 30 -> -162.24, 40 -> +1.21\n");
  std::printf("  TD-004 cross-check: its worst rejected crossing landed at 0.1969 mm = cell 9\n\n");
  std::printf("%8s %9s %9s %12s %9s %11s %10s %9s %9s %8s\n", "relaxT", "marchStp", "frontCel",
              "offset cell", "front mm", "fuel bal", "S_c cm/s", "delta mm", "S_L cm/s", "wall s");
  std::fflush(stdout);

  std::vector<Real> offsets;
  std::vector<Index> marchSteps;
  std::vector<Real> balances;
  const std::vector<Real> ladder = {10.0, 14.0, 18.0, 22.0, 26.0, 30.0, 34.0, 38.0, 42.0, 46.0};

  for (const Real times : ladder) {
    PremixedFlameSettings s;
    s.cells = kCells;
    s.domainLength = kDomain;
    s.anchorFraction = kAnchorFraction;
    s.tolerance = 1.0e-3;
    s.maxOuterIterations = 40;
    s.driftSettleTolerance = 0.05;
    s.maximumStepsPerTrial = 20000;
    s.driftWindow = 32;
    s.finalMarchRelaxationTimes = times;

    const auto t0 = std::chrono::steady_clock::now();
    const PremixedFlameResult f = solvePremixedFlame(chemistry, conditions, s);
    const Real wall = std::chrono::duration<Real>(std::chrono::steady_clock::now() - t0).count();

    const Real offset = (f.flamePosition - xAnchor) / cellSize;
    std::printf("%8.0f %9lld %9.0f %12.2f %9.4f %11.3e %10.5f %9.4f %9.5f %8.0f\n", times,
                static_cast<long long>(f.finalMarchSteps), f.flamePosition / cellSize, offset,
                f.flamePosition * 1000.0, f.fuelBalanceResidual, f.consumptionSpeed * 100.0,
                f.flameThickness * 1000.0, f.flameSpeed * 100.0, wall);
    std::fflush(stdout);
    offsets.push_back(offset);
    marchSteps.push_back(f.finalMarchSteps);
    balances.push_back(f.fuelBalanceResidual);
  }

  // ---- fail-closed: the independent variable must actually have moved the march
  bool monotone = true;
  for (std::size_t i = 1; i < marchSteps.size(); ++i) {
    if (marchSteps[i] <= marchSteps[i - 1]) {
      monotone = false;
    }
  }
  std::printf("\n  march steps %lld -> %lld, strictly increasing: %s\n",
              static_cast<long long>(marchSteps.front()),
              static_cast<long long>(marchSteps.back()), monotone ? "YES" : "*** NO ***");
  if (!monotone) {
    std::printf("  *** PROBE INVALID: finalMarchRelaxationTimes did not lengthen the march\n");
    std::printf("      monotonically, so this ladder is not a time series.\n");
    return 1;
  }

  // ---- the shape, reported from the numbers
  std::printf("\n  OFFSET (cells) against relaxT:\n   ");
  for (std::size_t i = 0; i < offsets.size(); ++i) {
    std::printf(" %+.0f", offsets[i]);
  }
  std::printf("\n");

  int signChanges = 0;
  for (std::size_t i = 1; i < offsets.size(); ++i) {
    if ((offsets[i] < 0.0) != (offsets[i - 1] < 0.0)) {
      ++signChanges;
    }
  }
  Real amp = 0.0;
  for (const Real o : offsets) {
    amp = std::max(amp, std::abs(o));
  }
  std::printf("  sign changes in the offset: %d      peak |offset|: %.1f cells\n", signChanges, amp);

  // amplitude in the FIRST half of the ladder versus the SECOND, for the decay question
  Real ampEarly = 0.0;
  Real ampLate = 0.0;
  const std::size_t half = offsets.size() / 2;
  for (std::size_t i = 0; i < offsets.size(); ++i) {
    if (i < half) {
      ampEarly = std::max(ampEarly, std::abs(offsets[i]));
    } else {
      ampLate = std::max(ampLate, std::abs(offsets[i]));
    }
  }
  std::printf("  peak |offset| early half %.1f cells   late half %.1f cells\n", ampEarly, ampLate);
  if (ampLate < 0.5 * ampEarly) {
    std::printf("  => AMPLITUDE DECAYING: a transient, not a limit cycle. A long enough march\n");
    std::printf("     would converge, and the fix is a march-length criterion.\n");
  } else if (ampLate > 0.5 * ampEarly && amp > 20.0) {
    std::printf("  => AMPLITUDE SUSTAINED over this ladder: consistent with a LIMIT CYCLE, in\n");
    std::printf("     which case NO march length converges and 'march until steady' is unsound.\n");
    std::printf("     A longer ladder is needed to call it a limit cycle rather than a slow decay.\n");
  } else {
    std::printf("  => the offset never leaves the anchor's neighbourhood on this ladder.\n");
  }

  std::printf("\n  FUEL BALANCE against relaxT:\n   ");
  for (std::size_t i = 0; i < balances.size(); ++i) {
    std::printf(" %.0e", balances[i]);
  }
  std::printf("\n");
  std::printf("\nHOW TO READ THIS\n");
  std::printf("  A smooth curve with regular sign changes confirms section H and gives the period.\n");
  std::printf("  An IRREGULAR, non-repeating pattern REFUTES section H: it would not be a simple\n");
  std::printf("  oscillation, and the phase-at-stop explanation for C3, TD-004's C2 and sweeps\n");
  std::printf("  A and C would collapse with it.\n");
  return 0;
}
