// TD-004 C2-A1 -- the FRESH verification run for the amended criterion.
//
// No production file is changed by this probe. It is the amendment's verification measurement,
// required by CLAUDE.md 5.3: "Rerun fresh. Reuse no earlier result."
//
// GATE_A1.md section 5 established NON-VACUITY from TD-005's logs/14, which is what a dry-run is
// for. The VERDICT must come from a fresh measurement, and this is it.
//
// C2-A1, over relaxT {10, 14, 18, 22, 26, 30} at GATE.md's fixture:
//   (a) REACHES   at least one level has fuelBalanceResidual < 0.05
//   (b) STAYS     every level at or beyond that first admissible level is also admissible
//   (c) SETTLES   over those levels, (max - min) <= mean        [no fitted constant]
//
// PREDICTED IN GATE_A1 SECTION 7, BEFORE THIS RUN: FAIL at (b), because the march executes a limit
// cycle of period 14 relaxation times. And a guard: if the phase pattern differs from logs/14's
// GGBGGB, the determinism this amendment rests on is in question, and the probe STOPS rather than
// evaluating a criterion against a non-reproducible signal.
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
// the tree's own admissibility bound, test_premixed_flame.cpp:447 -- neither weakened nor tightened
constexpr Real kAdmissible = 0.05;

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

  std::printf("TD-004 C2-A1 -- FRESH VERIFICATION\n\n");
  std::printf("  GATE.md fixture: 500 cells / 10 mm / anchor 0.35 / tolerance 1e-3, DEFAULT dt\n");
  std::printf("  admissibility bound %.2f, from test_premixed_flame.cpp:447\n", kAdmissible);
  std::printf("  PREDICTED in GATE_A1 section 7: FAIL at (b). logs/14 pattern was GGBGGB.\n\n");
  std::printf("%8s %10s %12s %9s %11s %9s %8s\n", "relaxT", "marchStp", "fuel bal", "admiss",
              "S_L cm/s", "delta mm", "wall s");
  std::fflush(stdout);

  const std::vector<Real> ladder = {10.0, 14.0, 18.0, 22.0, 26.0, 30.0};
  std::vector<Real> balances;
  std::string pattern;

  for (const Real t : ladder) {
    PremixedFlameSettings s;  // GATE.md's fixture, verbatim
    s.cells = 500;
    s.domainLength = 0.010;
    s.anchorFraction = 0.35;
    s.tolerance = 1.0e-3;
    s.maxOuterIterations = 40;
    s.driftSettleTolerance = 0.05;
    s.maximumStepsPerTrial = 20000;
    s.finalMarchRelaxationTimes = t;  // the family's independent variable

    const auto t0 = std::chrono::steady_clock::now();
    const PremixedFlameResult f = solvePremixedFlame(chemistry, conditions, s);
    const Real wall = std::chrono::duration<Real>(std::chrono::steady_clock::now() - t0).count();
    const bool ok = f.fuelBalanceResidual < kAdmissible;
    pattern += (ok ? 'G' : 'B');
    balances.push_back(f.fuelBalanceResidual);
    std::printf("%8.0f %10lld %12.3e %9s %11.5f %9.4f %8.0f\n", t,
                static_cast<long long>(f.finalMarchSteps), f.fuelBalanceResidual,
                ok ? "yes" : "NO", f.flameSpeed * 100.0, f.flameThickness * 1000.0, wall);
    std::fflush(stdout);
  }

  std::printf("\n  pattern %s      logs/14 measured GGBGGB\n", pattern.c_str());
  if (pattern != "GGBGGB") {
    std::printf("  *** PATTERN DIFFERS FROM logs/14. The determinism this amendment rests on is\n");
    std::printf("      in question. STOPPING: a criterion must not be evaluated against a\n");
    std::printf("      non-reproducible signal.\n");
    return 1;
  }
  std::printf("  determinism confirmed: the signal reproduces logs/14 exactly.\n");

  // ---------------------------------------------------------------- C2-A1
  std::printf("\n=== C2-A1\n");
  std::size_t first = balances.size();
  for (std::size_t i = 0; i < balances.size(); ++i) {
    if (balances[i] < kAdmissible) {
      first = i;
      break;
    }
  }
  const bool a = (first < balances.size());
  if (a) {
    std::printf("  (a) REACHES  first admissible level: relaxT %.0f (%.3e)   -> PASS\n",
                ladder[first], balances[first]);
  } else {
    std::printf("  (a) REACHES  NO level is admissible                       -> FAIL\n");
    std::printf("\n  C2-A1 VERDICT: FAIL at (a).\n");
    return 0;
  }

  bool b = true;
  for (std::size_t i = first; i < balances.size(); ++i) {
    if (balances[i] >= kAdmissible) {
      b = false;
      std::printf("  (b) STAYS    relaxT %.0f LEAVES: %.3e >= %.2f\n", ladder[i], balances[i],
                  kAdmissible);
    }
  }
  std::printf("  (b) STAYS                                                 -> %s\n",
              b ? "PASS" : "FAIL");

  Real lo = balances[first];
  Real hi = balances[first];
  Real sum = 0.0;
  std::size_t n = 0;
  for (std::size_t i = first; i < balances.size(); ++i) {
    lo = std::min(lo, balances[i]);
    hi = std::max(hi, balances[i]);
    sum += balances[i];
    ++n;
  }
  const Real mean = sum / static_cast<Real>(n);
  const bool c = ((hi - lo) <= mean);
  std::printf("  (c) SETTLES  max %.3e  min %.3e  mean %.3e\n", hi, lo, mean);
  std::printf("               max-min %.3e %s mean %.3e                 -> %s\n", hi - lo,
              c ? "<=" : ">", mean, c ? "PASS" : "FAIL");

  std::printf("\n  C2-A1 VERDICT: %s\n", (a && b && c) ? "PASS" : "FAIL");
  if (!(a && b && c)) {
    std::printf("\n  As predicted in GATE_A1 section 7. The criterion fails because TD-028 (the\n");
    std::printf("  limit cycle) and TD-030's candidate (the 8.48e-02 reaction-integral residual)\n");
    std::printf("  are both OPEN. A criterion that fails on two known open defects is a WORKING\n");
    std::printf("  criterion -- which is exactly the distinction the original C2 failed, since no\n");
    std::printf("  fix could have satisfied a bound on a trend that does not exist.\n");
  }
  return 0;
}
