// TD-005 C3 -- LIMIT CYCLE OR SLOW DECAY? The measurement that decides whether C3 is fixable
// by a march-length rule at all.
//
// INVESTIGATION ONLY. No production file is changed by this probe.
//
// THE QUESTION. logs/14 established a periodic, nearly square oscillation of period 14 relaxation
// times, amplitude ~163 cells, with no decay over relaxT 10..46. Ten points over a 4.6x range
// cannot separate an UNDECAYING limit cycle from one whose decay time is longer than 46 relaxation
// times, and the distinction decides the fix:
//
//   LIMIT CYCLE   no march length converges. "March until the state stops changing" is unsound as
//                 a design, not merely slow, and C3 cannot be fixed by marching longer.
//   SLOW DECAY    a long enough march DOES converge, and a march-length criterion fixes C3.
//
// WHY NOT H.6's PROPOSED {60, 80, 120}. That sampling was written before the period was known, and
// against the period-14 model it fails:
//
//     predicted bad-window centres: 18, 32, 46, 60, 74, 88, 102, 116
//     relaxT  60 -> distance 0 from a centre -> BAD phase
//     relaxT  80 -> distance 6                -> GOOD phase
//     relaxT 120 -> distance 4                -> GOOD phase
//
// So two of its three points sit in the GOOD phase, where the offset is small WHETHER OR NOT the
// amplitude has decayed. The test as specified could not have answered its own question. Recorded
// rather than quietly substituted.
//
// THIS LADDER instead samples AT the predicted centres -- 60, 74, 88, 102, 116 -- which are cycles
// 4 through 8 of the model. That measures the amplitude where it should be largest, and it
// simultaneously extends the period prediction from 3 cycles to 8.
//
// PRE-REGISTERED, with a falsifier for every direction:
//
//   LIMIT CYCLE     all five rows BAD, |offset| ~163 with no downward trend
//   SLOW DECAY      all five BAD but |offset| shrinking monotonically along 60 -> 116
//   PERIOD WRONG    any row comes back GOOD. The period-14 model predicts all five are bad, so a
//                   single good row at a predicted centre refutes the model (or shows the period
//                   drifts with march length, which is itself a finding)
//   SOMETHING ELSE  a non-monotonic amplitude that is neither flat nor shrinking
//
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

  std::printf("TD-005 C3 -- LIMIT CYCLE OR SLOW DECAY?\n\n");
  std::printf("  driftWindow 32 (the admissible mdot), 500 cells / 10 mm, anchor cell %.0f\n",
              xAnchor / cellSize);
  std::printf("  sampling AT the period-14 model's predicted BAD centres: 60 74 88 102 116\n");
  std::printf("  (cycles 4..8). logs/14 measured amplitude ~163 cells with no decay over 10..46.\n");
  std::printf("  ALL FIVE are predicted BAD. A single GOOD row refutes the period-14 model.\n\n");
  std::printf("%8s %9s %9s %12s %9s %11s %10s %9s %9s %8s\n", "relaxT", "marchStp", "frontCel",
              "offset cell", "front mm", "fuel bal", "S_c cm/s", "delta mm", "S_L cm/s", "wall s");
  std::fflush(stdout);

  std::vector<Real> offsets;
  std::vector<Index> marchSteps;
  std::vector<Real> balances;
  const std::vector<Real> ladder = {60.0, 74.0, 88.0, 102.0, 116.0};

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

  // ---- the decay question, answered from the five amplitudes at the predicted extremes
  int goodRows = 0;
  for (const Real o : offsets) {
    if (std::abs(o) < 20.0) {
      ++goodRows;
    }
  }
  std::printf("  rows in the GOOD phase (|offset| < 20 cells): %d of %zu\n", goodRows,
              offsets.size());
  if (goodRows > 0) {
    std::printf("  => THE PERIOD-14 MODEL IS REFUTED, or the period DRIFTS with march length.\n");
    std::printf("     Every one of these relaxT values is a predicted bad-window centre, so a\n");
    std::printf("     good row here cannot be reconciled with a fixed period of 14.\n");
  } else {
    bool monotoneDecay = true;
    for (std::size_t i = 1; i < offsets.size(); ++i) {
      if (std::abs(offsets[i]) >= std::abs(offsets[i - 1])) {
        monotoneDecay = false;
      }
    }
    const Real firstAmp = std::abs(offsets.front());
    const Real lastAmp = std::abs(offsets.back());
    std::printf("  amplitude at cycle 4: %.1f cells    at cycle 8: %.1f cells   ratio %.3f\n",
                firstAmp, lastAmp, (firstAmp != 0.0) ? lastAmp / firstAmp : -1.0);
    if (monotoneDecay && lastAmp < 0.7 * firstAmp) {
      std::printf("  => SLOW DECAY. The amplitude shrinks monotonically, so a long enough march\n");
      std::printf("     converges and a march-length criterion CAN fix C3.\n");
    } else if (lastAmp > 0.9 * firstAmp) {
      std::printf("  => LIMIT CYCLE. The amplitude is undiminished at 8 periods, so NO march\n");
      std::printf("     length converges, 'march until steady' is unsound as a design, and C3\n");
      std::printf("     CANNOT be fixed by marching longer.\n");
    } else {
      std::printf("  => AMBIGUOUS: the amplitude moved but neither flat nor monotonically down.\n");
      std::printf("     Report it as such; do not pick whichever reading is convenient.\n");
    }
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
