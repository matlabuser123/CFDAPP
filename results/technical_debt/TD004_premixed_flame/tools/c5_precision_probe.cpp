// TD-004 C5 -- THE THREE FAILING CLAUSES AT FULL PRECISION, pre-fix and post-fix.
//
// WHY THIS PROBE EXISTS. A2_06 ran the gate from C3 and C5 failed three of four clauses:
//
//     T_peak    1799.99897 == 1800.00 exactly      FAIL
//     x_front   3.499219   == 3.50000 mm exactly   FAIL
//     min Y_i   -1.8898e-11 >= 0                   FAIL
//     |sumY-1|  8.8229e-07 <= 1.2e-06              PASS
//
// Before any of those can be called a regression OR a mis-derived criterion, the BASELINE value of
// each is needed -- and it is not on record at usable precision:
//
//   * GATE.md §1 says the dry-run covered "C1 and its controls N2/N3". C5 WAS NEVER DRY-RUN.
//     Under CLAUDE.md §5.2 a clause that was never executed is not "passing", so C5's recorded
//     PASS in verification.md rests on no baseline execution.
//   * the baseline table (logs/04) printed `T_peak` as `1800.00` -- TWO DECIMALS. The post-fix
//     1799.99897 also prints `1800.00` at two decimals. The frozen threshold was transcribed from
//     that table, so an exact-equality test on a double is STRICTER THAN THE DERIVATION BASIS.
//     Whether the baseline satisfies a bitwise reading is unknown and is what this probe measures.
//   * the two baseline records of `min Y_i` CONTRADICT each other: audit.md §2 lists -1.5e-14
//     ("negligible, never repaired") and audit.md §5 lists 0. Neither is at this fixture.
//
// THIS PROBE CHANGES NO THRESHOLD. It measures, so that §5.3's "derive the replacement from first
// principles" has a baseline to derive against instead of a printed rounding.
//
// FIXTURE: identical to A2_06's gate probe -- 500 cells, PRODUCTION dt (cap 5), anchor 0.35,
// relaxT 10, driftWindow 32, maximumStepsPerTrial 80000, NO pinned timeStep. That makes A2_06 a
// REPRODUCTION CONTROL: the post-fix run must return steps 15283, T_peak 1799.99897,
// min Y -1.8898e-11, front 3.499219 mm, |sumY-1| 8.8229e-07, S_L 4.95533. If it does not, the
// probe or the tree is wrong and nothing below it may be read.
//
// ------------------------------------------------------------------ PRE-REGISTERED, one per DIRECTION
//
// T_peak, pre-fix:
//   also 1799.99...  (not bitwise 1800) -> the clause FAILS ON THE BASELINE. It is then a broken
//                    criterion under §5.2, mis-derived from a 2-decimal print, and §5.3 applies:
//                    derive a tolerance from the march's own convergence, do not relax to fit.
//   bitwise 1800.0   -> the fix CHANGED T_peak by ~1.03e-03 K. Small, but real, and it must be
//                    attributed rather than amended away.
//   further from 1800 than post-fix -> the fix IMPROVED the peak; the clause is still mis-derived
//                    but the fix is not implicated by it.
//
// min Y_i, pre-fix:
//   ~ -1.5e-14       -> matches audit.md §2, so the 1260x growth to -1.89e-11 is REAL and belongs
//                    to the fix or to the longer march. It must be explained, and amending the
//                    clause would hide it.
//   ~ -1e-11         -> the audit's -1.5e-14 came from a different fixture; there is NO growth and
//                    the clause was always failing at this fixture.
//   exactly 0        -> the fix introduced the negativity outright.
//
// MY PREDICTION, registered so it can be wrong: pre-fix T_peak is ALSO 1799.99-something, and
// pre-fix min Y is near -1e-11 rather than -1.5e-14 -- i.e. both clauses were already failing and
// the audit's -1.5e-14 belongs to a shorter march at another fixture. I have been wrong about a
// pre-registered value three times in this campaign; this is recorded, not relied upon.
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

  // T_ad, computed the same way the solver's own energy closure does, so "exactly 1800" can be
  // checked against the VALUE THE CODE CAN PRODUCE rather than against the decimal literal 1800.
  const Real tAdiabatic =
      kUnburntT + kFuelFraction * (-kMolarFormationP / kWeight) / kSpecificHeat;

  PremixedFlameSettings s;
  s.cells = kCells;
  s.domainLength = kDomain;
  s.anchorFraction = kAnchorFraction;
  s.tolerance = 1.0e-3;
  s.maxOuterIterations = 40;
  s.driftSettleTolerance = 0.05;
  s.driftWindow = 32;
  s.finalMarchRelaxationTimes = 10.0;
  s.maximumStepsPerTrial = 80000;

  std::printf("TD-004 C5 -- THE FAILING CLAUSES AT FULL PRECISION\n\n");
  std::printf("  fixture: A2_06's, so A2_06 is the reproduction control\n");
  std::printf("  T_ad computed from the case constants: %.15f K\n", tAdiabatic);
  std::printf("  T_ad == 1800.0 bitwise?  %s\n", (tAdiabatic == 1800.0) ? "YES" : "NO");
  std::printf("  T_ad - 1800.0 = %.6e K\n\n", tAdiabatic - 1800.0);
  std::fflush(stdout);

  const auto t0 = std::chrono::steady_clock::now();
  const PremixedFlameResult f = solvePremixedFlame(chemistry, conditions, s);
  const Real wall = std::chrono::duration<Real>(std::chrono::steady_clock::now() - t0).count();

  std::printf("=== MEASURED, full precision\n");
  std::printf("  finalMarchSteps            %lld\n", static_cast<long long>(f.finalMarchSteps));
  std::printf("  peakTemperature            %.15f K\n", f.peakTemperature);
  std::printf("    == 1800.0 bitwise?       %s\n", (f.peakTemperature == 1800.0) ? "YES" : "NO");
  std::printf("    == T_ad   bitwise?       %s\n", (f.peakTemperature == tAdiabatic) ? "YES" : "NO");
  std::printf("    1800.0 - T_peak          %.6e K\n", 1800.0 - f.peakTemperature);
  std::printf("    as printed at 2 dp       %.2f   <- the precision C5 was transcribed from\n",
              f.peakTemperature);
  std::printf("  minMassFraction            %.15e\n", f.minMassFraction);
  std::printf("    >= 0?                    %s\n", (f.minMassFraction >= 0.0) ? "YES" : "NO");
  std::printf("    == 0 bitwise?            %s\n", (f.minMassFraction == 0.0) ? "YES" : "NO");
  std::printf("  flamePosition              %.12f mm\n", f.flamePosition * 1000.0);
  std::printf("  maxCompositionSumDeviation %.15e\n", f.maxCompositionSumDeviation);
  std::printf("  flameSpeed                 %.9f cm/s\n", f.flameSpeed * 100.0);
  std::printf("  consumptionSpeed           %.9f cm/s\n", f.consumptionSpeed * 100.0);
  std::printf("  fuelBalanceResidual        %.9e\n", f.fuelBalanceResidual);
  std::printf("  recentrings                %lld (final march %lld)\n",
              static_cast<long long>(f.recentrings),
              static_cast<long long>(f.finalMarchRecentrings));
  std::printf("  wall                       %.0f s\n\n", wall);

  std::printf("=== REPRODUCTION CONTROL against A2_06 (post-fix tree only)\n");
  std::printf("  steps      15283 vs %lld        %s\n", static_cast<long long>(f.finalMarchSteps),
              (f.finalMarchSteps == 15283) ? "MATCH" : "DIFFERS");
  std::printf("  T_peak   1799.99897 vs %.5f   %s\n", f.peakTemperature,
              (std::abs(f.peakTemperature - 1799.99897) < 5e-6) ? "MATCH" : "DIFFERS");
  std::printf("  min Y   -1.8898e-11 vs %.4e  %s\n", f.minMassFraction,
              (std::abs(f.minMassFraction + 1.8898e-11) < 1e-15) ? "MATCH" : "DIFFERS");
  std::printf("  S_L        4.95533 vs %.5f     %s\n", f.flameSpeed * 100.0,
              (std::abs(f.flameSpeed * 100.0 - 4.95533) < 5e-6) ? "MATCH" : "DIFFERS");
  std::printf("\n  On the PRE-FIX library these are EXPECTED to differ -- that is the point.\n");
  std::printf("  They are the control only for the post-fix run.\n");
  return 0;
}
