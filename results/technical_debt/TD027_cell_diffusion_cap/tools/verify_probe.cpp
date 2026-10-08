// TD-027 -- VERIFICATION of C1 to C4, fresh, against the frozen gate.
//
// GATE.md sha256 56080f7ff3a0ad53655e05f13530bfb8f6ed898f5dc722d9f96cb351284b0e75
//
//   C1  the cap IS the realized D         alpha_max*dt/dx^2 == cap to 1e-12 relative
//   C2  S_L within the temporal budget    |S_L - 4.67589|/4.67589 <= 0.28 %   (G8's band / 10)
//   C3  reported precision coherent       C2's error / (sqrt(1+tol)-1) <= 2
//   C4  the limit cycle is suppressed     (max-min) <= mean over relaxT {18,22,26,30}
//
// C5 (regression counts) and C6 (the per-test cap) are measured by ctest runs, not by this probe.
//
// C1's INSTRUMENT. `PremixedFlameResult::trials` carries each trial's own `timeStep`, so the dt the
// solver actually marched with is readable without instrumenting production. The LAST trial's dt is
// the one at the eigenvalue, which is where the diffusion branch binds; the early trials run on the
// Courant branch while mdot is still near the Zel'dovich seed, and those are reported too so the
// crossover is visible rather than assumed.
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
constexpr Real kR = 8.314;

// frozen in GATE.md section 2, before any criterion used it
constexpr Real kConvergedSL = 4.67589;   // cm/s, Richardson over {dt/2, dt/4, dt/8}
constexpr Real kBudget = 0.28;           // %, G8's +-2.80 band / 10  [labelled empirical]
constexpr Real kTolerance = 1.0e-3;      // the bracket tolerance the fixture uses
constexpr Real kCoherence = 2.0;         // C3's factor  [labelled empirical]
constexpr Real kExpectedCap = 5.0;       // the new default under test

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

PremixedFlameSettings fixture() {
  PremixedFlameSettings s;  // GATE.md's fixture; maximumCellDiffusionNumber left at its DEFAULT
  s.cells = kCells;
  s.domainLength = kDomain;
  s.anchorFraction = kAnchorFraction;
  s.tolerance = kTolerance;
  s.maxOuterIterations = 40;
  s.driftSettleTolerance = 0.05;
  s.maximumStepsPerTrial = 80000;  // scaled with 1/dt; a safety cap, not an accuracy tolerance
  return s;
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
  const Real tAd = 1800.0;  // reported below and cross-checked
  const Real rhoBurnt = kPressure * kWeight / (kR * tAd);
  const Real alphaMax = kConductivity / (rhoBurnt * kSpecificHeat);

  std::printf("TD-027 -- VERIFICATION, C1 to C4\n");
  std::printf("GATE.md sha256 56080f7ff3a0ad53655e05f13530bfb8f6ed898f5dc722d9f96cb351284b0e75\n\n");
  std::printf("  %lld cells over %.0f mm, dx = %.1f um\n", static_cast<long long>(kCells),
              kDomain * 1000.0, dx * 1e6);
  std::printf("  alpha_max (burnt) = %.4e m2/s   rho_burnt = %.5f kg/m3\n", alphaMax, rhoBurnt);
  std::printf("  converged reference S_L = %.5f cm/s (GATE.md section 2)\n", kConvergedSL);
  std::printf("  maximumCellDiffusionNumber is left at its DEFAULT -- the change under test\n\n");

  int failures = 0;

  // ============================================================ C1, C2, C3 from one solve
  const PremixedFlameResult f = solvePremixedFlame(chemistry, conditions, fixture());
  std::printf("=== the reference solve\n");
  std::printf("    status %d   S_L %.5f cm/s   T_ad %.2f K   bracket %.4e   trials %zu\n",
              static_cast<int>(f.status), f.flameSpeed * 100.0, f.adiabaticTemperature,
              f.massFluxResidual, f.trials.size());
  std::fflush(stdout);

  // ---- C1
  std::printf("\n=== C1  the cap IS the realized cell diffusion number\n");
  std::printf("    trial dt history (the Courant branch binds early, the diffusion branch at the\n");
  std::printf("    eigenvalue -- the crossover is shown, not assumed):\n");
  const std::size_t n = f.trials.size();
  for (std::size_t i = 0; i < n; ++i) {
    if (i < 3 || i + 3 >= n) {
      const Real dt = f.trials[i].timeStep;
      std::printf("      trial %2zu  mdot %.6f  dt %.6e  D = %.6f\n", i, f.trials[i].massFlux, dt,
                  alphaMax * dt / (dx * dx));
    } else if (i == 3) {
      std::printf("      ...\n");
    }
  }
  const Real dtFinal = (n > 0) ? f.trials[n - 1].timeStep : 0.0;
  const Real dRealized = alphaMax * dtFinal / (dx * dx);
  const Real c1 = std::abs(dRealized - kExpectedCap) / kExpectedCap;
  const bool c1ok = (c1 <= 1.0e-12);
  std::printf("    D at the eigenvalue = %.15f   expected cap %.1f   rel diff %.3e\n", dRealized,
              kExpectedCap, c1);
  std::printf("    C1 -> %s\n", c1ok ? "PASS" : "*** FAIL ***");
  if (!c1ok) {
    ++failures;
    std::printf("    (if D equals 20 the fix did not take; if it is neither, the Courant branch\n");
    std::printf("     binds at the eigenvalue and the audit's section 1 reading is wrong)\n");
  }
  std::fflush(stdout);

  // ---- C2
  const Real slNew = f.flameSpeed * 100.0;
  const Real err = std::abs(slNew - kConvergedSL) / kConvergedSL * 100.0;
  const bool c2ok = (err <= kBudget);
  std::printf("\n=== C2  S_L within the temporal-error budget\n");
  std::printf("    S_L %.5f cm/s   reference %.5f   error %.4f %%   budget %.2f %%\n", slNew,
              kConvergedSL, err, kBudget);
  std::printf("    for scale: the OLD default gave 4.86230, i.e. +3.99 %%, against G8's +-2.80 %%\n");
  std::printf("    C2 -> %s\n", c2ok ? "PASS" : "*** FAIL ***");
  if (!c2ok) {
    ++failures;
  }
  std::fflush(stdout);

  // ---- C3
  const Real bracketPrecision = (std::sqrt(1.0 + kTolerance) - 1.0) * 100.0;
  const Real ratio = (bracketPrecision != 0.0) ? err / bracketPrecision : -1.0;
  const bool c3ok = (ratio <= kCoherence);
  std::printf("\n=== C3  the reported precision is coherent with the discretisation\n");
  std::printf("    reported bracket precision sqrt(1+%.0e)-1 = %.4f %%\n", kTolerance,
              bracketPrecision);
  std::printf("    temporal error %.4f %%   ratio %.3f   limit %.1f\n", err, ratio, kCoherence);
  std::printf("    for scale: cap 20 gave 80x, cap 10 gives 8.4x\n");
  std::printf("    C3 -> %s\n", c3ok ? "PASS" : "*** FAIL ***");
  if (!c3ok) {
    ++failures;
  }
  std::fflush(stdout);

  // ============================================================ C4
  std::printf("\n=== C4  the limit cycle is suppressed: the march converges\n");
  std::printf("    relaxT {18, 22, 26, 30}; the family starts at 18 because relaxT 10 and 14 were\n");
  std::printf("    measured as a startup transient at this resolution (GATE.md section 4.4)\n\n");
  std::printf("%10s %11s %12s %10s %10s %9s\n", "relaxT", "marchStp", "fuel bal", "delta mm",
              "S_L cm/s", "wall s");
  std::fflush(stdout);
  std::vector<Real> balances;
  for (const Real t : {18.0, 22.0, 26.0, 30.0}) {
    PremixedFlameSettings s = fixture();
    s.finalMarchRelaxationTimes = t;
    const auto t0 = std::chrono::steady_clock::now();
    const PremixedFlameResult g = solvePremixedFlame(chemistry, conditions, s);
    const Real wall = std::chrono::duration<Real>(std::chrono::steady_clock::now() - t0).count();
    std::printf("%10.0f %11lld %12.4e %10.4f %10.5f %9.0f\n", t,
                static_cast<long long>(g.finalMarchSteps), g.fuelBalanceResidual,
                g.flameThickness * 1000.0, g.flameSpeed * 100.0, wall);
    std::fflush(stdout);
    balances.push_back(g.fuelBalanceResidual);
  }
  Real lo = balances.front();
  Real hi = balances.front();
  Real sum = 0.0;
  for (const Real b : balances) {
    lo = std::min(lo, b);
    hi = std::max(hi, b);
    sum += b;
  }
  const Real mean = sum / static_cast<Real>(balances.size());
  const bool c4ok = ((hi - lo) <= mean);
  std::printf("\n    max %.4e  min %.4e  mean %.4e   max-min %.4e %s mean\n", hi, lo, mean, hi - lo,
              (hi - lo) <= mean ? "<=" : ">");
  std::printf("    for scale: at cap 20 this family gave max-min 6.647e-01 against mean 2.208e-01\n");
  std::printf("    C4 -> %s\n", c4ok ? "PASS" : "*** FAIL ***");
  if (!c4ok) {
    ++failures;
  }

  std::printf("\n=== C1-C4 SUMMARY: %s\n", (failures == 0) ? "ALL PASS" : "FAILURES PRESENT");
  std::printf("    C5 (343/331/32) and C6 (the 7200 s per-test cap) are measured by ctest runs.\n");
  return failures == 0 ? 0 : 1;
}
