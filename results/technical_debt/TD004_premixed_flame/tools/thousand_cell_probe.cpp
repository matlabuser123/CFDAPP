// TD-004 -- DOES THE 1000-CELL LEVEL FIT ctest's 7200 s CAP? And would the trial cap truncate it?
//
// WHY THIS MATTERS MORE THAN IT LOOKS. C6a exists only because the EXPENSIVE refinement triplet
// {250, 500, 1000} was ruled out, on this evidence (logs/07):
//
//     [ RUN      ] G8PremixedFlameTest.TheFlameSpeedConvergesUnderMeshRefinement
//     === exit 0 (124 = still unfinished at 1750s) ===
//
// The test STARTED and produced NO result line, so it genuinely did not finish. But two things
// about that record are weak:
//
//   1. THE EXIT CODE IS NOT EVIDENCE. It says `exit 0` while the note says 124 would mean timeout.
//      A `timeout ... | tee` pipeline reports the exit of the LAST stage, so the 0 is tee's. The
//      harness below captures the exit code BEFORE any pipe, which is the defect it is correcting.
//   2. 1750 s IS NOT THE CAP. The repository's per-test cap is `ctest --timeout 7200`. The
//      1000-cell level was ruled out at 1750 s and NEVER run against 7200 s.
//
// If it fits the real cap, C6a's premise dissolves: the fixture never needed shrinking, the
// [1.5, 2.5] band would not be borrowed from a different family, and the record's p = 1.93 would
// apply directly to the triplet it was measured on.
//
// ------------------------------------------------------------------ THE SECOND TRAP, AND THE GUARD
//
// `fixture_probe.cpp` runs `maximumStepsPerTrial = 20000`. At 500 cells the trials settle in roughly
// 4 600-17 400 steps, so the cap does not bind. At 1000 cells `dx` halves, `dt ~ dx^2` QUARTERS, and
// the trials need about 4x as many steps -- so 20000 would TRUNCATE them, the drift signs would be
// wrong, and the bisection would converge on a wrong eigenvalue. That is the same failure mode the
// TD-040 dt ladder had to scale around.
//
// So this runs with the cap scaled 4x to 80000, AND reports every trial's `settled` flag plus the
// maximum steps any trial used. `PremixedFlameTrial::settled` is documented as "the drift settled,
// rather than the step budget running out", so an unsettled trial is a direct, fail-closed signal
// that the cap bound. If the observed maximum exceeds 20000, that PROVES fixture_probe's cap would
// have truncated this level -- which would mean the original ruling-out was measuring the cap and
// not the cost.
//
// PRE-REGISTERED READINGS:
//
//   completes in <= 7200 s, all trials settled
//        -> the expensive triplet is AFFORDABLE. C6a's premise dissolves and the right move is to
//           restore {250, 500, 1000}, not to amend C6a's band.
//   completes in > 7200 s (or is killed by the harness's own timeout)
//        -> the level genuinely does not fit the cap. C6a's existence is justified and the
//           amendment question in amendment_A6_proposal.md §4 stands as posed.
//   completes, but some trial did NOT settle even at cap 80000
//        -> the cap still binds at 1000 cells and the eigenvalue from this level is NOT
//           trustworthy, whatever the wall time says. Report and stop; do not read S_L from it.
//   max trial steps > 20000
//        -> fixture_probe's cap WOULD have truncated this level. Recorded either way, because it
//           bears on whether logs/07 measured cost or measured the cap.
//
// NO PREDICTION is registered on the wall time. The TD-035 cost ladder's own interval exponents
// ranged 0.79 to 4.07, so extrapolating 500 -> 1000 cells spans roughly 4 900 s to 8 600 s -- which
// straddles the cap. That is precisely why it is being measured rather than argued.
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
constexpr Index kCells = 1000;
constexpr Index kTrialCap = 80000;          // 4x fixture_probe's 20000, for the 4x smaller dt
constexpr Index kFixtureProbeCap = 20000;   // what fixture_probe uses, for the comparison
constexpr Real kCap = 7200.0;               // ctest --timeout, per test

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

  std::printf("TD-004 -- THE 1000-CELL LEVEL. Does it fit ctest's %.0f s cap?\n\n", kCap);
  std::printf("  logs/07 ruled this level out at 1750 s, not at the 7200 s cap, and its recorded\n");
  std::printf("  'exit 0' was a pipeline's exit rather than the timeout's. Both are corrected here.\n\n");
  std::printf("  cells %lld, dx %.3e m, trial cap %lld (4x fixture_probe's %lld, for the 4x\n",
              static_cast<long long>(kCells), dx, static_cast<long long>(kTrialCap),
              static_cast<long long>(kFixtureProbeCap));
  std::printf("  smaller dt -- an unscaled cap would truncate the trials and corrupt S_L).\n\n");
  std::fflush(stdout);

  PremixedFlameSettings s;
  s.cells = kCells;
  s.domainLength = kDomain;
  s.anchorFraction = kAnchorFraction;
  s.tolerance = 1.0e-3;
  s.maxOuterIterations = 40;
  s.driftSettleTolerance = 0.05;
  s.maximumStepsPerTrial = kTrialCap;

  const auto t0 = std::chrono::steady_clock::now();
  PremixedFlameResult f;
  try {
    f = solvePremixedFlame(chemistry, conditions, s);
  } catch (const std::exception& e) {
    const Real w = std::chrono::duration<Real>(std::chrono::steady_clock::now() - t0).count();
    std::printf("THREW after %.0f s: %s\n", w, e.what());
    return 0;
  }
  const Real wall = std::chrono::duration<Real>(std::chrono::steady_clock::now() - t0).count();

  const Real resCells = f.flameThickness / dx;
  std::printf("=== MEASURED\n");
  std::printf("  wall                 %.1f s        cap %.0f s\n", wall, kCap);
  std::printf("  S_L                  %.6f cm/s\n", f.flameSpeed * 100.0);
  std::printf("  flameThickness       %.6f mm   = %.2f cells  (the test's guard is > 4)\n",
              f.flameThickness * 1000.0, resCells);
  std::printf("  fuelBalanceResidual  %.6e\n", f.fuelBalanceResidual);
  std::printf("  finalMarchSteps      %lld\n", static_cast<long long>(f.finalMarchSteps));
  std::printf("  timeStep             %.6e s\n", f.timeStep);
  std::printf("  bracketSteps         %lld   outerIterations %lld\n",
              static_cast<long long>(f.bracketSteps), static_cast<long long>(f.outerIterations));
  std::printf("  trials               %lld\n\n", static_cast<long long>(f.trials.size()));

  // ---- the fail-closed check on the trial cap
  Index worstSteps = 0;
  std::size_t unsettled = 0;
  std::size_t measured = 0;
  for (const PremixedFlameTrial& t : f.trials) {
    worstSteps = std::max(worstSteps, t.steps);
    if (t.measured) {
      ++measured;
      if (!t.settled) {
        ++unsettled;
      }
    }
  }
  std::printf("=== DID THE TRIAL CAP BIND? (fail-closed: an unsettled trial voids S_L)\n");
  std::printf("  trials that measured a drift   %zu\n", measured);
  std::printf("  of those, NOT settled          %zu\n", unsettled);
  std::printf("  worst steps used by any trial  %lld   against this run's cap %lld\n",
              static_cast<long long>(worstSteps), static_cast<long long>(kTrialCap));
  std::printf("  would fixture_probe's cap of %lld have truncated it?  %s\n\n",
              static_cast<long long>(kFixtureProbeCap),
              (worstSteps > kFixtureProbeCap) ? "YES" : "no");

  if (unsettled > 0) {
    std::printf("  *** %zu trial(s) ran out of budget rather than settling. S_L from this level is\n",
                unsettled);
    std::printf("      NOT trustworthy, whatever the wall time says. Stopping.\n");
    return 0;
  }

  std::printf("=== THE VERDICT ON AFFORDABILITY\n\n");
  if (wall <= kCap) {
    std::printf("  %.1f s <= %.0f s -> THE 1000-CELL LEVEL FITS THE CAP, with %.0f s (%.0f %%) spare.\n",
                wall, kCap, kCap - wall, (kCap - wall) / kCap * 100.0);
    std::printf("  C6a's premise -- that the expensive triplet is unaffordable -- does NOT hold at\n");
    std::printf("  the real cap. logs/07 ruled it out at 1750 s.\n");
  } else {
    std::printf("  %.1f s > %.0f s -> it does NOT fit. C6a's existence is justified and the\n", wall, kCap);
    std::printf("  amendment question in amendment_A6_proposal.md §4 stands as posed.\n");
  }

  std::printf("\n=== THE {250, 500, 1000} TRIPLET -- PROVISIONAL\n\n");
  std::printf("  fixture_probe measured 250 -> 4.94863 and 500 -> 4.95533 at trial cap %lld.\n",
              static_cast<long long>(kFixtureProbeCap));
  std::printf("  This level used %lld. If the caps differ in whether they BIND, the family is not\n",
              static_cast<long long>(kTrialCap));
  std::printf("  self-similar (CLAUDE.md §6) and the order below is NOT admissible -- it is printed\n");
  std::printf("  to size the follow-up, not to be used.\n\n");
  const Real s250 = 4.94863;
  const Real s500 = 4.95533;
  const Real s1000 = f.flameSpeed * 100.0;
  const Real d1 = s500 - s250;
  const Real d2 = s1000 - s500;
  const Real q = s500 * (std::pow(2.0, 1.0 / 1024.0) - 1.0);
  std::printf("  diffs  %+.6e  %+.6e cm/s   = %.1f and %.1f lattice quanta\n", d1, d2, d1 / q,
              d2 / q);
  if ((d1 > 0.0) == (d2 > 0.0) && d2 != 0.0) {
    std::printf("  p = %.3f   (the record's value for this family is 1.93)\n",
                std::log2(std::abs(d1 / d2)));
  } else {
    std::printf("  SIGN CHANGE or zero fine difference -- order undefined\n");
  }
  return 0;
}
