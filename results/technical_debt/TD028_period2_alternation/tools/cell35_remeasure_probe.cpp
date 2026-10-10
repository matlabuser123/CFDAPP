// TD-028 -- IS THE PERIOD-2 ALTERNATION GONE? Re-measured on its OWN observable.
//
// WHAT TD-028 RECORDS. The march executes a period-2 alternation, root-caused to TD-032's
// one-step-stale time level. The measured signature, from TD031/logs/05_cell35_history.log at
// 500 cells / cap 5 / relaxT 18, stopping the march at five CONSECUTIVE budgets:
//
//     budget   T[35] K            dT from prev
//      30945    312.99462225717     0.0000e+00
//      30946   1796.61325275801    +1.4836e+03
//      30947    312.99215651739    -1.4836e+03
//      30948   1796.60836183127    +1.4836e+03
//      30949    312.98969432633    -1.4836e+03
//
// Cell 35 is x = 0.70 mm, in the COLD PREHEAT ZONE at about 317 K. It swung the full 1483.6 K rise
// on every single step. That is the observable TD-028 is about, and it is the one re-measured here.
//
// WHY `S_L` IS NOT SUFFICIENT EVIDENCE. TD-027's C4 re-measure found S_L identical to six decimals
// across both step parities, which is encouraging but INDIRECT: S_L comes from the bisection on the
// TRIALS' drift signs, not from the final march's field. TD-028's claim is about the FIELD. A
// solver could in principle report a parity-independent eigenvalue while its final march still
// alternated, so the field must be read directly.
//
// ------------------------------------------------------------------- THE TRAP THIS PROBE AVOIDS
//
// TD-031's original probe hardcodes `budgets[] = {30945, 30946, 30947, 30948, 30949}` and relies on
//
//     finalBudget = min( finalMarchSteps, derived_bound )
//
// to clip the march. Pre-fix the derived bound at relaxT 18 was 30949, so those budgets clipped and
// the five states were genuinely consecutive.
//
// ON THE FIXED TREE THE DERIVED BOUND IS 27511 (TD-027's C4 measured it). So all five original
// budgets now EXCEED it, every run would clip to 27511, and the probe would print FIVE IDENTICAL
// STATES with zero variation -- which reads exactly like "the alternation is gone".
//
// That is a fail-open instrument: it would confirm the hypothesis by failing to vary its own
// independent variable. The budgets below straddle 27511 instead, and the probe ASSERTS that each
// march actually stopped where it was told to. A clipped run is reported as a defect, not averaged
// into a verdict.
//
// TD-031's original probe is NOT modified -- it stays at its recorded hash 620013f0c92ff086.
//
// PRE-REGISTERED READINGS, one per direction:
//
//   T[35] varies by << 1 K across the five consecutive budgets
//        -> the period-2 alternation is GONE on its own observable. TD-028 is resolved by TD-032,
//           and the field may be trusted for quantities read from it.
//   T[35] still swings by ~1483 K, or any amount comparable to the full rise
//        -> the alternation SURVIVES TD-032's fix. That would mean TD-032's root-cause attribution
//           is wrong or incomplete, which is a serious finding and reopens TD-032, not just TD-028.
//   T[35] varies by an intermediate amount (say 1-100 K)
//        -> something still oscillates, at reduced amplitude. TD-028 is NOT resolved and needs its
//           own diagnosis rather than closure.
//   any run reports marchStp != budget
//        -> the clipping failed and NO verdict may be read from this run at all.
//
// MY PREDICTION, registered so it can be wrong: T[35] varies smoothly and by much less than 1 K,
// because the transient path that created the two interleaved sub-sequences no longer exists. I
// have been wrong on a pre-registered value three times in this campaign.
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
constexpr Index kWatch = 35;       // the cell TD-031 logs/04 localised the saturated change to
constexpr Real kFullRise = 1500.0; // T_ad - T_unburnt, the scale the swing is judged against

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

  std::printf("TD-028 -- CELL 35 ACROSS CONSECUTIVE MARCH STEPS, on the TD-032-FIXED tree\n\n");
  std::printf("  500 cells / cap 5 / relaxT 18 -- the same configuration TD-031 logs/05 used.\n");
  std::printf("  cell %lld is x = %.2f mm, in the cold preheat zone.\n",
              static_cast<long long>(kWatch), static_cast<Real>(kWatch) * dx * 1000.0);
  std::printf("  PRE-FIX this cell swung 1483.6 K EVERY STEP between 312.99 and 1796.61 K.\n\n");
  std::printf("  Budgets straddle the FIXED tree's derived bound of 27511, so each clips. The\n");
  std::printf("  original probe's 30945-30949 would all exceed it and give five IDENTICAL states.\n\n");
  std::printf("%10s %10s %20s %16s %14s %9s\n", "budget", "marchStp", "T[35] K", "dT from prev",
              "worstChange", "wall s");
  std::fflush(stdout);

  // All <= 27511, so min(finalMarchSteps, 27511) == finalMarchSteps and each march clips.
  const Index budgets[] = {27507, 27508, 27509, 27510, 27511};
  std::vector<Real> watched;
  std::vector<std::vector<Real>> fields;
  bool clipFailed = false;

  for (const Index b : budgets) {
    PremixedFlameSettings s;
    s.cells = kCells;
    s.domainLength = kDomain;
    s.anchorFraction = kAnchorFraction;
    s.tolerance = 1.0e-3;
    s.maxOuterIterations = 40;
    s.driftSettleTolerance = 0.05;
    s.driftWindow = 32;
    s.finalMarchRelaxationTimes = 18.0;
    s.maximumStepsPerTrial = 80000;
    s.finalMarchSteps = b;  // the hard ceiling -- this is what stops the march at step b

    const auto t0 = std::chrono::steady_clock::now();
    PremixedFlameResult f;
    try {
      f = solvePremixedFlame(chemistry, conditions, s);
    } catch (const std::exception& e) {
      std::printf("%10lld  THREW: %s\n", static_cast<long long>(b), e.what());
      std::fflush(stdout);
      continue;
    }
    const Real wall = std::chrono::duration<Real>(std::chrono::steady_clock::now() - t0).count();
    const Real t35 = f.temperature[static_cast<std::size_t>(kWatch)];
    const Real prev = watched.empty() ? t35 : watched.back();
    const long long got = static_cast<long long>(f.finalMarchSteps);

    std::printf("%10lld %10lld %20.11f %16.4e %14.4e %9.0f%s\n", static_cast<long long>(b), got,
                t35, t35 - prev, f.worstStateChange, wall,
                (got == static_cast<long long>(b)) ? "" : "   <- CLIP FAILED");
    std::fflush(stdout);
    if (got != static_cast<long long>(b)) {
      clipFailed = true;
    }
    watched.push_back(t35);
    std::vector<Real> row;
    for (Index c = 25; c <= 45; ++c) {
      row.push_back(f.temperature[static_cast<std::size_t>(c)]);
    }
    fields.push_back(row);
  }

  if (clipFailed) {
    std::printf("\n*** AT LEAST ONE MARCH DID NOT STOP WHERE IT WAS TOLD.\n");
    std::printf("    The budgets are not consecutive states and NO verdict may be read from this\n");
    std::printf("    run. This is the fail-open mode the header describes.\n");
    return 0;
  }
  if (watched.size() < 2) {
    std::printf("\n*** fewer than two states -- nothing to compare\n");
    return 0;
  }

  const Real mx = *std::max_element(watched.begin(), watched.end());
  const Real mn = *std::min_element(watched.begin(), watched.end());
  const Real swing = mx - mn;

  std::printf("\n=== THE NEIGHBOURHOOD, cells 25 to 45, max spread across the five budgets\n\n");
  Real worstCell = 0.0;
  Index worstIdx = 25;
  for (std::size_t j = 0; j < fields[0].size(); ++j) {
    Real cmx = fields[0][j];
    Real cmn = fields[0][j];
    for (const auto& row : fields) {
      cmx = std::max(cmx, row[j]);
      cmn = std::min(cmn, row[j]);
    }
    const Real sp = cmx - cmn;
    if (sp > worstCell) {
      worstCell = sp;
      worstIdx = 25 + static_cast<Index>(j);
    }
  }
  std::printf("  worst cell in 25..45 is cell %lld, spread %.6e K\n",
              static_cast<long long>(worstIdx), worstCell);

  std::printf("\n=== THE VERDICT ON TD-028's OWN OBSERVABLE\n\n");
  std::printf("  T[35] max            %.11f K\n", mx);
  std::printf("  T[35] min            %.11f K\n", mn);
  std::printf("  T[35] swing          %.6e K\n", swing);
  std::printf("  as %% of the full rise %.6e %%\n", swing / kFullRise * 100.0);
  std::printf("  PRE-FIX swing        1.4836e+03 K  (98.9 %% of the full rise)\n\n");

  if (swing < 1.0) {
    std::printf("  -> THE PERIOD-2 ALTERNATION IS GONE on its own observable. The swing fell from\n");
    std::printf("     1483.6 K to %.4e K, a factor of %.3e. TD-028 is resolved by TD-032.\n", swing,
                1483.6 / std::max(swing, 1e-30));
  } else if (swing > 100.0) {
    std::printf("  -> THE ALTERNATION SURVIVES. %.4e K is comparable to the full rise, so TD-032's\n", swing);
    std::printf("     root-cause attribution is wrong or incomplete. This reopens TD-032.\n");
  } else {
    std::printf("  -> INTERMEDIATE: %.4e K. Something still oscillates at reduced amplitude.\n", swing);
    std::printf("     TD-028 is NOT resolved and needs its own diagnosis, not closure.\n");
  }
  return 0;
}
