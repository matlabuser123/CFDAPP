// TD-005 -- IS THE CHEMISTRY SUB-STEP LIMITER RESPONSIBLE FOR THE 7.9 % REACTION EXCESS?
//
// INVESTIGATION ONLY. No production file is changed by this probe.
//
// THE FINDING UNDER TEST. logs/20 pinned the open defect to ONE term: on the converged dt/4 state
// the reaction integral is -107.892 % of fuelThrough while convection is exactly -100.000 %, inlet
// diffusion is -0.572 %, the omitted outlet flux is 1e-14 and the omitted accumulation term is
// +0.350 %. The excess is identically the S_c / S_L ratio (1.07892 both ways), and it GREW with dt
// refinement: +0.776 % at the default dt, +7.892 % at dt/4.
//
// THE CANDIDATE. The march applies chemistry in an operator split under a sub-step limiter --
// `chemistryFractionalChange = 0.1`, "a sub-step may consume at most this fraction of any species'
// local mass". The reported `fuelIntegral` is instead RECOMPUTED from the final fields by
// `arrheniusProgressRatesFromFields`, with no limiter. If the march under-resolves the chemistry,
// its steady profile carries more fuel than it should, the instantaneous rate recomputed on that
// profile is too high, and S_c exceeds S_L. Tightening the limiter integrates the chemistry more
// accurately, so the mismatch should SHRINK.
//
// A DIRECT OBSERVABLE MAKES THIS SHARPER THAN A SWEEP ALONE. `result.chemistrySubSteps` counts
// every chemistry sub-step over the whole search, so sub-steps PER TIME STEP says whether the
// limiter binds at all:
//
//     ~1 per step        the limiter NEVER binds, the chemistry takes one sub-step per march step,
//                        and the hypothesis is DEAD ON THE FIRST ROW -- no sweep needed.
//     >> 1 per step      the limiter is active, and the sweep then says whether it is responsible.
//
// PRE-REGISTERED, with a falsifier in each direction:
//
//   LIMITER RESPONSIBLE   the mismatch shrinks monotonically as the fraction tightens
//                         0.1 -> 0.03 -> 0.01, and sub-steps per step rises. If it extrapolates
//                         toward zero, the reaction excess is a chemistry-resolution artefact and
//                         `chemistryFractionalChange = 0.1` is a second unvalidated resolution
//                         constant alongside maximumCellDiffusionNumber = 20.0 (section M.4).
//   LIMITER CLEARED       the mismatch is unchanged across a 10x range of the fraction. Then the
//                         7.892 % is NOT the limiter and the search moves to the operator split
//                         itself, or to the recomputation of the rates on the final state.
//   UNEXPECTED            the mismatch GROWS as the limiter tightens. That would mean more
//                         accurate chemistry makes the two measures disagree more, which points at
//                         the SPLIT rather than at either half of it.
//
// COST NOTE. Tightening the fraction multiplies the chemistry work, so the 0.01 row is the
// expensive one. relaxT is held at 22 -- inside the converged plateau established in logs/19 and
// the same point logs/20 measured -- so each row is one solve and directly comparable.
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

  std::printf("TD-005 -- DOES THE CHEMISTRY SUB-STEP LIMITER CAUSE THE REACTION EXCESS?\n\n");
  std::printf("  500 cells, dt = %.4e s (D = 0.833), relaxT 22 -- the converged point logs/20 used\n",
              kDtQuarter);
  std::printf("  logs/20 at the default fraction 0.1:  S_L 4.67194  S_c 5.04065  mismatch +7.892 %%\n");
  std::printf("  reaction term -107.892 %% of fuelThrough; convection exactly -100.000 %%\n\n");
  std::printf("  KEY COLUMN is subSteps/step: ~1 means the limiter NEVER BINDS and the hypothesis\n");
  std::printf("  is dead on the first row, whatever the sweep then shows.\n\n");
  std::printf("%9s %10s %10s %10s %11s %13s %10s %9s\n", "fraction", "S_L cm/s", "S_c cm/s",
              "mismatch", "fuel bal", "chemSubSteps", "perStep", "wall s");
  std::fflush(stdout);

  std::vector<Real> fractions = {0.1, 0.03, 0.01};
  std::vector<Real> mismatches;
  std::vector<Real> perStep;

  for (const Real frac : fractions) {
    PremixedFlameSettings s;
    s.cells = kCells;
    s.domainLength = kDomain;
    s.anchorFraction = kAnchorFraction;
    s.tolerance = 1.0e-3;
    s.maxOuterIterations = 40;
    s.driftSettleTolerance = 0.05;
    s.driftWindow = 32;
    s.timeStep = kDtQuarter;
    s.finalMarchRelaxationTimes = 22.0;
    s.maximumStepsPerTrial = 80000;
    s.chemistryFractionalChange = frac;

    const auto t0 = std::chrono::steady_clock::now();
    const PremixedFlameResult f = solvePremixedFlame(chemistry, conditions, s);
    const Real wall = std::chrono::duration<Real>(std::chrono::steady_clock::now() - t0).count();

    const Real mismatch = (f.flameSpeed != 0.0)
                              ? (f.consumptionSpeed - f.flameSpeed) / f.flameSpeed
                              : 0.0;
    const Real ratio = (f.totalTimeSteps != 0)
                           ? static_cast<Real>(f.chemistrySubSteps) /
                                 static_cast<Real>(f.totalTimeSteps)
                           : 0.0;
    std::printf("%9.3f %10.5f %10.5f %+9.4f%% %11.3e %13llu %10.2f %9.0f%s\n", frac,
                f.flameSpeed * 100.0, f.consumptionSpeed * 100.0, mismatch * 100.0,
                f.fuelBalanceResidual,
                static_cast<unsigned long long>(f.chemistrySubSteps), ratio, wall,
                (f.status == PremixedFlameStatus::Converged) ? "" : "  NOT Converged");
    std::fflush(stdout);
    mismatches.push_back(mismatch);
    perStep.push_back(ratio);
  }

  // ---------------------------------------------------------------- verdict
  std::printf("\n=== VERDICT\n");
  std::printf("    sub-steps per time step: ");
  for (const Real r : perStep) {
    std::printf(" %.2f", r);
  }
  std::printf("\n    mismatch:                ");
  for (const Real m : mismatches) {
    std::printf(" %+.4f%%", m * 100.0);
  }
  std::printf("\n\n");

  const bool limiterIdle = (perStep.front() < 1.5);
  if (limiterIdle) {
    std::printf("    => THE LIMITER NEVER BINDS: %.2f sub-steps per march step at the default\n",
                perStep.front());
    std::printf("       fraction, i.e. essentially one. The chemistry is NOT being sub-stepped, so\n");
    std::printf("       `chemistryFractionalChange` cannot be causing the reaction excess.\n");
    std::printf("       THE HYPOTHESIS IS REFUTED, independently of the sweep below it.\n");
  }

  bool shrinking = true;
  for (std::size_t i = 1; i < mismatches.size(); ++i) {
    if (std::abs(mismatches[i]) >= std::abs(mismatches[i - 1])) {
      shrinking = false;
    }
  }
  const Real spread = std::abs(mismatches.back() - mismatches.front());

  if (spread < 0.002) {
    std::printf("    => THE MISMATCH IS FLAT across a 10x range of the fraction (spread %.4f %%).\n",
                spread * 100.0);
    std::printf("       The limiter is CLEARED. The 7.892 %% lies elsewhere -- the operator split\n");
    std::printf("       itself, or the recomputation of the rates on the final state.\n");
  } else if (shrinking) {
    std::printf("    => THE MISMATCH SHRINKS as the limiter tightens (%+.4f%% -> %+.4f%%).\n",
                mismatches.front() * 100.0, mismatches.back() * 100.0);
    std::printf("       The chemistry resolution IS implicated, and chemistryFractionalChange = 0.1\n");
    std::printf("       is a SECOND unvalidated resolution constant beside\n");
    std::printf("       maximumCellDiffusionNumber = 20.0.\n");
  } else {
    std::printf("    => THE MISMATCH GROWS as the limiter tightens (%+.4f%% -> %+.4f%%).\n",
                mismatches.front() * 100.0, mismatches.back() * 100.0);
    std::printf("       More accurate chemistry makes the two measures disagree MORE, which points\n");
    std::printf("       at the OPERATOR SPLIT rather than at either half of it.\n");
  }
  return 0;
}
