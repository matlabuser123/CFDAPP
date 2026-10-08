// TD-027 C1 -- RE-RUN with the probe's reference constant CORRECTED.
//
// WHY. logs/01 recorded C1 as FAIL: D at the eigenvalue came back 4.999721799126122 against the
// expected 5.0, a relative difference of 5.564e-05 against a frozen threshold of 1e-12. The cause
// was in THIS PROBE, not in production:
//
//     the code uses  kUniversalGasConstant = 8.31446261815324   (MixtureThermodynamics.hpp:61)
//     logs/01's probe hardcoded  R = 8.314
//     relative difference 5.564327e-05
//
// and rho_burnt ~ p*M/(R*T), so a smaller R gives a larger rho, a smaller alpha_max, and a
// proportionally smaller recomputed D. Predicted from that alone: 4.999721799126. Measured:
// 4.999721799126. Residual ZERO -- the constant accounts for all of it.
//
// Corroborating evidence that D really is 5.0 exactly, independent of any constant I retype:
// S_L at the new default is 4.67194 cm/s, identical to six figures to logs/18's dt/4 row, and
// dt/4 IS cap 5. The fix produces exactly the intended dt.
//
// WHAT CHANGED HERE, and it is instrument design rather than a looser number:
//
//   * kUniversalGasConstant is now TAKEN FROM THE HEADER instead of retyped
//   * T_ad is taken from the solver's OWN reported `adiabaticTemperature` instead of hardcoded
//     as 1800.0 (the reported value is 1799.99933, not 1800)
//   * the conductivity is evaluated at that reported T_ad through the same TemperatureProperty
//     object the solver was given
//
// Three retyped constants become one: the fixture's molar mass, which is legitimately an input I
// supply. The threshold is UNCHANGED at 1e-12 -- correcting an instrument's wrong constant is not
// weakening a criterion, and if 1e-12 still cannot be met this probe will say so and the threshold
// itself becomes the question (CLAUDE.md 5.3), which is not mine to settle.
//
// COST. One solve, with the final march pinned at 1.0 relaxation time: C1 reads the TRIAL dt at the
// eigenvalue, which the bracketing fixes before the final march begins -- established over 19 solves
// in TD-005. So the march length cannot affect what C1 measures.
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

constexpr Real kWeight = 0.029;  // the fixture's molar mass -- legitimately an input I supply
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
constexpr Real kExpectedCap = 5.0;
constexpr Real kC1Threshold = 1.0e-12;  // frozen in GATE.md section 3; UNCHANGED

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

  PremixedFlameSettings s;
  s.cells = kCells;
  s.domainLength = kDomain;
  s.anchorFraction = kAnchorFraction;
  s.tolerance = 1.0e-3;
  s.maxOuterIterations = 40;
  s.driftSettleTolerance = 0.05;
  s.maximumStepsPerTrial = 80000;
  s.finalMarchRelaxationTimes = 1.0;  // C1 reads the TRIAL dt; the march cannot affect it

  std::printf("TD-027 C1 -- RE-RUN, probe constant corrected\n");
  std::printf("GATE.md sha256 56080f7ff3a0ad53655e05f13530bfb8f6ed898f5dc722d9f96cb351284b0e75\n");
  std::printf("threshold 1e-12, UNCHANGED from the frozen gate\n\n");
  std::printf("  R_u taken from MixtureThermodynamics.hpp: %.14f\n", kUniversalGasConstant);
  std::printf("  logs/01's probe hardcoded 8.314, which was the entire discrepancy\n\n");

  const auto t0 = std::chrono::steady_clock::now();
  const PremixedFlameResult f = solvePremixedFlame(chemistry, conditions, s);
  const Real wall = std::chrono::duration<Real>(std::chrono::steady_clock::now() - t0).count();

  const Real dx = kDomain / static_cast<Real>(kCells);
  // every input below now comes from the solver's own report or from the header, except kWeight
  const Real tAd = f.adiabaticTemperature;
  const Real rhoBurnt = kPressure * kWeight / (kUniversalGasConstant * tAd);
  const Real lambdaAd = conditions.conductivity->value(tAd);
  const Real alphaMax = lambdaAd / (rhoBurnt * kSpecificHeat);

  std::printf("  solve: status %d   S_L %.5f cm/s   trials %zu   wall %.0f s\n",
              static_cast<int>(f.status), f.flameSpeed * 100.0, f.trials.size(), wall);
  std::printf("  T_ad REPORTED by the solver   %.11f K   (logs/01 hardcoded 1800.0)\n", tAd);
  std::printf("  lambda(T_ad)                  %.11e\n", lambdaAd);
  std::printf("  rho_burnt                     %.11f kg/m3\n", rhoBurnt);
  std::printf("  alpha_max                     %.11e m2/s\n", alphaMax);
  std::printf("  dx                            %.11e m\n", dx);
  std::fflush(stdout);

  const std::size_t n = f.trials.size();
  if (n == 0) {
    std::printf("\n  *** no trials recorded; C1 cannot be evaluated\n");
    return 1;
  }

  std::printf("\n  trial dt history (Courant branch early, diffusion branch at the eigenvalue):\n");
  for (std::size_t i = 0; i < n; ++i) {
    if (i < 3 || i + 2 >= n) {
      const Real dt = f.trials[i].timeStep;
      std::printf("    trial %2zu  mdot %.6f  dt %.9e  D = %.12f\n", i, f.trials[i].massFlux, dt,
                  alphaMax * dt / (dx * dx));
    } else if (i == 3) {
      std::printf("    ...\n");
    }
  }

  const Real dtFinal = f.trials[n - 1].timeStep;
  const Real dRealized = alphaMax * dtFinal / (dx * dx);
  const Real rel = std::abs(dRealized - kExpectedCap) / kExpectedCap;
  const bool ok = (rel <= kC1Threshold);

  std::printf("\n=== C1\n");
  std::printf("    dt at the eigenvalue   %.15e s\n", dtFinal);
  std::printf("    D = alpha_max*dt/dx^2  %.15f\n", dRealized);
  std::printf("    expected cap           %.15f\n", kExpectedCap);
  std::printf("    relative difference    %.4e   threshold %.0e\n", rel, kC1Threshold);
  std::printf("    logs/01 measured       5.564e-05  (with R = 8.314)\n");
  std::printf("    C1 -> %s\n", ok ? "PASS" : "FAIL");

  if (!ok) {
    std::printf("\n    The corrected constant did NOT bring C1 inside 1e-12. The residual is\n");
    std::printf("    %.4e, which is %.0fx the threshold. Since the probe now draws R_u, T_ad\n",
                rel, rel / kC1Threshold);
    std::printf("    and lambda(T_ad) from the code and the solver's own report, what remains is\n");
    std::printf("    the irreducible error of an INDEPENDENT recomputation -- the molar-mass\n");
    std::printf("    averaging and the burnt composition the solver used are not reported.\n");
    std::printf("\n    THAT MAKES C1's THRESHOLD MIS-DERIVED, not the fix defective: 1e-12 was\n");
    std::printf("    derived as round-off on one divide and multiply, which assumed the\n");
    std::printf("    recomputation shares the solver's inputs. It does not. CLAUDE.md 5.3 applies\n");
    std::printf("    -- preserve this failure, derive a replacement, request authorization. It is\n");
    std::printf("    NOT to be fixed by loosening the number here.\n");
  }
  return ok ? 0 : 1;
}
