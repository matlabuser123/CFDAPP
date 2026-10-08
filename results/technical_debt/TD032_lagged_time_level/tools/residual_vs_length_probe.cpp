// TD-032 -- C3(b)'s BASELINE: does the per-step change FALL with march length on the lagged tree?
//
// WHY THIS EXISTS. C3's first draft bounded `temperatureResidual` at 1e-3 and justified the margin
// with the within-branch drift read off five sampled cells in TD031/logs/05 (2.47e-3 K). The
// dry-run (logs/01) showed the FULL-FIELD max two-step difference is 0.2646 K -- 107x larger -- so
// the claimed 600x margin is really about 5x, which is too thin to distinguish a correct fix from a
// marginal one. The bound was mis-derived and the gate is not yet frozen, so it is being re-derived
// here rather than carried into a freeze (CLAUDE.md 5.2: the dry-run exists to catch exactly this).
//
// THE REPLACEMENT CRITERION, and why it discriminates where a bare bound does not. A SATURATED
// quantity does not move: TD-031 measured `temperatureResidual` at 0.989 +- 0.0002 across six
// configurations spanning three meshes and two caps, a relative spread of 2e-4. A quantity that is
// actually measuring a converging march's per-step change must FALL as the march gets longer. So:
//
//     C3(b)   temperatureResidual(relaxT 26) <= 0.5 * temperatureResidual(relaxT 18)
//
// The factor 2 is 1000x outside the saturated spread of 2e-4, so it cannot be met by noise, and it
// asks for a DIRECTION rather than a level -- which is what makes it immune to the sampling error
// that broke the first draft.
//
// WHAT THIS RUN ESTABLISHES. On the LAGGED tree the ratio must be ~1.0 (no fall), which is C3(b)'s
// fail-direction non-vacuity. If the lagged tree already showed a 2x fall, C3(b) would be vacuous
// and must not be frozen.
//
// The exit code is NOT the verdict. Nonzero means the PROBE malfunctioned.
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
constexpr Index kMaxStepsPerTrial = 80000;
constexpr Real kFallFactor = 0.5;  // C3(b): the residual must at least halve

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

  std::printf("TD-032 -- C3(b) BASELINE: the per-step change against MARCH LENGTH\n\n");
  std::printf("  500 cells / cap 5, the gated fixture, with the step ceiling raised so the\n");
  std::printf("  DERIVED budget governs. A saturated quantity does not move; a converging\n");
  std::printf("  march's per-step change must FALL. C3(b) asks for a factor %.1f.\n\n",
              1.0 / kFallFactor);
  std::printf("%8s %10s %18s %16s %12s %9s\n", "relaxT", "marchStp", "T resid", "worstChange",
              "S_L cm/s", "wall s");
  std::fflush(stdout);

  std::vector<Real> residuals;
  for (const Real relaxT : {18.0, 26.0}) {
    PremixedFlameSettings s;
    s.cells = 500;
    s.domainLength = kDomain;
    s.anchorFraction = kAnchorFraction;
    s.tolerance = 1.0e-3;
    s.maxOuterIterations = 40;
    s.driftSettleTolerance = 0.05;
    s.driftWindow = 32;
    s.maximumCellDiffusionNumber = 5.0;
    s.finalMarchRelaxationTimes = relaxT;
    s.maximumStepsPerTrial = kMaxStepsPerTrial;
    s.finalMarchSteps = 1000000;  // raised: let the derived budget govern

    const auto t0 = std::chrono::steady_clock::now();
    const PremixedFlameResult f = solvePremixedFlame(chemistry, conditions, s);
    const Real wall = std::chrono::duration<Real>(std::chrono::steady_clock::now() - t0).count();

    if (f.status != PremixedFlameStatus::Converged || f.temperature.empty()) {
      std::printf("%8.0f   *** the solve did not converge -- PROBE/SOLVER FAILURE\n", relaxT);
      return 1;
    }
    std::printf("%8.0f %10lld %18.10e %16.6e %12.5f %9.0f\n", relaxT,
                static_cast<long long>(f.finalMarchSteps), f.temperatureResidual,
                f.worstStateChange, f.flameSpeed * 100.0, wall);
    std::fflush(stdout);
    residuals.push_back(f.temperatureResidual);
  }

  const Real ratio = (residuals[0] > 0.0) ? residuals[1] / residuals[0] : -1.0;
  std::printf("\n=== C3(b) ON THE LAGGED TREE\n");
  std::printf("    resid(26) / resid(18) = %.6f   against the required <= %.2f\n", ratio,
              kFallFactor);
  if (ratio > kFallFactor) {
    std::printf("    => NO FALL. The quantity is SATURATED, exactly as TD-031's six-configuration\n");
    std::printf("       invariance said. C3(b) FAILS here, which is its fail-direction\n");
    std::printf("       non-vacuity, and the criterion may be frozen.\n");
  } else {
    std::printf("    *** THE LAGGED TREE ALREADY HALVES THE RESIDUAL. C3(b) would be VACUOUS and\n");
    std::printf("        MUST NOT be frozen -- a criterion the defective tree already satisfies\n");
    std::printf("        discriminates nothing.\n");
  }
  return 0;
}
