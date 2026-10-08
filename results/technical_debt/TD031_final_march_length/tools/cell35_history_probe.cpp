// TD-031 -- DUMP CELL 35 ACROSS CONSECUTIVE MARCH STEPS.
//
// INVESTIGATION ONLY, and with NO production change -- which is worth explaining, because the
// obvious way to get a per-step history is to add another diagnostic, and that is not necessary.
//
// THE TRICK. `finalMarchSteps` is a hard ceiling on the march budget:
//
//     finalBudget = min( finalMarchSteps,
//                        max( minimumStepsPerTrial, relaxT * relaxation / finalTarget ) )
//
// At relaxT 18 the derived value is 30949 and `minimumStepsPerTrial` is 128, so the budget is
// min(finalMarchSteps, 30949). Setting `finalMarchSteps` to 30945, 30946, ... therefore stops the
// march at exactly those step counts, and the reported field is the state AT that step. The
// steadiness break cannot fire early because its tolerance is unreachable. The solver is
// deterministic -- S_L has been identical across 30+ solves -- so run N+1 reproduces run N's first
// N steps. Five runs give five CONSECUTIVE states.
//
// WHAT IS BEING CHASED. logs/04 localised the saturated per-step change to CELL 35 at the converged
// state, on a step where NO recentring occurred, with worstStateChangeNoRecentre exactly equal to
// worstStateChange. Cell 35 is x = 0.70 mm, in the cold preheat zone at about 317 K, and the
// reported change there is 0.9896 of the full rise -- 1484 K, which would take that cell to 1801 K
// or -1167 K in a single step while the front, the balance and the thickness all sit steady.
//
// THE CANDIDATES THIS SEPARATES, and no prediction is registered between them because four
// successive guesses in this item have been wrong:
//
//   the cell OSCILLATES by ~1484 K each step      -> a real instability in advance(), localised
//                                                    to the preheat zone
//   the cell JUMPS ONCE and stays                 -> a one-off event, and `worstStateChange`
//                                                    being a max over all steps would then be
//                                                    reporting history rather than the state
//   the cell is SMOOTH across these steps         -> the reported change does not correspond to
//                                                    any change in this cell, and the comparison
//                                                    itself is wrong
//
// NEIGHBOURS 25..45 are printed too: a single bad cell and a bad REGION mean different things.
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
constexpr Real kAnchorFraction = 0.35;
constexpr Index kCells = 500;
constexpr Index kWatch = 35;  // the cell logs/04 localised the saturated change to

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

  std::printf("TD-031 -- CELL %lld ACROSS CONSECUTIVE MARCH STEPS\n\n",
              static_cast<long long>(kWatch));
  std::printf("  500 cells / cap 5 / relaxT 18, the CONVERGED configuration.\n");
  std::printf("  cell %lld is x = %.2f mm, in the cold preheat zone (~317 K).\n",
              static_cast<long long>(kWatch), kWatch * dx * 1000.0);
  std::printf("  logs/04 reported a per-step change of 0.9896 of the full rise THERE, on a step\n");
  std::printf("  with no recentring, at a state whose front/balance/thickness are all steady.\n");
  std::printf("  The march is stopped at successive budgets via finalMarchSteps, so no production\n");
  std::printf("  change is needed and these are genuinely consecutive states.\n\n");

  const Index budgets[] = {30945, 30946, 30947, 30948, 30949};
  std::vector<Real> watched;
  std::vector<std::vector<Real>> windows;

  std::printf("%10s %10s %18s %14s %12s %9s\n", "budget", "marchStp", "T[35] K", "dT from prev",
              "worstChange", "wall s");
  std::fflush(stdout);

  for (const Index b : budgets) {
    PremixedFlameSettings s;
    s.cells = kCells;
    s.domainLength = kDomain;
    s.anchorFraction = kAnchorFraction;
    s.tolerance = 1.0e-3;
    s.maxOuterIterations = 40;
    s.driftSettleTolerance = 0.05;
    s.driftWindow = 32;
    s.maximumCellDiffusionNumber = 5.0;
    s.finalMarchRelaxationTimes = 18.0;
    s.maximumStepsPerTrial = 80000;
    s.finalMarchSteps = b;  // the hard ceiling -- this is what stops the march at step b

    const auto t0 = std::chrono::steady_clock::now();
    const PremixedFlameResult f = solvePremixedFlame(chemistry, conditions, s);
    const Real wall = std::chrono::duration<Real>(std::chrono::steady_clock::now() - t0).count();

    const Real t35 = f.temperature[static_cast<std::size_t>(kWatch)];
    const Real prev = watched.empty() ? t35 : watched.back();
    std::printf("%10lld %10lld %18.11f %14.4e %12.4e %9.0f\n", static_cast<long long>(b),
                static_cast<long long>(f.finalMarchSteps), t35, t35 - prev, f.worstStateChange,
                wall);
    std::fflush(stdout);
    watched.push_back(t35);

    std::vector<Real> w;
    for (Index c = 25; c <= 45; c += 5) {
      w.push_back(f.temperature[static_cast<std::size_t>(c)]);
    }
    windows.push_back(w);
  }

  // ---------------------------------------------------------------- the neighbourhood
  std::printf("\n=== THE NEIGHBOURHOOD, cells 25 to 45 at each budget\n");
  std::printf("%10s", "budget");
  for (Index c = 25; c <= 45; c += 5) {
    std::printf("   T[%3lld] K", static_cast<long long>(c));
  }
  std::printf("\n");
  for (std::size_t i = 0; i < windows.size(); ++i) {
    std::printf("%10lld", static_cast<long long>(budgets[i]));
    for (const Real t : windows[i]) {
      std::printf(" %10.4f", t);
    }
    std::printf("\n");
  }

  // ---------------------------------------------------------------- verdict
  Real maxStep = 0.0;
  for (std::size_t i = 1; i < watched.size(); ++i) {
    maxStep = std::max(maxStep, std::abs(watched[i] - watched[i - 1]));
  }
  std::printf("\n=== VERDICT\n");
  std::printf("    T[%lld] over five consecutive steps: ", static_cast<long long>(kWatch));
  for (const Real t : watched) {
    std::printf(" %.6f", t);
  }
  std::printf("\n    largest consecutive change: %.6e K  =  %.4e of the full rise\n", maxStep,
              maxStep / 1500.0);
  std::printf("    the reported worstStateChange is 9.8957e-01 of the rise, i.e. 1484 K\n\n");
  if (maxStep > 100.0) {
    std::printf("    => THIS CELL REALLY DOES MOVE by order 1000 K per step. That is a real\n");
    std::printf("       instability in the march, localised to the cold preheat zone, and it is a\n");
    std::printf("       defect in its own right -- not an instrument artefact.\n");
  } else if (maxStep < 1.0) {
    std::printf("    => THIS CELL IS SMOOTH -- it moves less than 1 K per step. The reported\n");
    std::printf("       change does NOT correspond to any change in this cell, so the COMPARISON\n");
    std::printf("       is wrong: worstStateChangeCell does not identify the cell that produced\n");
    std::printf("       the maximum, or previousTemperature is not the previous state.\n");
  } else {
    std::printf("    => INTERMEDIATE (%.2f K per step). Neither reading is clean; report the\n", maxStep);
    std::printf("       numbers and do not round to either.\n");
  }
  return 0;
}
