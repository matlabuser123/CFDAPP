// TD-040 §5b -- WHICH EIGENVALUE IS CORRECT? The dt ladder, re-measured on the FIXED tree.
//
// THE QUESTION, quoted from audit.md §5b:
//   "WHICH EIGENVALUE IS CORRECT? Neither 4.671937 nor 4.955332 is verified. TD-027's Richardson
//    limit was extrapolated on the blind-locator solver and may have inherited the defect. The dt
//    ladder re-measured on the fixed tree would give a limit that has not."
//
// This single measurement bears on three open items:
//   TD-040  its own §5b, the eigenvalue question
//   TD-027  C2 gates against the Richardson limit 4.67589, extrapolated PRE-fix. The measured
//           eigenvalue is now +5.98 % from it (verification_A2.md §4).
//   TD-004  its registered stop is "S_L moved +6.07 %", and whether that move was toward or away
//           from the truth has never been established.
//
// THE DESIGN, and the two traps it avoids.
//
//   1. THE TRIAL BUDGET MUST SCALE WITH dt. `maximumStepsPerTrial` is a HARD cap. At the base dt a
//      solve spends ~74 000 steps in trials; at dt/8 it would need ~590 000 and be silently
//      truncated, giving wrong drift signs and therefore a wrong eigenvalue -- the ladder would
//      measure the cap, not the physics. So the cap scales as 1/dt, holding the trials' PHYSICAL
//      duration fixed. This is the knob-that-actually-controls-the-error lesson applied forward.
//
//   2. FOUR LEVELS, NOT THREE. Three levels give ONE order estimate, and CLAUDE.md §6 admits an
//      observed order only if the family is demonstrably in the asymptotic range -- which a single
//      estimate cannot show. Four levels give TWO, and their agreement IS the asymptotic evidence.
//      An under-supported extrapolation is exactly how TD-027's 4.67589 limit reached the record.
//
//   The ladder does NOT go coarser than the base dt: 2*dt would be cell diffusion number 10,
//   above the cap of 5.0 that TD-027 shipped. Measuring a configuration the project rejected would
//   not inform anything.
//
// PRE-REGISTERED READINGS, one per direction:
//
//   limit within ~0.5 % of 4.955332
//        -> the POST-FIX eigenvalue is confirmed and TD-027's 4.67589 limit inherited the
//           blind-locator defect. TD-004's +6.07 % move was TOWARD the truth.
//   limit near 4.671937
//        -> the post-fix eigenvalue is WRONG and TD-004's locator fix introduced an error. That
//           would be a serious finding and would reopen TD-004's fix, not just its gate.
//   limit near NEITHER
//        -> both recorded values are off and a third mechanism is at work. Stop and characterise.
//   the two order estimates DISAGREE
//        -> the family is not in the asymptotic range and NO limit may be claimed from it,
//           whatever the numbers look like. Reporting a Richardson value anyway would repeat the
//           error this run exists to correct.
//
// MY PREDICTION, registered so it can be wrong: the limit lands near 4.96, confirming the post-fix
// eigenvalue, because TD-029 measured the per-halving dt sensitivity at only +0.068 % -- a
// geometric tail of that size cannot reach 4.67, which is 6 % away. I have been wrong on a
// pre-registered value three times in this campaign and right twice.
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

constexpr Real kBaseDt = 9.4243e-06;        // cell diffusion number 5.0, TD-027's shipped cap
constexpr Index kBaseTrialCap = 80000;      // scaled as 1/dt below

// the two values in contention, and the limit whose provenance is in question
constexpr Real kPostFix = 4.955332;
constexpr Real kPreFix = 4.671937;
constexpr Real kTd027Limit = 4.67589;

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

  std::printf("TD-040 §5b -- THE dt LADDER ON THE FIXED TREE. Which eigenvalue is correct?\n\n");
  std::printf("  in contention:  post-fix %.6f   pre-fix %.6f\n", kPostFix, kPreFix);
  std::printf("  TD-027 C2 gates against a Richardson limit of %.5f, extrapolated PRE-fix.\n\n",
              kTd027Limit);
  std::printf("  500 cells (dx FIXED). The trial cap scales as 1/dt so the trials' physical\n");
  std::printf("  duration is held constant -- an unscaled cap would truncate the fine levels and\n");
  std::printf("  the ladder would measure the cap rather than the physics.\n\n");
  std::printf("%6s %14s %11s %12s %12s %12s %10s\n", "level", "dt", "trial cap", "S_L cm/s",
              "marchSteps", "fuel bal", "wall s");
  std::fflush(stdout);

  std::vector<Real> speeds;
  std::vector<Real> dts;

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
      std::printf("%6d %14.6e %11lld  THREW: %s\n", k, dt,
                  static_cast<long long>(trialCap), e.what());
      std::fflush(stdout);
      break;
    }
    const Real wall = std::chrono::duration<Real>(std::chrono::steady_clock::now() - t0).count();

    std::printf("%6d %14.6e %11lld %12.6f %12lld %12.4e %10.0f\n", k, dt,
                static_cast<long long>(trialCap), f.flameSpeed * 100.0,
                static_cast<long long>(f.finalMarchSteps), f.fuelBalanceResidual, wall);
    std::fflush(stdout);

    speeds.push_back(f.flameSpeed * 100.0);
    dts.push_back(dt);
  }

  std::printf("\n=== SUCCESSIVE DIFFERENCES AND OBSERVED ORDER\n\n");
  if (speeds.size() < 3) {
    std::printf("  fewer than 3 levels completed -- no order and no limit can be formed.\n");
    return 0;
  }
  std::vector<Real> orders;
  std::printf("%22s %16s %16s %10s\n", "triplet", "coarse diff", "fine diff", "order p");
  for (std::size_t i = 0; i + 2 < speeds.size(); ++i) {
    const Real d1 = speeds[i + 1] - speeds[i];
    const Real d2 = speeds[i + 2] - speeds[i + 1];
    const bool sameSign = (d1 > 0.0) == (d2 > 0.0);
    const Real p = (sameSign && d2 != 0.0) ? std::log(std::abs(d1 / d2)) / std::log(2.0) : 0.0;
    char label[64];
    std::snprintf(label, sizeof(label), "{%d, %d, %d}", static_cast<int>(i),
                  static_cast<int>(i + 1), static_cast<int>(i + 2));
    std::printf("%22s %+16.6e %+16.6e %10.3f%s\n", label, d1, d2, p,
                sameSign ? "" : "   <- SIGN CHANGE: order undefined");
    if (sameSign) {
      orders.push_back(p);
    }
  }

  std::printf("\n=== IS THE FAMILY ASYMPTOTIC? This gates whether a limit may be claimed at all.\n\n");
  if (orders.size() < 2) {
    std::printf("  fewer than TWO order estimates -- the asymptotic range is NOT demonstrated.\n");
    std::printf("  CLAUDE.md §6 admits an observed order only on a demonstrably asymptotic family,\n");
    std::printf("  so NO Richardson limit is claimed below, whatever the numbers suggest.\n");
    return 0;
  }
  const Real spread = std::abs(orders[0] - orders[1]);
  std::printf("  order estimates: %.3f and %.3f, spread %.3f\n", orders[0], orders[1], spread);
  const bool asymptotic = (spread <= 0.5);
  std::printf("  -> %s\n", asymptotic ? "CONSISTENT: the family supports an extrapolation."
                                      : "INCONSISTENT: NOT asymptotic. No limit may be claimed.");

  if (!asymptotic) {
    std::printf("\n  Reporting a Richardson value from a non-asymptotic family is precisely how\n");
    std::printf("  TD-027's 4.67589 reached the record. It is not done here.\n");
    return 0;
  }

  const Real p = orders.back();
  const std::size_t n = speeds.size();
  const Real fine = speeds[n - 1];
  const Real coarse = speeds[n - 2];
  const Real limit = fine + (fine - coarse) / (std::pow(2.0, p) - 1.0);
  const Real gci = 1.25 * std::abs((fine - coarse) / fine) / (std::pow(2.0, p) - 1.0) * 100.0;

  std::printf("\n=== THE RICHARDSON LIMIT\n\n");
  std::printf("  finest S_L        %.6f cm/s   at dt = %.6e\n", fine, dts[n - 1]);
  std::printf("  observed order p  %.3f\n", p);
  std::printf("  dt -> 0 LIMIT     %.6f cm/s\n", limit);
  std::printf("  GCI               %.4f %%\n\n", gci);

  std::printf("=== THE VERDICT ON WHICH EIGENVALUE IS CORRECT\n\n");
  const Real dPost = std::abs(limit - kPostFix) / kPostFix * 100.0;
  const Real dPre = std::abs(limit - kPreFix) / kPreFix * 100.0;
  const Real d027 = std::abs(limit - kTd027Limit) / kTd027Limit * 100.0;
  std::printf("  limit vs POST-FIX %.6f   %+.3f %%\n", kPostFix, (limit - kPostFix) / kPostFix * 100.0);
  std::printf("  limit vs PRE-FIX  %.6f   %+.3f %%\n", kPreFix, (limit - kPreFix) / kPreFix * 100.0);
  std::printf("  limit vs TD-027's %.5f    %+.3f %%\n\n", kTd027Limit,
              (limit - kTd027Limit) / kTd027Limit * 100.0);

  if (dPost < 0.5 && dPre > 2.0) {
    std::printf("  -> THE POST-FIX EIGENVALUE IS CONFIRMED. TD-027's limit inherited the\n");
    std::printf("     blind-locator defect, and TD-004's +6.07 %% move was TOWARD the truth.\n");
  } else if (dPre < 0.5 && dPost > 2.0) {
    std::printf("  -> THE PRE-FIX EIGENVALUE IS CONFIRMED. The locator fix moved S_L AWAY from\n");
    std::printf("     the limit, which reopens the FIX itself and not merely TD-004's gate.\n");
  } else {
    std::printf("  -> NEITHER recorded value matches the limit (%.3f %% and %.3f %% away).\n", dPost,
                dPre);
    std::printf("     A third mechanism is at work. Stop and characterise it.\n");
  }
  std::printf("\n  d027 = %.3f %% -- how far TD-027's C2 reference sits from this limit.\n", d027);
  return 0;
}
