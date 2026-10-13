// TD-027 C4 RE-MEASURE -- on the TD-032-fixed march, with step parity REPORTED not assumed.
//
// WHY C4 WAS INVALIDATED (regression.md, recorded 2026-10-06). C4's four levels relaxT 18/22/26/30
// had march budgets 30949, 37827, 44705 and 51583. **All four are odd**, because consecutive levels
// differ by 6878 -- an even number -- so the ladder preserved step parity BY CONSTRUCTION. With
// TD-032's stale time level making the even and odd steps two interleaved sub-sequences, the
// recorded `max-min 8.07e-04 <= mean 8.48e-02, by 105x` was four samples of ONE branch of the
// alternation. It measured the reproducibility of a parity, not the convergence of a march.
//
// WHAT THIS PROBE DOES DIFFERENTLY. It reports, per level, the budget's PARITY alongside the fuel
// balance -- so whether the family crosses parity is a MEASUREMENT rather than an assumption. The
// previous ladder's defect was invisible precisely because nobody printed the parity.
//
// THE CRITERION, quoted from GATE.md's C4 row, and NOT restated from memory:
//   "the limit cycle suppressed" -- the relaxT family's fuel balance must satisfy
//   max-min <= mean. At cap = 20 it was 6.751e-01 / 1.045e-02 / 1.093e-02 / 6.022e-01, giving
//   max-min = 6.647e-01 > mean 2.208e-01, a FAIL. At cap = 5 it passed.
//
// THE FIXTURE IS STATED, because it is NOT the one C4 was originally measured on. TD-032's fix
// changed every field-derived quantity, so the pre-fix numbers (mean ~8.5e-02) are not reproducible
// by construction and are not a target. What carries over is the criterion's FORM -- max-min <= mean
// -- evaluated on the fixed march at the fixture every other measurement this session used:
// 500 cells, anchorFraction 0.35, driftWindow 32, maximumStepsPerTrial 80000, production dt.
//
// PRE-REGISTERED READINGS, one per direction, no prediction between them:
//
//   parities MIXED and max-min <= mean
//        -> C4 is validly re-measured and PASSES. The ladder now crosses parity, so the statistic
//           is no longer one branch of an alternation, and the limit cycle really is suppressed.
//   parities MIXED and max-min > mean
//        -> C4 FAILS on a valid ladder. Something still varies with march length, and TD-032's fix
//           did not suppress it. That would be a new finding and a stop.
//   parities ALL THE SAME
//        -> the ladder is STILL parity-preserving and this probe inherits the exact defect it was
//           written to remove. C4 is then NOT re-measured, whatever the statistic says, and a
//           budget-ceiling pair (N, N+1 -- guaranteed opposite parity) is required instead.
//           Reporting a PASS in that case would repeat the original error.
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

  std::printf("TD-027 C4 RE-MEASURE -- the relaxT family on the TD-032-FIXED march\n\n");
  std::printf("  ORIGINAL ladder, INVALID: relaxT 18/22/26/30 -> budgets 30949/37827/44705/51583,\n");
  std::printf("  ALL ODD (consecutive levels differ by 6878, an even number). Parity preserved by\n");
  std::printf("  construction, so the statistic sampled one branch of TD-032's alternation.\n\n");
  std::printf("  CRITERION (GATE.md C4): max-min <= mean, over the family's fuel balance.\n\n");
  std::printf("%8s %12s %8s %14s %12s %10s %9s\n", "relaxT", "budget", "parity", "fuel balance",
              "S_L cm/s", "front mm", "wall s");
  std::fflush(stdout);

  std::vector<Real> balances;
  std::vector<long long> budgets;
  std::vector<int> parities;

  for (const Real relaxT : {18.0, 22.0, 26.0, 30.0}) {
    PremixedFlameSettings s;
    s.cells = kCells;
    s.domainLength = kDomain;
    s.anchorFraction = kAnchorFraction;
    s.tolerance = 1.0e-3;
    s.maxOuterIterations = 40;
    s.driftSettleTolerance = 0.05;
    s.driftWindow = 32;
    s.finalMarchRelaxationTimes = relaxT;
    s.maximumStepsPerTrial = 80000;

    const auto t0 = std::chrono::steady_clock::now();
    PremixedFlameResult f;
    try {
      f = solvePremixedFlame(chemistry, conditions, s);
    } catch (const std::exception& e) {
      std::printf("%8.0f  THREW: %s\n", relaxT, e.what());
      std::fflush(stdout);
      continue;
    }
    const Real wall = std::chrono::duration<Real>(std::chrono::steady_clock::now() - t0).count();
    const long long budget = static_cast<long long>(f.finalMarchSteps);
    const int parity = static_cast<int>(budget % 2);

    std::printf("%8.0f %12lld %8s %14.6e %12.6f %10.6f %9.0f\n", relaxT, budget,
                (parity == 0) ? "EVEN" : "ODD", f.fuelBalanceResidual, f.flameSpeed * 100.0,
                f.flamePosition * 1000.0, wall);
    std::fflush(stdout);

    balances.push_back(f.fuelBalanceResidual);
    budgets.push_back(budget);
    parities.push_back(parity);
  }

  std::printf("\n=== IS THE LADDER PARITY-CROSSING? This decides whether C4 is re-measured at all.\n\n");
  int evens = 0;
  int odds = 0;
  for (const int p : parities) {
    (p == 0) ? ++evens : ++odds;
  }
  std::printf("  EVEN budgets %d    ODD budgets %d\n", evens, odds);
  const bool crossing = (evens > 0 && odds > 0);
  std::printf("  -> %s\n\n", crossing ? "PARITY-CROSSING: the family samples BOTH branches."
                                      : "STILL PARITY-PRESERVING: this probe inherits the defect.");

  if (balances.size() < 2) {
    std::printf("  fewer than 2 levels completed -- no statistic can be formed\n");
    return 0;
  }
  const Real mx = *std::max_element(balances.begin(), balances.end());
  const Real mn = *std::min_element(balances.begin(), balances.end());
  Real sum = 0.0;
  for (const Real b : balances) {
    sum += b;
  }
  const Real mean = sum / static_cast<Real>(balances.size());
  const Real spread = mx - mn;

  std::printf("=== C4's STATISTIC\n\n");
  std::printf("  max       %.6e\n", mx);
  std::printf("  min       %.6e\n", mn);
  std::printf("  max-min   %.6e\n", spread);
  std::printf("  mean      %.6e\n", mean);
  std::printf("  ratio     %.4f   (max-min / mean; the criterion is <= 1)\n", spread / mean);
  std::printf("\n  C4  max-min <= mean   %s\n", (spread <= mean) ? "PASS" : "FAIL");

  if (!crossing) {
    std::printf("\n  *** THE VERDICT ABOVE IS NOT ADMISSIBLE AS A C4 RE-MEASURE.\n");
    std::printf("      The ladder did not cross parity, so the statistic is again one branch --\n");
    std::printf("      exactly the defect that invalidated the original. A budget-ceiling pair\n");
    std::printf("      (finalMarchSteps = N and N+1, guaranteed opposite parity) is required.\n");
    std::printf("      Reporting this as a PASS would repeat the original error.\n");
  }
  return 0;
}
