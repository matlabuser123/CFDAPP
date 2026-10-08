// TD-031 -- LOCALISE the per-step state change, and prove the diagnostic changed nothing.
//
// TWO JOBS, in this order, because the second is worthless without the first.
//
// 1. BITWISE NEUTRALITY. The diagnostic added four read-only fields and changed the max-tracking
//    loop from `std::max(max, d)` to `if (d > max) { max = d; cell = c; }`. Neither form updates on
//    a tie, so `temperatureResidual` must be bitwise what it was. The pre-diagnostic values are
//    recorded below from logs/03 and TD-027 logs/01, and any difference means the "read-only"
//    claim is false and nothing else here can be trusted.
//
// 2. LOCALISATION. `temperatureResidual` reads 0.989 at a state that three independent quantities
//    agree is converged, and six configurations across three meshes and two caps return
//    0.989 +- 0.0002 -- a saturated quantity, not a measurement. THE MECHANISM IS NOT KNOWN, and
//    this probe does not assume one. The new fields say WHERE and WHEN:
//
//      worstStateChangeCell          0 or nc-1  -> the end fill a translation performs
//                                    ~ the front -> gradient x shift
//                                    elsewhere  -> neither, and the suspicion was wrong
//      worstStateChangeWasRecentred  separates the translation from the physics
//      worstStateChangeNoRecentre    the frame-following steadiness -- the quantity clause (a)
//                                    of derivation.md actually needs
//
// TWO LEVELS: relaxT 18, which three measurements agree is CONVERGED, and relaxT 10, which the
// same measurements agree is a TRANSIENT. A diagnostic that cannot tell those two apart is no
// better than the one it replaces.
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

  const Real xAnchor = kAnchorFraction * kDomain;
  const Real dx = kDomain / static_cast<Real>(kCells);

  std::printf("TD-031 -- LOCALISING the per-step state change\n\n");
  std::printf("  500 cells / cap 5. relaxT 18 is CONVERGED and relaxT 10 is a TRANSIENT, by three\n");
  std::printf("  independent measurements (front, balance, thickness) in logs/01, logs/02 and\n");
  std::printf("  TD-027 logs/01. Front region is cell ~%.0f; the ends are cell 0 and %lld.\n\n",
              xAnchor / dx, static_cast<long long>(kCells - 1));

  struct Reference {
    Real relaxT;
    const char* state;
    Real temperatureResidual;  // pre-diagnostic, from logs/03
  };
  const Reference refs[] = {{18.0, "CONVERGED", 9.8937e-01}, {10.0, "TRANSIENT", 3.5264e-01}};

  int failures = 0;
  for (const Reference& ref : refs) {
    PremixedFlameSettings s;
    s.cells = kCells;
    s.domainLength = kDomain;
    s.anchorFraction = kAnchorFraction;
    s.tolerance = 1.0e-3;
    s.maxOuterIterations = 40;
    s.driftSettleTolerance = 0.05;
    s.driftWindow = 32;
    s.maximumCellDiffusionNumber = 5.0;
    s.finalMarchRelaxationTimes = ref.relaxT;
    s.maximumStepsPerTrial = 80000;  // matches logs/03 exactly, so the comparison is like-for-like

    const auto t0 = std::chrono::steady_clock::now();
    const PremixedFlameResult f = solvePremixedFlame(chemistry, conditions, s);
    const Real wall = std::chrono::duration<Real>(std::chrono::steady_clock::now() - t0).count();

    std::printf("================= relaxT %.0f  (%s)   %.0f s\n", ref.relaxT, ref.state, wall);
    std::printf("  S_L %.5f cm/s   balance %.4e   delta %.4f mm   front %+.2f cells\n",
                f.flameSpeed * 100.0, f.fuelBalanceResidual, f.flameThickness * 1000.0,
                (f.flamePosition - xAnchor) / dx);

    std::printf("\n  BITWISE NEUTRALITY of the diagnostic\n");
    std::printf("    temperatureResidual  %.17e\n", f.temperatureResidual);
    std::printf("    pre-diagnostic       %.4e   (logs/03)\n", ref.temperatureResidual);
    const bool same = (std::abs(f.temperatureResidual - ref.temperatureResidual) <
                       5.0e-5 * ref.temperatureResidual);
    std::printf("    agrees to the recorded precision: %s\n", same ? "YES" : "*** NO ***");
    if (!same) {
      ++failures;
      std::printf("    *** THE DIAGNOSTIC IS NOT READ-ONLY. Stop: nothing below can be trusted.\n");
    }

    std::printf("\n  LOCALISATION\n");
    std::printf("    worstStateChange              %.6e   (max over EVERY judged step)\n",
                f.worstStateChange);
    std::printf("    worstStateChangeCell          %lld",
                static_cast<long long>(f.worstStateChangeCell));
    if (f.worstStateChangeCell == 0 || f.worstStateChangeCell + 1 >= kCells) {
      std::printf("   <- AN END CELL: implicates the fill a translation performs\n");
    } else if (std::abs(static_cast<Real>(f.worstStateChangeCell) - xAnchor / dx) < 80.0) {
      std::printf("   <- the FRONT region: implicates gradient x shift\n");
    } else {
      std::printf("   <- NEITHER an end nor the front: both suspicions were wrong\n");
    }
    std::printf("    worstStateChangeWasRecentred  %s\n",
                f.worstStateChangeWasRecentred ? "YES -- a translation step" : "no");
    std::printf("    worstStateChangeNoRecentre    %.6e   <- the FRAME-FOLLOWING steadiness\n",
                f.worstStateChangeNoRecentre);
    std::printf("    finalMarchRecentrings         %lld of %lld steps\n",
                static_cast<long long>(f.finalMarchRecentrings),
                static_cast<long long>(f.finalMarchSteps));
    std::printf("\n");
    std::fflush(stdout);
  }

  std::printf("=== READING\n");
  std::printf("  If worstStateChangeNoRecentre is ORDERS below worstStateChange, the saturation is\n");
  std::printf("  the frame translation and clause (a) should use the no-recentre quantity.\n");
  std::printf("  If the two are COMPARABLE, the translation is not the cause and the saturation is\n");
  std::printf("  in the physics or in the step itself -- which would need its own investigation\n");
  std::printf("  before any convergence criterion can rest on a per-step change at all.\n");
  std::printf("  And if the CONVERGED and TRANSIENT levels do not separate on the no-recentre\n");
  std::printf("  quantity either, clause (a) cannot be made to work and (b)+(c) must carry it.\n");
  return failures == 0 ? 0 : 1;
}
