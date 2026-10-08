// TD-004 AUDIT -- THE RAW LOCATED-POSITION TRACE, by stopping the final march at chosen budgets.
//
// WHY THIS AND NOT ANOTHER COUNTER. TD-004 has now absorbed five refuted diagnoses before this
// session and two more today -- a self-sealing refusal bound and an inherited trial state, both
// killed by measurement. The standing lesson from this item is that at three refutations the
// INSTRUMENT CLASS is the suspect, not diagnosis n+1: every observable so far has been a cumulative
// counter, and a cumulative counter cannot say WHEN anything happened.
//
// THE TRICK, and it needs NO production change. `finalMarchSteps` is a hard ceiling on the final
// march budget:
//
//     finalBudget = min( finalMarchSteps, max( minimumStepsPerTrial, relaxT*relaxation/finalTarget ) )
//
// so setting it to 1, 10, 50 ... stops the march at exactly that step, and the reported state is the
// state AT that step. The solver is deterministic -- S_L has been bit-identical across dozens of
// solves -- so run N+1 reproduces run N's first N steps. DIFFERENCING consecutive rows therefore
// gives what happened in each interval, which is the trace the cumulative counters cannot provide.
//
// The front position comes from `flamePosition`, which TD-005's C4d made an unconditional scan of
// the reported field rather than an echo of the anchor input. It is the TRUE crossing, independent
// of whatever the control locator decided.
//
// WHAT EACH COLUMN ANSWERS:
//   front / offCells     where the front actually is, per step
//   finalRecentres       WHEN the single final-march recentring happens
//   rejected<10/10-50/>50  WHEN the locator starts refusing, and at what distance
//   worstRejPos          WHERE the refused crossing sat -- near the true front, or near the anchor
//   maxCross             whether a SECOND crossing exists for the locator to prefer
//
// NO PREDICTION IS REGISTERED between the surviving mechanisms. Two of mine have already died on
// this item today and the point of a raw trace is to stop guessing.
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
constexpr Real kAnchorFraction = 0.35;
constexpr Index kCells = 500;
constexpr Real kDtQuarter = 9.4243e-06;

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

  const Real dx = kDomain / static_cast<Real>(kCells);
  const Real xAnchor = kAnchorFraction * kDomain;

  std::printf("TD-004 AUDIT -- RAW LOCATED-POSITION TRACE via the march budget ceiling\n\n");
  std::printf("  500 cells, dt %.4e pinned, relaxT 10 (derived budget 17194), anchor %.3f mm\n",
              kDtQuarter, xAnchor * 1000.0);
  std::printf("  dead band 3.0 cells; the recentre operator acts only for 3.000 < |off| < ~4.234\n");
  std::printf("  DIFFERENCE consecutive rows: cumulative counters then describe each interval.\n\n");
  std::printf("%8s %11s %9s %7s %6s %7s %6s %11s %11s %5s\n", "budget", "front mm", "offCells",
              "finalRc", "rej<10", "rej10-50", "rej>50", "worstRejC", "worstRejPos", "maxX");
  std::fflush(stdout);

  const Index budgets[] = {1, 10, 50, 100, 500, 1000, 5000, 17194};

  for (const Index b : budgets) {
    PremixedFlameSettings s;
    s.cells = kCells;
    s.domainLength = kDomain;
    s.anchorFraction = kAnchorFraction;
    s.tolerance = 1.0e-3;
    s.maxOuterIterations = 40;
    s.driftSettleTolerance = 0.05;
    s.driftWindow = 32;
    s.timeStep = kDtQuarter;
    s.finalMarchRelaxationTimes = 10.0;
    s.maximumStepsPerTrial = 80000;
    s.finalMarchSteps = b;  // the hard ceiling -- this is what stops the march at step b

    const PremixedFlameResult f = solvePremixedFlame(chemistry, conditions, s);
    if (f.temperature.empty()) {
      std::printf("%8lld  *** EMPTY reported field -- the probe cannot measure\n",
                  static_cast<long long>(b));
      return 1;
    }
    std::printf("%8lld %11.6f %9.3f %7lld %6lld %8lld %6lld %11.3f %11.6f %5lld\n",
                static_cast<long long>(f.finalMarchSteps), f.flamePosition * 1000.0,
                (f.flamePosition - xAnchor) / dx,
                static_cast<long long>(f.finalMarchRecentrings),
                static_cast<long long>(f.rejectedJumpsUnder10Cells),
                static_cast<long long>(f.rejectedJumps10To50Cells),
                static_cast<long long>(f.rejectedJumpsOver50Cells), f.worstRejectedJumpCells,
                f.worstRejectedJumpPosition * 1000.0,
                static_cast<long long>(f.maxFrontCrossings));
    std::fflush(stdout);
  }

  std::printf("\n=== HOW TO READ THIS\n");
  std::printf("  The rejection counters are CUMULATIVE OVER THE WHOLE SOLVE, trials included, so\n");
  std::printf("  their absolute values carry the search's history. Only the DIFFERENCES between\n");
  std::printf("  consecutive rows belong to the final march, and only those should be read.\n");
  std::printf("  If a difference is zero across an interval in which the front moved, then the\n");
  std::printf("  locator neither accepted nor refused anything there -- which would mean it was\n");
  std::printf("  not called at all, and the question moves to why.\n");
  return 0;
}
