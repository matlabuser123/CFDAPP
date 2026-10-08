// TD-005 C3 -- WHERE AND WHEN DOES THE FRONT DRIFT OFF THE ANCHOR?
//
// INVESTIGATION ONLY. No production file is changed by this probe.
//
// WHY THIS IS POSSIBLE NOW AND WAS NOT BEFORE. Until C4d, `result.flamePosition` was the anchor
// INPUT echoed back, so no probe could see the front move at all. It is now measured from the
// reported field, which makes the front a dependent variable for the first time.
//
// HOW A TIME SERIES IS OBTAINED WITHOUT INSTRUMENTING PRODUCTION. The solver is deterministic and
// `finalMarchRelaxationTimes` scales ONLY the final march -- the trials, the bracketing and
// therefore the state ENTERING the final march are identical across these runs. So the reported
// final state at relaxT = k IS the march state after k relaxation times, and sweeping k samples the
// march trajectory. Nothing is added to the production path to get it.
//
// THE QUESTION THIS SETTLES, and both answers are informative:
//
//   INHERITED   if the front is already tens of cells off-anchor at the SMALLEST k, it was
//               delivered that way by the trials and the bracketing, and the final march is only
//               reporting a state it was handed. The diagnosis then moves to the trials.
//   ACQUIRED    if the offset GROWS with k, the final march itself is walking the front away from
//               the anchor, and the trajectory shows whether it creeps or jumps.
//
// This is the measurement TD-004's summary section 7.4 recommended and never took, and the reason
// it is being taken now rather than a fourth hypothesis being offered: three hypotheses for the
// inadmissible state have been refuted (insufficient relaxation, inlet proximity, bracket width),
// and every one of them was inferred from counters DERIVED from the profile.
//
// NO PREDICTION IS MADE. The pre-registration discipline this item has been using applies to
// hypotheses, and there is no hypothesis here -- only a dependent variable that can finally be
// observed. Recording a guess would invite fitting the reading to it.
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
constexpr Index kCells = 500;
constexpr Real kAnchorFraction = 0.35;

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

PremixedFlameSettings baseSettings() {
  PremixedFlameSettings settings;
  settings.cells = kCells;
  settings.domainLength = kDomain;
  settings.anchorFraction = kAnchorFraction;
  settings.tolerance = 1.0e-3;
  settings.maxOuterIterations = 40;
  settings.driftSettleTolerance = 0.05;
  settings.maximumStepsPerTrial = 20000;
  return settings;
}

// The front's own definition, applied to the reported field, so the probe does not have to trust a
// single reported scalar: the FIRST tAnchor crossing from the inlet. This duplicates what C4d's fix
// now does inside the solver, deliberately -- an independent recomputation is what makes
// `flamePosition` checkable rather than merely reported.
Real crossingOf(const PremixedFlameResult& f, Real tAnchor) {
  const Index nc = static_cast<Index>(f.temperature.size());
  for (Index c = 0; c + 1 < nc; ++c) {
    const Real below = f.temperature[static_cast<std::size_t>(c)] - tAnchor;
    const Real above = f.temperature[static_cast<std::size_t>(c + 1)] - tAnchor;
    if (below * above <= 0.0 &&
        f.temperature[static_cast<std::size_t>(c)] != f.temperature[static_cast<std::size_t>(c + 1)]) {
      return f.x[static_cast<std::size_t>(c)] +
             (tAnchor - f.temperature[static_cast<std::size_t>(c)]) /
                 (f.temperature[static_cast<std::size_t>(c + 1)] -
                  f.temperature[static_cast<std::size_t>(c)]) *
                 (f.x[static_cast<std::size_t>(c + 1)] - f.x[static_cast<std::size_t>(c)]);
    }
  }
  return 0.0;
}

// *** DEFECT FOUND AND FIXED, recorded because the failure mode is instructive.
// The first version looped `for (Index k = -60; k <= 60; k += 15)`. `cfd::Index` is
// `std::size_t` -- UNSIGNED (include/cfd/core/Types.hpp:13) -- so `k = -60` wrapped to
// ~1.8e19, failed `k <= 60` immediately, and the loop ran ZERO times. logs/10 printed the
// column header and no rows, for all four dumps.
//
// It failed in the SAFE direction (nothing printed rather than wrong values), but an empty
// table under a header can read as "nothing to show", so the guard below now says so
// explicitly. Offsets use a signed type.
void profileAroundFront(const PremixedFlameResult& f, Real front) {
  const Index nc = static_cast<Index>(f.temperature.size());
  const Real cellSize = kDomain / static_cast<Real>(kCells);
  Index centre = static_cast<Index>(front / cellSize);
  if (centre < 0) {
    centre = 0;
  }
  std::printf("      %6s %9s %10s %12s %11s %13s\n", "cell", "x mm", "T K", "Y_F", "u m/s",
              "S_E W/m3");
  int rows = 0;
  for (long long k = -90; k <= 90; k += 10) {
    const long long cc = static_cast<long long>(centre) + k;
    if (cc < 0 || cc >= static_cast<long long>(nc)) {
      continue;
    }
    const Index c = static_cast<Index>(cc);
    ++rows;
    std::printf("      %6lld %9.4f %10.2f %12.6e %11.5f %13.4e%s\n", static_cast<long long>(c),
                f.x[static_cast<std::size_t>(c)] * 1000.0,
                f.temperature[static_cast<std::size_t>(c)],
                f.massFractions[0][static_cast<std::size_t>(c)],
                f.velocity[static_cast<std::size_t>(c)],
                f.heatRelease[static_cast<std::size_t>(c)], (k == 0) ? "   <- front" : "");
  }
  if (rows == 0) {
    std::printf("      *** FIELD DUMP EMPTY -- the probe printed no rows. This is a PROBE DEFECT,\n");
    std::printf("          not a physical result: do NOT read it as \"no features present\".\n");
  }
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

  const Real cellSize = kDomain / static_cast<Real>(kCells);
  const Real xAnchor = kAnchorFraction * kDomain;

  std::printf("TD-005 C3 -- THE FRONT'S TRAJECTORY THROUGH THE FINAL MARCH\n\n");
  std::printf("  500 cells / 10 mm / anchor %.2f = %.3f mm; dt and trials IDENTICAL across rows,\n",
              kAnchorFraction, xAnchor * 1000.0);
  std::printf("  because finalMarchRelaxationTimes scales ONLY the final march.\n");
  std::printf("  driftWindow 16 = the inadmissible mode (fuel balance 0.3647, delta 0.7365 mm)\n");
  std::printf("  driftWindow 32 = the admissible mode  (fuel balance 0.0153, delta 1.2289 mm)\n\n");

  // driftWindow 16 ONLY. logs/10 recorded window 32's full ladder and it never leaves the
  // anchor (+1.0 to +1.2 cells throughout), so re-running it would spend six solves
  // reproducing a flat line. The comparison that matters is already in logs/10.
  for (const Index window : {16}) {
    std::printf("================================================================ driftWindow %lld\n",
                static_cast<long long>(window));
    std::printf("%8s %9s %11s %12s %10s %10s %10s %9s %8s %8s %8s\n", "relaxT", "marchStp",
                "front mm", "offset cells", "fuel bal", "S_c cm/s", "delta mm", "S_L cm/s",
                "worstJmp", "shifts", "rejects");
    std::fflush(stdout);

    std::vector<Real> fronts;
    std::vector<PremixedFlameResult> keep;
    // logs/10 established the shape: both modes hold the front within ~2 cells for 1589
    // steps with the balance IMPROVING monotonically, then driftWindow 16 departs to -46.6
    // cells by 3973 steps. The event is between 4 and 10 relaxation times, so THAT is what
    // to sample; re-measuring 0.25 to 2.0 would reproduce rows already in logs/10.
    const std::vector<Real> ladder = {4.0, 5.0, 6.0, 7.0, 8.0, 10.0};

    for (const Real times : ladder) {
      auto s = baseSettings();
      s.driftWindow = window;
      s.finalMarchRelaxationTimes = times;
      const PremixedFlameResult f = solvePremixedFlame(chemistry, conditions, s);

      // tAnchor is not reported, so it is reconstructed from the reported adiabatic temperature
      // and the setting that defines it -- the same expression the solver uses.
      const Real tAnchor =
          kUnburntT + s.anchorTemperatureFraction * (f.adiabaticTemperature - kUnburntT);
      const Real independent = crossingOf(f, tAnchor);
      const Real offsetCells = (f.flamePosition - xAnchor) / cellSize;

      std::printf("%8.2f %9lld %11.5f %12.2f %10.3e %10.5f %10.4f %9.5f %8.2f %8lld %8lld",
                  times, static_cast<long long>(f.finalMarchSteps), f.flamePosition * 1000.0,
                  offsetCells, f.fuelBalanceResidual, f.consumptionSpeed * 100.0,
                  f.flameThickness * 1000.0, f.flameSpeed * 100.0, f.worstFrontJumpCells,
                  static_cast<long long>(f.largestShiftCells),
                  static_cast<long long>(f.rejectedFrontCrossings));
      // C4d's own check, independently: the reported front must equal the field's crossing.
      if (std::abs(independent - f.flamePosition) > cellSize) {
        std::printf("   *** flamePosition DISAGREES with the field by %.2f cells",
                    (independent - f.flamePosition) / cellSize);
      }
      std::printf("\n");
      std::fflush(stdout);
      fronts.push_back(f.flamePosition);
      keep.push_back(f);
    }

    // ---- the shape of the trajectory, stated from the numbers rather than asserted
    std::printf("\n  TRAJECTORY of the front offset, in cells:\n     ");
    for (std::size_t i = 0; i < fronts.size(); ++i) {
      std::printf(" %+.1f", (fronts[i] - xAnchor) / cellSize);
    }
    std::printf("\n");
    const Real first = (fronts.front() - xAnchor) / cellSize;
    const Real last = (fronts.back() - xAnchor) / cellSize;
    std::printf("  at the SHORTEST march (%.2f relax times): %+.1f cells\n", ladder.front(), first);
    std::printf("  at the LONGEST  march (%.2f relax times): %+.1f cells\n", ladder.back(), last);
    if (std::abs(first) > 10.0) {
      std::printf("  => INHERITED: the front is already far off-anchor before the final march has\n");
      std::printf("     done any work. The trials delivered it that way; the march is not the cause.\n");
    } else if (std::abs(last) > 10.0) {
      std::printf("  => ACQUIRED: the front STARTS near the anchor and walks away DURING the final\n");
      std::printf("     march. The trajectory above shows whether it creeps or jumps.\n");
    } else {
      std::printf("  => the front stays near the anchor at every march length in this ladder.\n");
    }
    std::fflush(stdout);

    // ---- the field at EVERY rung. The event is INSIDE this ladder, so the progression is
    //      the measurement; two end-points would only show the before and after again.
    for (std::size_t i = 0; i < keep.size(); ++i) {
      std::printf("\n  FIELD at relaxT %.2f (%lld march steps), front %+.1f cells off anchor:\n",
                  ladder[i], static_cast<long long>(keep[i].finalMarchSteps),
                  (fronts[i] - xAnchor) / cellSize);
      profileAroundFront(keep[i], fronts[i]);
    }
    std::printf("\n");
    std::fflush(stdout);
  }

  std::printf("HOW TO READ THIS\n");
  std::printf("  The two driftWindow blocks share every setting except the window, so comparing\n");
  std::printf("  their trajectories isolates what the window does to the front -- which is the\n");
  std::printf("  one thing logs/01 sweep A could not explain (16 and 64 bad, 32 good).\n");
  std::printf("  A *** line means the reported flamePosition does not match the field's own\n");
  std::printf("  crossing, which would be a C4d regression and must stop everything.\n");
  return 0;
}
