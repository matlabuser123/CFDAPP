// TD-004 -- DOES MORE UPSTREAM ROOM STOP THE INLET IGNITING?
//
// INVESTIGATION ONLY. No production file is changed by this probe.
//
// THE DIAGNOSIS IT TESTS. logs/15 measured that the single tAnchor crossing relocates to
// x = 0.1969 mm -- cell 9 of 500, hard against the inlet -- on 3952 of 11905 steps at relaxT 30 and
// 15858 of 35717 at relaxT 90, while landing on the 3.5 mm anchor exactly ZERO times. A 1050 K
// crossing nine cells from a Dirichlet boundary pinned at 300 K is not the front moving: the inlet
// region is IGNITING. The old locator followed that to the inlet and executed a 164-cell shift,
// which is why the fuel balance tracked the big-jump count so exactly.
//
// This is the classic cold-boundary difficulty, and the prior work already named it -- audit.md
// property 3 is "S_L plateaus in anchor position and domain length -- the cold-boundary
// difficulty". The fixture leaves only 3.5 mm of upstream room against a measured preheat thickness
// of 1.229 mm, i.e. under three thicknesses.
//
// WHY THE ANCHOR FRACTION AND NOT THE DOMAIN LENGTH. Both add upstream room, but lengthening the
// domain at a fixed cell count coarsens the mesh -- and logs/08 showed the coarse levels are
// qualitatively compromised (p = 0.523 at 63 cells, 47-80 % fuel imbalance). Moving the anchor
// downstream adds upstream room at IDENTICAL resolution and identical cost, so it changes one thing.
//
// WHAT WOULD CONFIRM THE DIAGNOSIS. The >50-cell jump count falling to zero as upstream room grows,
// with the fuel balance following it down. What would REFUTE it: the jumps persisting, which would
// mean the ignition is not caused by proximity to the inlet and the cold-boundary reading is wrong.
//
// This sweep is ALSO property 3's own measurement -- S_L must plateau in the anchor position -- so
// a flat S_L column is evidence for a criterion the suite currently cannot run at all.
#include <chrono>
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

  std::printf("TD-004 -- upstream room vs inlet ignition\n\n");
  std::printf("  500 cells over 10 mm; relaxT 30, where logs/15 measured 3952 jumps to x=0.1969 mm\n");
  std::printf("  measured preheat thickness 1.229 mm, so upstream room is quoted in thicknesses\n\n");
  std::printf("%8s %10s %9s %11s %12s %9s %9s %10s %9s\n", "anchorF", "upstream", "x/delta",
              ">50c jumps", "fuel bal", "S_L cm/s", "x_front", "worst jump", "wall s");

  for (const Real anchorFraction : {0.35, 0.50, 0.65, 0.80}) {
    PremixedFlameSettings settings;
    settings.cells = 500;
    settings.domainLength = kDomain;
    settings.anchorFraction = anchorFraction;
    settings.tolerance = 1.0e-3;
    settings.maxOuterIterations = 40;
    settings.driftSettleTolerance = 0.05;
    settings.maximumStepsPerTrial = 20000;
    settings.finalMarchRelaxationTimes = 30.0;  // the configuration where the ignition is clear

    const auto t0 = std::chrono::steady_clock::now();
    const PremixedFlameResult flame = solvePremixedFlame(chemistry, conditions, settings);
    const Real wall = std::chrono::duration<Real>(std::chrono::steady_clock::now() - t0).count();

    const Real upstream = anchorFraction * kDomain;
    std::printf("%8.2f %10.4f %9.2f %11lld %12.6e %9.5f %9.5f %10.2f %9.1f\n", anchorFraction,
                upstream * 1000.0, upstream / 1.228906e-3,
                static_cast<long long>(flame.rejectedJumpsOver50Cells), flame.fuelBalanceResidual,
                flame.flameSpeed * 100.0, flame.flamePosition * 1000.0,
                flame.worstRejectedJumpCells, wall);
    std::fflush(stdout);
  }

  std::printf("\n  >50c jumps falling to 0 as upstream room grows, with the balance following,\n");
  std::printf("  CONFIRMS the inlet-ignition diagnosis. Jumps persisting REFUTES it.\n");
  std::printf("  A flat S_L column is property 3's own criterion (plateau in anchor position).\n");
  return 0;
}
