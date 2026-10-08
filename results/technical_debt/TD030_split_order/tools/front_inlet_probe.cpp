// TD-030 AUDIT -- WHERE IS THE FRONT, AND WHY HAS THE INLET DIFFUSIVE FLUX GROWN 9x?
//
// THE QUESTION. Re-measuring TD-005's fuel budget on the fixed tree (TD030 logs/01) found the
// reaction excess HALVED, 7.892 % -> 3.9 %, but the inlet diffusive term NINEFOLD larger,
// 0.572 % -> ~5.0 %. The solver's balance is
//
//     |convection - reaction - inletDiffusion| / fuelThrough
//
// and `inletDiffusion = gamma * (Y_F[0] - Y_F,u) / (dx/2)`, so that term is driven ENTIRELY by how
// far the first cell's fuel has fallen below the inlet value. Backing that out:
//
//     section O era   Y_F[0] = 4.999621924e-02   deficit -3.78e-06
//     now             Y_F[0] = 4.996720579e-02   deficit -3.28e-05     8.7x larger
//
// HYPOTHESIS, and it is only that: TD-032's fix moved the flame UPSTREAM, so the preheat zone now
// reaches the inlet and depletes the first cell. If true, the new 5 % term is a DOMAIN/ANCHOR
// effect, not a chemistry one -- and it is then TD-004's or TD-026's business, not TD-030's.
//
// PRE-REGISTERED READINGS, one per direction, written before the run:
//
//   front moves UPSTREAM as relaxT grows AND the deficit grows with it
//        -> the hypothesis holds. The inlet term tracks front position and TD-030's scope shrinks
//           back to the reaction excess alone.
//   front is STATIONARY across the ladder while the deficit still grows
//        -> position is NOT the driver. Something else is depleting the first cell, and the
//           hypothesis is dead.
//   front moves DOWNSTREAM (toward the anchor) while the deficit grows
//        -> the two are anti-correlated and the hypothesis is dead in the opposite direction.
//   deficit does NOT grow across the ladder
//        -> logs/01's 4.965 % -> 5.338 % was not a trend; stop and re-read that instead.
//
// THE UPSTREAM PROFILE is printed because a single number cannot tell a flame that has MOVED from
// one that has SPREAD: a front at the same place with a longer preheat tail depletes the inlet just
// as well.
//
// Fixture: EXACTLY TD-005's fuel_budget_probe -- 500 cells, dt pinned at 9.4243e-06 (D = 0.833),
// driftWindow 32, maximumStepsPerTrial 80000 -- so this is directly comparable to logs/01.
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
constexpr Real kDtQuarter = 9.4243e-06;

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
  const Real halfCell = 0.5 * dx;
  const Real gamma = kConductivity / (kSpecificHeat * 1.0);
  const Real xAnchor = kAnchorFraction * kDomain;

  std::printf("TD-030 AUDIT -- FRONT POSITION vs INLET FUEL DEFICIT\n\n");
  std::printf("  fixture identical to TD-005's fuel_budget_probe: 500 cells, dt %.4e (D = 0.833)\n",
              kDtQuarter);
  std::printf("  anchor x = %.3f mm   gamma = %.6e   half cell = %.3e m\n\n", xAnchor * 1000.0,
              gamma, halfCell);
  std::printf("%7s %9s %11s %11s %14s %12s %11s %9s\n", "relaxT", "marchStp", "front mm",
              "delta mm", "Y_F[0]", "deficit", "inletDiff%", "S_L");
  std::fflush(stdout);

  struct Row {
    Real relaxT, front, deficit;
  };
  std::vector<Row> rows;
  std::vector<std::vector<Real>> profiles;

  for (const Real relaxT : {10.0, 14.0, 18.0, 22.0, 26.0, 30.0}) {
    PremixedFlameSettings s;
    s.cells = kCells;
    s.domainLength = kDomain;
    s.anchorFraction = kAnchorFraction;
    s.tolerance = 1.0e-3;
    s.maxOuterIterations = 40;
    s.driftSettleTolerance = 0.05;
    s.driftWindow = 32;
    s.timeStep = kDtQuarter;
    s.finalMarchRelaxationTimes = relaxT;
    s.maximumStepsPerTrial = 80000;

    const PremixedFlameResult f = solvePremixedFlame(chemistry, conditions, s);
    if (f.massFractions.empty() || f.massFractions[0].empty()) {
      std::printf("%7.0f   *** EMPTY composition -- probe cannot measure\n", relaxT);
      return 1;
    }
    const std::vector<Real>& yF = f.massFractions[0];
    const Real deficit = yF[0] - kFuelFraction;
    const Real inletDiff = gamma * deficit / halfCell;
    const Real fuelThrough = f.massFlux * kFuelFraction;

    std::printf("%7.0f %9lld %11.6f %11.6f %14.9e %12.4e %11.3f %9.5f\n", relaxT,
                static_cast<long long>(f.finalMarchSteps), f.flamePosition * 1000.0,
                f.flameThickness * 1000.0, yF[0], deficit,
                inletDiff / fuelThrough * 100.0, f.flameSpeed * 100.0);
    std::fflush(stdout);
    rows.push_back(Row{relaxT, f.flamePosition * 1000.0, deficit});

    std::vector<Real> prof;
    for (const Index c : {Index(0), Index(2), Index(5), Index(10), Index(20), Index(40),
                          Index(80), Index(120)}) {
      prof.push_back(yF[static_cast<std::size_t>(c)]);
    }
    profiles.push_back(prof);
  }

  // ---- the upstream profile: a flame that MOVED looks different from one that SPREAD
  std::printf("\n=== UPSTREAM FUEL PROFILE, Y_F at the first cells (inlet value %.3f)\n",
              kFuelFraction);
  std::printf("%7s", "relaxT");
  for (const Index c : {Index(0), Index(2), Index(5), Index(10), Index(20), Index(40), Index(80),
                        Index(120)}) {
    std::printf("  x=%.2fmm", static_cast<Real>(c) * dx * 1000.0);
  }
  std::printf("\n");
  for (std::size_t i = 0; i < profiles.size(); ++i) {
    std::printf("%7.0f", rows[i].relaxT);
    for (const Real y : profiles[i]) {
      std::printf(" %9.6f", y);
    }
    std::printf("\n");
  }

  // ---- the pre-registered readings, applied
  std::printf("\n=== READING, against the directions registered in this file's header\n");
  const Real frontMove = rows.back().front - rows.front().front;
  const Real deficitRatio = (rows.front().deficit != 0.0)
                                ? rows.back().deficit / rows.front().deficit
                                : 0.0;
  std::printf("    front  relaxT 10 -> 30 : %.6f -> %.6f mm   (%+.6f mm)\n", rows.front().front,
              rows.back().front, frontMove);
  std::printf("    deficit relaxT 10 -> 30: %.4e -> %.4e      (x %.3f)\n", rows.front().deficit,
              rows.back().deficit, deficitRatio);
  std::printf("\n");
  if (deficitRatio <= 1.0) {
    std::printf("    => THE DEFICIT DOES NOT GROW across the ladder. logs/01's 4.965 -> 5.338 %%\n");
    std::printf("       was not a trend. Stop and re-read that before going further.\n");
  } else if (std::abs(frontMove) < 0.01) {
    std::printf("    => THE FRONT IS STATIONARY (<10 um) while the deficit grows. Position is NOT\n");
    std::printf("       the driver and the hypothesis is DEAD. Read the profile above: if the\n");
    std::printf("       upstream tail is rising, the flame SPREAD rather than moved.\n");
  } else if (frontMove < 0.0) {
    std::printf("    => the front moves UPSTREAM and the deficit grows: the hypothesis HOLDS, and\n");
    std::printf("       the inlet term is a domain/anchor effect rather than a chemistry one.\n");
  } else {
    std::printf("    => the front moves DOWNSTREAM while the deficit grows. They are\n");
    std::printf("       ANTI-correlated and the hypothesis is dead in the opposite direction.\n");
  }
  return 0;
}
