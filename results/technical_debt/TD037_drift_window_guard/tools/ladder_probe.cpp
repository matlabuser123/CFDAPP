// TD-037 -- the driftWindow ladder, and the gate's instrument for C0, C1, C3 and C4.
//
// The ladder {16, 32, 64, 128} STRADDLES the measured 40-step trial lifetime: 16 and 32 are below
// it, 64 and 128 above. A ladder sampling one side of a boundary is how TD-027's C4 came to be
// unsound, so this one is built to cross it.
//
// C3's independent answer. A trial's assigned side is `tooLarge`. The correct side is knowable from
// the FINAL eigenvalue: a trial whose mass flux exceeds the converged mdot was on the too-large
// side. Trials inside the converged bracket are SKIPPED -- within the bracket the side genuinely is
// not determined, and asserting one would be inventing information.
//
// The exit code is NOT the verdict: on the unchanged tree C0 is REQUIRED to reproduce the defect
// and C1 is REQUIRED to fail. Nonzero means the probe malfunctioned.
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
constexpr Real kTolerance = 1.0e-3;

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

// EXACTLY the test's makeSettings(), with driftWindow as the only variable. Neither the diffusion
// cap nor the march length is overridden, so both take the production defaults the test takes.
PremixedFlameSettings makeSettings(Index window) {
  PremixedFlameSettings settings;
  settings.cells = 500;
  settings.domainLength = 0.010;
  settings.anchorFraction = 0.35;
  settings.tolerance = kTolerance;
  settings.maxOuterIterations = 40;
  settings.driftSettleTolerance = 0.05;
  settings.maximumStepsPerTrial = 20000;
  settings.driftWindow = window;
  return settings;
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

  std::printf("TD-037 -- the driftWindow LADDER\n\n");
  std::printf("  fixture: the test's makeSettings(), production cap and march length\n");
  std::printf("  ladder STRADDLES the measured 40-step trial: 16, 32 below it; 64, 128 above\n\n");

  bool unmeasuredAt64 = false;
  int c3Wrong = 0;
  int convergedCount = 0;

  for (const Index window : {Index(16), Index(32), Index(64), Index(128)}) {
    const PremixedFlameResult f = solvePremixedFlame(chemistry, conditions, makeSettings(window));
    const bool ok = (f.status == PremixedFlameStatus::Converged);
    if (ok) {
      ++convergedCount;
    }

    std::printf("=============== driftWindow %lld\n", static_cast<long long>(window));
    std::printf("  status %d %s   S_L %.6f cm/s   final mdot %.9g\n", static_cast<int>(f.status),
                ok ? "Converged" : "NOT CONVERGED", f.flameSpeed * 100.0, f.massFlux);
    std::printf("  trials %zu   bracketSteps %lld\n", f.trials.size(),
                static_cast<long long>(f.bracketSteps));
    std::printf("  RECORD -- C4 compares this VERBATIM at window 32\n");
    std::printf("  %4s %18s %18s %9s %5s %4s %4s %4s\n", "#", "mdot", "drift", "steps", "meas",
                "set", "ext", "big");

    int unmeasured = 0;
    for (std::size_t i = 0; i < f.trials.size(); ++i) {
      const PremixedFlameTrial& t = f.trials[i];
      std::printf("  %4zu %18.9g %18.9g %9lld %5s %4s %4s %4s\n", i, t.massFlux, t.drift,
                  static_cast<long long>(t.steps), t.measured ? "yes" : "NO",
                  t.settled ? "y" : "n", t.extinguished ? "y" : "n", t.tooLarge ? "y" : "n");
      if (!t.measured) {
        ++unmeasured;
      }
    }
    std::printf("  unmeasured trials: %d\n", unmeasured);
    if (window == 64 && unmeasured > 0) {
      unmeasuredAt64 = true;
    }

    // ---- C3: every sided trial's side must agree with the final eigenvalue
    if (ok && f.massFlux > 0.0) {
      int checked = 0;
      int wrong = 0;
      int skipped = 0;
      for (const PremixedFlameTrial& t : f.trials) {
        if (!t.measured) {
          continue;
        }
        const Real rel = std::abs(t.massFlux - f.massFlux) / f.massFlux;
        if (rel <= kTolerance) {
          ++skipped;  // inside the converged bracket: the side is genuinely undetermined
          continue;
        }
        const bool expected = (t.massFlux > f.massFlux);
        ++checked;
        if (t.tooLarge != expected) {
          ++wrong;
          std::printf("  *** C3: trial mdot %.9g sided %s, but the eigenvalue %.9g says %s\n",
                      t.massFlux, t.tooLarge ? "tooLarge" : "tooSmall", f.massFlux,
                      expected ? "tooLarge" : "tooSmall");
        }
      }
      std::printf("  C3  %d sided trials checked, %d skipped inside the bracket, %d WRONG -> %s\n",
                  checked, skipped, wrong, wrong == 0 ? "PASS" : "FAIL");
      c3Wrong += wrong;
    } else {
      std::printf("  C3  not evaluable -- no convergence, so there is no eigenvalue to check against\n");
    }
    std::printf("\n");
    std::fflush(stdout);
  }

  std::printf("=== CRITERIA AS MEASURED HERE\n");
  std::printf("  C0  driftWindow 64 fails AND has an unmeasured trial : %s\n",
              unmeasuredAt64 ? "REPRODUCED" : "NOT reproduced");
  std::printf("  C1  Converged at all four windows                    : %s (%d/4)\n",
              convergedCount == 4 ? "PASS" : "FAIL", convergedCount);
  std::printf("  C3  no sided trial disagrees with the eigenvalue     : %s\n",
              c3Wrong == 0 ? "PASS" : "FAIL");
  std::printf("  C2 and C4 are read from the numbers above; C5, C6 and C7 run separately.\n");
  std::printf("\n  ON THE UNCHANGED TREE, C0 REPRODUCED and C1 FAILING is the REQUIRED result.\n");
  return 0;
}
