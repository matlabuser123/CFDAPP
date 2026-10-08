// TD-032 -- THE DISCRIMINATING PROBE. Is the post-fix `NoFlame` at driftWindow 64 TRUTHFUL, or did
// the fix lose a capability?
//
// THE QUESTION. `TheAnswerIsInsensitiveToTheStepsPerTrial` was recorded PASS twice pre-fix and now
// FAILS: at driftWindow 64 the solver returns NoFlame. attribution.md established that the FIX
// caused that transition, and explicitly did NOT establish which of these holds:
//
//   (b) LOST CAPABILITY   a configuration that genuinely had a flame no longer finds one
//   (c) TRUTHFUL FAILURE  the configuration never had a flame, and the pre-fix ALTERNATION hid
//                         that -- its hot branch is a nearly fully burnt field, which crosses
//                         T_anchor everywhere, so a "flame" was found whatever the physical state
//
// HOW THIS DISCRIMINATES, and it needs no production change. `finalise()` is the single return path
// and fills the reported profiles even on a failure status, so `result.temperature` is populated on
// a NoFlame return. The anchor temperature is
//
//     T_anchor = T_u + anchorTemperatureFraction * (T_ad - T_u) = 300 + 0.5 * 1500 = 1050 K
//
// so the field itself answers whether anything crosses it.
//
// PRE-REGISTERED READINGS, ONE PER DIRECTION rather than one per hypothesis. Three of this
// campaign's falsifiers died by a direction that had not been nominated, including this item's own,
// so every outcome below is named BEFORE the run:
//
//   POST-FIX window 64
//     zero crossings of 1050 K, and max T below 1050 K   -> NoFlame is LITERALLY TRUE. (c).
//     zero crossings but max T above 1050 K              -> a crossing exists in temperature but
//                                                           not as the locator counts it: look at
//                                                           the profile before concluding.
//     one or more crossings                              -> NoFlame is a FALSE REPORT. That is a
//                                                           reporting defect in its own right and
//                                                           favours (b).
//   PRE-FIX window 64  (it PASSED, so it found something -- the question is what)
//     reported field is the HOT branch (upstream cells near T_ad)  -> the PASS was an alternation
//                                                           artefact. (c), from the other side.
//     reported field is the COLD branch with a crossing near the
//     anchor at 3.5 mm                                   -> the pre-fix flame was REAL. (b).
//
//   WINDOW 32 IS THE INTERNAL CONTROL: it passes on both libraries. If the pre-fix window-32 field
//   is ALSO a hot-branch field, then the pre-fix "pass" was artefactual at every window, which
//   strengthens (c) independently of window 64.
//
// The exit code is NOT the verdict. Nonzero means the probe malfunctioned.
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

// exactly the test's constants -- the probe must reproduce the FAILURE, not something near it
constexpr Real kWeight = 0.029;
constexpr Real kSpecificHeat = 1200.0;
constexpr Real kFuelFraction = 0.05;
constexpr Real kUnburntT = 300.0;
constexpr Real kPressure = 101325.0;
constexpr Real kConductivity = 0.05;
constexpr Real kActivation = 1.2e5;
constexpr Real kPreExponential = 3.0e7;
constexpr Real kMolarFormationP = -1.044e6;
constexpr Real kExpectedAdiabatic = 1800.0;
constexpr Real kAnchorT = 1050.0;  // T_u + 0.5 (T_ad - T_u), the default anchor fraction

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

// EXACTLY the test's makeSettings(), with driftWindow as the only variable. No cap and no march
// length is overridden, so both take the production defaults the test takes.
PremixedFlameSettings makeSettings(Index window) {
  PremixedFlameSettings settings;
  settings.cells = 500;
  settings.domainLength = 0.010;
  settings.anchorFraction = 0.35;
  settings.tolerance = 1.0e-3;
  settings.maxOuterIterations = 40;
  settings.driftSettleTolerance = 0.05;
  settings.maximumStepsPerTrial = 20000;
  settings.driftWindow = window;
  return settings;
}

const char* statusName(PremixedFlameStatus s) {
  switch (s) {
    case PremixedFlameStatus::Converged: return "Converged";
    case PremixedFlameStatus::MaxIterations: return "MaxIterations";
    case PremixedFlameStatus::NoFlame: return "NoFlame";
    case PremixedFlameStatus::LinearSolveFailure: return "LinearSolveFailure";
    case PremixedFlameStatus::NonFiniteState: return "NonFiniteState";
    case PremixedFlameStatus::InvalidConfiguration: return "InvalidConfiguration";
  }
  return "?";
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

  std::printf("TD-032 -- THE DISCRIMINATING PROBE: is NoFlame at driftWindow 64 truthful?\n\n");
  std::printf("  T_anchor = %.1f K   T_ad(expected) = %.1f K   anchor x = 3.500 mm\n", kAnchorT,
              kExpectedAdiabatic);
  std::printf("  The reported field is filled even on a failure status (finalise() is the single\n");
  std::printf("  return path), so a NoFlame result still carries the profile that explains it.\n\n");

  int probeFailures = 0;

  for (const Index window : {Index(32), Index(64)}) {
    const auto t0 = std::chrono::steady_clock::now();
    const PremixedFlameResult f =
        solvePremixedFlame(chemistry, conditions, makeSettings(window));
    const Real wall = std::chrono::duration<Real>(std::chrono::steady_clock::now() - t0).count();

    std::printf("================ driftWindow %lld   (%.0f s)\n", static_cast<long long>(window),
                wall);
    std::printf("  status            %s\n", statusName(f.status));
    if (f.temperature.empty()) {
      std::printf("  *** THE REPORTED FIELD IS EMPTY. finalise() did not fill it, which would be a\n");
      std::printf("      production defect in its own right and makes this probe unable to answer.\n");
      ++probeFailures;
      continue;
    }

    const Index nc = static_cast<Index>(f.temperature.size());
    Real tMin = f.temperature[0];
    Real tMax = f.temperature[0];
    for (Index c = 0; c < nc; ++c) {
      tMin = std::min(tMin, f.temperature[c]);
      tMax = std::max(tMax, f.temperature[c]);
    }

    // ---- how many times does the field cross T_anchor?
    Index crossings = 0;
    std::vector<Real> crossingX;
    for (Index c = 0; c + 1 < nc; ++c) {
      const Real a = f.temperature[c] - kAnchorT;
      const Real b = f.temperature[c + 1] - kAnchorT;
      if (a * b <= 0.0 && f.temperature[c] != f.temperature[c + 1]) {
        ++crossings;
        const Real xc = f.x[static_cast<std::size_t>(c)] +
                        (kAnchorT - f.temperature[c]) /
                            (f.temperature[c + 1] - f.temperature[c]) *
                            (f.x[static_cast<std::size_t>(c + 1)] - f.x[static_cast<std::size_t>(c)]);
        if (crossingX.size() < 6) {
          crossingX.push_back(xc);
        }
      }
    }

    std::printf("  T range           %.4f .. %.4f K\n", tMin, tMax);
    std::printf("  peakTemperature   %.4f K\n", f.peakTemperature);
    std::printf("  flamePosition     %.6f mm      (anchor is 3.500 mm)\n",
                f.flamePosition * 1000.0);
    std::printf("  S_L               %.5f cm/s\n", f.flameSpeed * 100.0);
    std::printf("  CROSSINGS of %.0f K  %lld", kAnchorT, static_cast<long long>(crossings));
    for (const Real xc : crossingX) {
      std::printf("   at %.4f mm", xc * 1000.0);
    }
    std::printf("\n");

    // ---- cold branch or hot branch? The upstream cells are the discriminator: the cold branch
    //      sits near T_u there, the hot branch near T_ad.
    std::printf("  upstream profile  ");
    for (const Index c : {Index(5), Index(15), Index(25), Index(35), Index(45)}) {
      if (c < nc) {
        std::printf("T[%lld]=%.1f  ", static_cast<long long>(c), f.temperature[c]);
      }
    }
    std::printf("\n");

    const Real upstream = f.temperature[25];
    const bool hotBranch = upstream > 0.5 * (kUnburntT + kExpectedAdiabatic);
    std::printf("  BRANCH            %s  (T[25] = %.1f K against the midpoint %.1f K)\n",
                hotBranch ? "HOT -- upstream is nearly fully burnt" : "COLD -- upstream is preheat",
                upstream, 0.5 * (kUnburntT + kExpectedAdiabatic));

    // ---- the pre-registered readings, applied
    std::printf("  READING           ");
    if (f.status == PremixedFlameStatus::NoFlame) {
      if (crossings == 0 && tMax < kAnchorT) {
        std::printf("NoFlame is LITERALLY TRUE -- nothing reaches %.0f K anywhere.\n", kAnchorT);
      } else if (crossings == 0) {
        std::printf("no crossing, but max T %.1f EXCEEDS %.0f K -- read the profile.\n", tMax,
                    kAnchorT);
      } else {
        std::printf("*** %lld CROSSINGS EXIST yet NoFlame was reported -- a FALSE REPORT.\n",
                    static_cast<long long>(crossings));
      }
    } else if (hotBranch) {
      std::printf("converged on a HOT-BRANCH field -- the pass is an alternation artefact.\n");
    } else if (crossings >= 1) {
      std::printf("converged on a COLD-BRANCH field with a real crossing.\n");
    } else {
      std::printf("converged with NO crossing in the reported field -- inconsistent, investigate.\n");
    }
    std::printf("\n");
    std::fflush(stdout);
  }

  std::printf("=== Read both windows against the pre-registered readings in this file's header.\n");
  std::printf("    Window 32 is the control: it passes on BOTH libraries, so whichever branch it\n");
  std::printf("    reports tells us what a \"passing\" result on this library actually looks like.\n");
  return probeFailures == 0 ? 0 : 1;
}
