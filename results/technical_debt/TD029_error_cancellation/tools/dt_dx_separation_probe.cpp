// TD-029 RE-MEASURE -- do the SPATIAL and TEMPORAL errors still have OPPOSITE SIGNS on the
// TD-032-fixed march? And does that explain C6a's observed order?
//
// TD-029's RECORDED CLAIM (TD005_flame_speed_extraction/dry_run.md §L.3, measured PRE-TD-032):
//
//   500 cells, dt HALVED  1.885e-05   S_L = 4.65615 cm/s   -4.24 %   TEMPORAL error
//   250 cells, dt pinned  3.770e-05   S_L = 4.82296 cm/s   -0.81 %   SPATIAL error at fixed dt
//
//   "refining dx RAISES S_L (+0.81 % from 250 to 500 at fixed dt)"
//   "refining dt LOWERS S_L (-4.24 % from dt to dt/2 at fixed dx)"
//   "So under combined refinement they partially cancel, and the test reports convergence while
//    neither parameter is converged."
//
// Both numbers were measured on the ALTERNATING field, so both are invalid as authoritative and
// this probe re-measures them. The claim that matters is about SIGNS, not magnitudes.
//
// WHY THE MAGNITUDES ARE NOT COMPARABLE, AND THE SIGNS ARE. TD-029's base dt of 3.770e-05 at 500
// cells is a cell diffusion number of 20 -- the cap TD-027 REMOVED, lowering it to 5.0. Reproducing
// that dt would re-run a configuration the project has since rejected. So this probe uses the
// current-cap base dt, 9.4243e-06 (D = 5), and the recorded -0.81 % / -4.24 % are NOT targets.
// What carries over unchanged is TD-029's actual assertion: the two errors point in OPPOSITE
// directions and therefore cancel under coupled refinement.
//
// WHY THIS ALSO BEARS ON C6a. verification_A5.md §2.2 recorded that C6a's observed order left the
// frozen [1.5, 2.5] band (2.057 -> 2.693) and offered the dt/dx coupling as a CANDIDATE, explicitly
// NOT established, because that family refines `cells` while dt ~ dx^2 -- so it varies both at once.
// This probe varies them SEPARATELY, which is the discriminator §4 of that document named.
//
// THE THREE LEGS, chosen so each isolates one parameter:
//   A  500 cells, dt 9.4243e-06     the reference
//   B  250 cells, dt 9.4243e-06     dx doubled, dt FIXED      -> pure SPATIAL error
//   C  500 cells, dt 4.71215e-06    dt halved, dx FIXED       -> pure TEMPORAL error
// Pinning dt makes the step count identical between A and B, so B is simply cheaper, and C doubles
// it. finalMarchSteps{200000} is the ceiling, so C's ~30 600 steps is not clipped.
//
// PRE-REGISTERED READINGS, one per direction, no prediction between them:
//
//   signs OPPOSITE   -> cancellation stands on the fixed march. TD-029's finding survives TD-032,
//                       the mesh family is still not self-similar, and C6a's p = 2.693 is
//                       consistent with the coupling rather than with a defect.
//   signs the SAME   -> there is NO cancellation on the fixed march. TD-029's finding was an
//                       artifact of the alternating field, and C6a's order shift needs a different
//                       explanation. That would be a new finding and would reopen how C6a is read.
//   either error ~0  -> that parameter is converged at this fixture and the cancellation question
//                       is moot for it. Reporting a sign for a vanishing difference would be
//                       reading noise.
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

// The current-cap base dt at 500 cells: cell diffusion number 5.0, TD-027's shipped value.
constexpr Real kBaseDt = 9.4243e-06;

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

struct Leg {
  const char* name;
  Index cells;
  Real dt;
  Real sL{};
  long long steps{};
  Real balance{};
  Real front{};
  Real wall{};
  bool ok{false};
};

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

  std::printf("TD-029 RE-MEASURE -- SPATIAL and TEMPORAL error SIGNS, separated\n\n");
  std::printf("  RECORDED PRE-TD-032 (dry_run.md §L.3), on the alternating field:\n");
  std::printf("    spatial  (dx -> 2dx at fixed dt)   -0.81 %%   i.e. refining dx RAISES S_L\n");
  std::printf("    temporal (dt -> dt/2 at fixed dx)  -4.24 %%   i.e. refining dt LOWERS S_L\n");
  std::printf("    -> opposite directions, so they CANCEL under coupled refinement\n\n");
  std::printf("  base dt here is %.6e (cell diffusion number 5.0, TD-027's shipped cap).\n", kBaseDt);
  std::printf("  TD-029's own 3.770e-05 is D = 20, the cap TD-027 REMOVED, so the MAGNITUDES are\n");
  std::printf("  not comparable and are not targets. The SIGNS are what TD-029 claims.\n\n");
  std::printf("%4s %8s %14s %12s %10s %12s %10s %8s\n", "leg", "cells", "dt", "S_L cm/s", "steps",
              "fuel bal", "front mm", "wall s");
  std::fflush(stdout);

  std::vector<Leg> legs = {
      {"A", 500, kBaseDt},
      {"B", 250, kBaseDt},
      {"C", 500, kBaseDt * 0.5},
  };

  for (Leg& leg : legs) {
    PremixedFlameSettings s;
    s.cells = leg.cells;
    s.domainLength = kDomain;
    s.anchorFraction = kAnchorFraction;
    s.tolerance = 1.0e-3;
    s.maxOuterIterations = 40;
    s.driftSettleTolerance = 0.05;
    s.driftWindow = 32;
    s.timeStep = leg.dt;
    s.finalMarchRelaxationTimes = 10.0;
    s.maximumStepsPerTrial = 80000;

    const auto t0 = std::chrono::steady_clock::now();
    PremixedFlameResult f;
    try {
      f = solvePremixedFlame(chemistry, conditions, s);
    } catch (const std::exception& e) {
      std::printf("%4s %8lld %14.6e  THREW: %s\n", leg.name,
                  static_cast<long long>(leg.cells), leg.dt, e.what());
      std::fflush(stdout);
      continue;
    }
    leg.wall = std::chrono::duration<Real>(std::chrono::steady_clock::now() - t0).count();
    leg.sL = f.flameSpeed * 100.0;
    leg.steps = static_cast<long long>(f.finalMarchSteps);
    leg.balance = f.fuelBalanceResidual;
    leg.front = f.flamePosition * 1000.0;
    leg.ok = true;

    std::printf("%4s %8lld %14.6e %12.6f %10lld %12.4e %10.6f %8.0f\n", leg.name,
                static_cast<long long>(leg.cells), leg.dt, leg.sL, leg.steps, leg.balance,
                leg.front, leg.wall);
    std::fflush(stdout);
  }

  if (!(legs[0].ok && legs[1].ok && legs[2].ok)) {
    std::printf("\n*** a leg did not complete -- no sign comparison is possible\n");
    return 0;
  }

  const Real ref = legs[0].sL;
  // SPATIAL: coarsening dx from 500 to 250 at fixed dt. Reported as the change TD-029 reported,
  // i.e. the coarse value relative to the reference.
  const Real spatial = (legs[1].sL - ref) / ref * 100.0;
  // TEMPORAL: halving dt at fixed dx -- a REFINEMENT, so its sign is read directly.
  const Real temporal = (legs[2].sL - ref) / ref * 100.0;

  std::printf("\n=== THE TWO ERRORS, SEPARATED\n\n");
  std::printf("  reference  A  500 cells, dt %.6e        S_L %.6f\n", kBaseDt, ref);
  std::printf("  SPATIAL    B  250 cells, dt FIXED        S_L %.6f   %+.3f %%\n", legs[1].sL,
              spatial);
  std::printf("  TEMPORAL   C  500 cells, dt HALVED       S_L %.6f   %+.3f %%\n", legs[2].sL,
              temporal);
  std::printf("\n  refining dx (250 -> 500 at fixed dt) moves S_L %+.3f %%\n", -spatial);
  std::printf("  refining dt (dt -> dt/2 at fixed dx) moves S_L %+.3f %%\n", temporal);

  const Real refineDx = -spatial;
  const Real refineDt = temporal;
  // A difference this small is not a direction. 1e-4 % of ~5 cm/s is 5e-6 cm/s, which is the
  // sixth decimal the parity ladder showed to be reproducible -- below it, a sign is noise.
  const Real floorPct = 1.0e-4;
  const bool dxNull = std::abs(refineDx) < floorPct;
  const bool dtNull = std::abs(refineDt) < floorPct;

  std::printf("\n=== THE VERDICT ON TD-029's CLAIM\n\n");
  if (dxNull || dtNull) {
    std::printf("  AT LEAST ONE ERROR IS BELOW THE %0.0e %% FLOOR -- no direction is read from it.\n",
                floorPct);
    std::printf("    refine dx: %+.6f %%%s\n", refineDx, dxNull ? "   <- NULL" : "");
    std::printf("    refine dt: %+.6f %%%s\n", refineDt, dtNull ? "   <- NULL" : "");
    std::printf("  That parameter is converged at this fixture and the cancellation question is\n");
    std::printf("  moot for it. Reporting a sign for a vanishing difference would be reading noise.\n");
  } else if ((refineDx > 0.0) != (refineDt > 0.0)) {
    std::printf("  SIGNS ARE OPPOSITE. Cancellation STANDS on the TD-032-fixed march.\n");
    std::printf("    -> TD-029's finding survives: no mesh family that ties dt to dx is\n");
    std::printf("       self-similar, and a combined-refinement order mixes the two.\n");
    std::printf("    -> C6a's p = 2.693 is CONSISTENT with the coupling. Consistent is not\n");
    std::printf("       proven: this shows the mechanism is present, not that it accounts for\n");
    std::printf("       the whole 2.057 -> 2.693 shift.\n");
  } else {
    std::printf("  SIGNS ARE THE SAME. There is NO cancellation on the fixed march.\n");
    std::printf("    -> TD-029's recorded finding was an artifact of the ALTERNATING field.\n");
    std::printf("    -> C6a's order shift then needs a DIFFERENT explanation, and\n");
    std::printf("       verification_A5.md §2.2's candidate must be withdrawn.\n");
  }
  std::printf("\n  The ratio |temporal| / |spatial| is %.2f (recorded pre-fix: 5.23).\n",
              (std::abs(refineDx) > 0.0) ? std::abs(refineDt) / std::abs(refineDx) : 0.0);
  return 0;
}
