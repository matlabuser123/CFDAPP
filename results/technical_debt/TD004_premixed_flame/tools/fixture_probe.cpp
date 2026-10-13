// TD-004 -- CHOOSING THE REFINEMENT FIXTURE BY MEASUREMENT.
//
// INVESTIGATION ONLY. No production file is changed.
//
// WHY. `TheFlameSpeedConvergesUnderMeshRefinement` uses {250, 500, 1000} cells and does not finish
// in 1750 s (logs/07). The cost scales roughly as nc^3 -- the march length goes as 1/dt with dt
// diffusion-limited (dt ~ dx^2, so steps ~ nc^2) and the per-step solve cost goes as nc -- so from
// the measured 240 s at 500 cells the triplet costs about 30 + 240 + 1920 s. The 1000-cell level
// alone exceeds the budget.
//
// The authorized fix is to SHRINK THE FIXTURE and keep criterion C6 intact. Two constraints bound
// how far, and neither may be assumed:
//
//   1. THE FRONT MUST STAY RESOLVED. The test's own guard is thickness/cellSize > 4. Measured at
//      500 cells the thickness is 1.22891 mm against a 20 um cell, i.e. 61 cells -- so the guard
//      has room, but it has to be checked at every candidate level rather than extrapolated.
//
//   2. THE FAMILY MUST STAY IN THE ASYMPTOTIC RANGE. CLAUDE.md section 6: an observed-order claim
//      is valid only if the mesh family is self-similar AND asymptotic AND the error does not
//      change sign. A coarser triplet is LESS likely to be asymptotic, so moving the family
//      downward is exactly the change that can invalidate the order silently. The record's
//      p = 1.93 and GCI 0.21 % came from the EXPENSIVE triplet; whether a cheaper one supports the
//      same claim is a measurement, not an inference.
//
// So this sweeps a halving family and reports, per level: S_L, the front resolution in cells, the
// wall time, and then the successive differences and the observed order the family implies. The
// triplet chosen for the test is whichever one satisfies both constraints at an affordable cost --
// and the numbers are recorded so the choice can be re-derived rather than taken on trust, the same
// way DISABLED_ParameterWindowScan exists so the (A, Ea) choice can be.
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

// The test fixture's synthetic fuel, value for value.
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
  FormationEnthalpies formation =
      FormationEnthalpies::fromMolar(mixture, {0.0, kMolarFormationP});
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
  const PremixedFlameChemistry chemistry{&problem.mechanism, &problem.mixture, &problem.formation,
                                         &problem.kinetics};
  const cfd::physics::ConstantProperty conductivity(kConductivity);
  PremixedFlameConditions conditions;
  conditions.pressure = kPressure;
  conditions.unburntTemperature = kUnburntT;
  conditions.unburntComposition = {kFuelFraction, 1.0 - kFuelFraction};
  conditions.conductivity = &conductivity;
  conditions.specificHeat = kSpecificHeat;
  conditions.lewisNumber = 1.0;
  conditions.fuelIndex = 0;

  std::printf("TD-004 -- refinement fixture selection, measured\n\n");
  std::printf("  the test's current triplet {250, 500, 1000} does not finish in 1750 s (logs/07)\n");
  std::printf("  resolution guard: flameThickness / cellSize > 4\n\n");
  std::printf("%7s %11s %12s %9s %10s %9s %8s %9s\n", "cells", "S_L cm/s", "thickness mm",
              "res cells", "wall s", "steps", "status", "fuel bal");

  // A HALVING family, so it is self-similar and parity-consistent (CLAUDE.md §6). Coarsest first:
  // if the budget runs out, the cheap levels are the ones that survive.
  const Index levels[] = {63, 125, 250, 500};
  std::vector<Real> speeds;
  std::vector<Index> used;

  for (const Index cells : levels) {
    PremixedFlameSettings settings;
    settings.cells = cells;
    settings.domainLength = kDomain;
    settings.anchorFraction = 0.35;
    settings.tolerance = 1.0e-3;
    settings.maxOuterIterations = 40;
    settings.driftSettleTolerance = 0.05;
    settings.maximumStepsPerTrial = 20000;

    const auto t0 = std::chrono::steady_clock::now();
    const PremixedFlameResult flame = solvePremixedFlame(chemistry, conditions, settings);
    const Real wall =
        std::chrono::duration<Real>(std::chrono::steady_clock::now() - t0).count();

    const Real cellSize = kDomain / static_cast<Real>(cells);
    const Real resolution = flame.flameThickness / cellSize;
    const bool converged = flame.status == PremixedFlameStatus::Converged;

    std::printf("%7lld %11.5f %12.6f %9.2f %10.1f %9lld %8s %9.4f\n",
                static_cast<long long>(cells), flame.flameSpeed * 100.0,
                flame.flameThickness * 1000.0, resolution, wall,
                static_cast<long long>(flame.totalTimeSteps), converged ? "OK" : "NOT-CONV",
                flame.fuelBalanceResidual);
    std::fflush(stdout);

    if (converged && resolution > 4.0) {
      speeds.push_back(flame.flameSpeed);
      used.push_back(cells);
    } else {
      std::printf("        ^ EXCLUDED from the family: %s\n",
                  converged ? "front spans <= 4 cells" : "did not converge");
    }
  }

  // ---- the two things that decide whether a cheaper triplet is admissible
  std::printf("\nSUCCESSIVE DIFFERENCES AND OBSERVED ORDER\n\n");
  if (speeds.size() < 3) {
    std::printf("  fewer than 3 admissible levels -- no triplet can be formed\n");
    return 0;
  }
  std::printf("%26s %14s %14s %9s\n", "triplet", "coarse diff", "fine diff", "order p");
  for (std::size_t i = 0; i + 2 < speeds.size(); ++i) {
    const Real d1 = speeds[i + 1] - speeds[i];
    const Real d2 = speeds[i + 2] - speeds[i + 1];
    // p from a halving family: p = log2(|d1/d2|). Sign-consistency is checked separately, because
    // an order across a zero crossing is undefined (CLAUDE.md §6).
    const bool sameSign = (d1 > 0.0) == (d2 > 0.0);
    const Real p = (d2 != 0.0) ? std::log2(std::abs(d1 / d2)) : 0.0;
    char label[48];
    std::snprintf(label, sizeof(label), "{%lld, %lld, %lld}", static_cast<long long>(used[i]),
                  static_cast<long long>(used[i + 1]), static_cast<long long>(used[i + 2]));
    std::printf("%26s %+14.6e %+14.6e %9.3f%s\n", label, d1, d2, p,
                sameSign ? "" : "   <- SIGN CHANGE: order undefined");
  }

  std::printf("\nHOW TO READ THIS\n");
  std::printf("  A triplet is admissible only if ALL of these hold:\n");
  std::printf("    * every level converged and spans > 4 cells of front;\n");
  std::printf("    * the differences SHRINK (|fine| < |coarse|) -- the test's own criterion;\n");
  std::printf("    * no sign change -- an observed order across a zero crossing is undefined;\n");
  std::printf("    * p is near the record's 1.93, which is the evidence that the cheaper family\n");
  std::printf("      is still in the asymptotic range rather than merely cheaper.\n");
  std::printf("  If the cheapest admissible triplet's p departs from 1.93, the family has left the\n");
  std::printf("  asymptotic range and shrinking the fixture is NOT a free substitution -- that\n");
  std::printf("  would have to be reported, not absorbed.\n");
  return 0;
}
