// TD-027 GATE_A3 -- THE POST-FREEZE VERIFICATION of C2-A2.
//
// CLAUDE.md §5.3 step 6: "Rerun fresh. Reuse no earlier result." dry_run_A3.md evaluated C2-A2 on
// TD-040's logs/03 numbers; those are the DRY-RUN and are deliberately not reused. This probe
// re-solves the ladder.
//
// THE FROZEN CRITERION, quoted from GATE_A3.md -- not restated from memory:
//
//   C2-A2   | S_L(dt_default) - S_L(dt_finest) | / S_L(dt_finest)
//           + | last increment |               / S_L(dt_finest)     <=  0.28 %
//           over a dt ladder of AT LEAST THREE HALVINGS at fixed dx.
//
//   The 0.28 % budget is §3.2's LABELLED EMPIRICAL CONSTANT from G8's published band and is
//   UNCHANGED by the amendment. The tail term bounds the remaining error beyond the finest level
//   for any p >= 1, which is weaker than the p = 2.321 the frozen C2 assumed.
//
// WHY THIS FORM AND NOT A RICHARDSON LIMIT. The frozen C2 gated against 4.67589, extrapolated over
// {dt/2, dt/4, dt/8} whose FINE DIFFERENCE WAS ONE LATTICE QUANTUM (TD-043). With n2 = 1.00 the
// order's uncertainty interval is (1.00, INFINITY) because n2 - 1 = 0, so the limit is one
// arbitrary point in an unbounded family. The fixed tree's ladder has increments +1, 0, +1 quanta,
// where a zero difference makes the order undefined outright. No Richardson reference is obtainable
// on either library at settings.tolerance = 1e-3, so the criterion is built to need none.
//
// THE TRIAL CAP SCALES AS 1/dt. At the base dt a solve spends roughly 74 000 steps in trials; at
// dt/8 it needs about 4x that per trial. An unscaled `maximumStepsPerTrial` would TRUNCATE the fine
// levels, corrupt the drift signs the bisection reads, and the ladder would measure the cap instead
// of the physics. Every trial's `settled` flag is checked and an unsettled trial VOIDS the run.
//
// EXPECTED, from the dry-run, stated so a deviation is visible rather than absorbed:
//     S_L  4.955332 / 4.958687 / 4.958687 / 4.962045
//     C2-A2  0.1353 % + 0.0677 % = 0.2030 %  ->  PASS with 1.38x margin
// A disagreement means the ladder is not reproducible, and that is the finding.
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <stdexcept>
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

constexpr Real kBaseDt = 9.4243e-06;    // cell diffusion number 5.0 -- TD-027's shipped cap
constexpr Index kBaseTrialCap = 80000;  // scaled as 1/dt below
constexpr Real kBudget = 0.28;          // §3.2's labelled empirical constant, UNCHANGED

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

  std::printf("TD-027 GATE_A3 -- POST-FREEZE VERIFICATION of C2-A2\n\n");
  std::printf("  500 cells (dx FIXED), base dt %.6e = cell diffusion number 5.0 (the shipped cap)\n",
              kBaseDt);
  std::printf("  the trial cap scales as 1/dt so the trials' physical duration is constant; an\n");
  std::printf("  unscaled cap would truncate the fine levels and measure the cap, not the physics\n\n");
  std::printf("%6s %14s %11s %12s %12s %11s %9s\n", "level", "dt", "trial cap", "S_L cm/s",
              "marchSteps", "fuel bal", "wall s");
  std::fflush(stdout);

  std::vector<Real> speeds;
  bool unsettledAnywhere = false;

  for (int k = 0; k < 4; ++k) {
    const Real factor = std::pow(2.0, static_cast<Real>(k));
    const Real dt = kBaseDt / factor;
    const Index trialCap = static_cast<Index>(static_cast<Real>(kBaseTrialCap) * factor);

    PremixedFlameSettings s;
    s.cells = kCells;
    s.domainLength = kDomain;
    s.anchorFraction = kAnchorFraction;
    s.tolerance = 1.0e-3;
    s.maxOuterIterations = 40;
    s.driftSettleTolerance = 0.05;
    s.driftWindow = 32;
    s.timeStep = dt;
    s.finalMarchRelaxationTimes = 10.0;
    s.maximumStepsPerTrial = trialCap;

    const auto t0 = std::chrono::steady_clock::now();
    PremixedFlameResult f;
    try {
      f = solvePremixedFlame(chemistry, conditions, s);
    } catch (const std::exception& e) {
      std::printf("%6d %14.6e %11lld  THREW: %s\n", k, dt, static_cast<long long>(trialCap),
                  e.what());
      std::fflush(stdout);
      break;
    }
    const Real wall = std::chrono::duration<Real>(std::chrono::steady_clock::now() - t0).count();

    std::size_t unsettled = 0;
    for (const PremixedFlameTrial& t : f.trials) {
      if (t.measured && !t.settled) {
        ++unsettled;
      }
    }
    if (unsettled > 0) {
      unsettledAnywhere = true;
    }

    std::printf("%6d %14.6e %11lld %12.6f %12lld %11.4e %9.0f%s\n", k, dt,
                static_cast<long long>(trialCap), f.flameSpeed * 100.0,
                static_cast<long long>(f.finalMarchSteps), f.fuelBalanceResidual, wall,
                (unsettled > 0) ? "   <- UNSETTLED TRIAL" : "");
    std::fflush(stdout);
    speeds.push_back(f.flameSpeed * 100.0);
  }

  if (unsettledAnywhere) {
    std::printf("\n*** A TRIAL RAN OUT OF BUDGET RATHER THAN SETTLING. The drift signs the\n");
    std::printf("    bisection read are not trustworthy, so S_L from that level is not either.\n");
    std::printf("    NO VERDICT is issued. This is the fail-closed path the header describes.\n");
    return 0;
  }
  if (speeds.size() < 4) {
    std::printf("\n*** fewer than four levels completed -- the ladder is not three halvings deep\n");
    std::printf("    and C2-A2 cannot be evaluated.\n");
    return 0;
  }

  const Real def = speeds[0];
  const Real finest = speeds[3];
  const Real lastInc = speeds[3] - speeds[2];
  const Real measured = std::abs(def - finest) / finest * 100.0;
  const Real tail = std::abs(lastInc) / finest * 100.0;
  const Real total = measured + tail;

  std::printf("\n=== C2-A2, in the frozen form\n\n");
  std::printf("  S_L(default dt)      %.6f cm/s\n", def);
  std::printf("  S_L(dt/8, finest)    %.6f cm/s\n", finest);
  std::printf("  last increment       %+.6e cm/s   (dt/4 -> dt/8)\n", lastInc);
  std::printf("  halvings in ladder   3            (the frozen minimum)\n\n");
  std::printf("  measured term        %.4f %%\n", measured);
  std::printf("  tail term  (p >= 1)  %.4f %%\n", tail);
  std::printf("  TOTAL                %.4f %%   <=  %.2f %%   %s\n", total, kBudget,
              (total <= kBudget) ? "PASS" : "FAIL");
  if (total <= kBudget) {
    std::printf("  margin               %.2fx\n", kBudget / total);
  } else {
    std::printf("  exceeded by          %.2fx\n", total / kBudget);
  }

  std::printf("\n=== REPRODUCTION CONTROL against the dry-run (TD-040 logs/03)\n\n");
  const Real expect[4] = {4.955332, 4.958687, 4.958687, 4.962045};
  bool reproduced = true;
  for (int i = 0; i < 4; ++i) {
    const bool ok = std::abs(speeds[static_cast<std::size_t>(i)] - expect[i]) < 5.0e-6;
    reproduced = reproduced && ok;
    std::printf("  level %d  %.6f vs %.6f   %s\n", i, speeds[static_cast<std::size_t>(i)],
                expect[i], ok ? "MATCH" : "DIFFERS");
  }
  std::printf("\n  %s\n", reproduced
                              ? "The ladder is reproducible; the verdict above stands on a fresh run."
                              : "THE LADDER IS NOT REPRODUCIBLE. That is the finding, not the verdict.");
  return 0;
}
