// TD-031 -- FIX kMargin BY MEASUREMENT, not by choice.
//
// INVESTIGATION ONLY. No production file is changed. The instrument already exists:
// `result.temperatureResidual` and `result.compositionResidual` carry the achieved per-step change
// (PremixedFlame.cpp:1880-1881), so the ratio to the derived floor is directly measurable.
//
// THE FLOOR, from derivation.md section 2.1:
//
//     floor = eps * S_L * dt / delta        eps = sqrt(1 + massFluxResidual) - 1
//
// using the ACHIEVED bracket rather than the requested tolerance, because the achieved value is
// the actual uncertainty in mdot (6.771e-04 against a requested 1e-03, so eps = 3.385e-04).
//
// HOW kMargin IS FIXED, and why this is a measurement rather than a choice. kMargin must
//
//     ADMIT every CONVERGED state        kMargin >= max(ratio over converged states)
//     EXCLUDE every NON-CONVERGED state  kMargin <  min(ratio over non-converged states)
//
// so the data BRACKETS it. If max(converged) >= min(non-converged) the bracket is EMPTY and the
// per-step change does not discriminate at all -- in which case clause (a) cannot carry the
// criterion and (b) and (c) must, which is a finding about the design and not a value to pick.
//
// WHICH STATES ARE WHICH, fixed from logs/01 and logs/02 BEFORE this run:
//
//     500 cells / cap  5   converges at relaxT 18.  relaxT 18, 26 CONVERGED; relaxT 10 TRANSIENT
//     250 cells / cap  5   no convergence point.    ALL levels NON-CONVERGED
//     500 cells / cap 10   no convergence point.    ALL levels NON-CONVERGED
//
// That classification is NOT re-derived from this run's numbers. It comes from the convergence
// measurement already recorded, which is what keeps this from being circular: if the classification
// came from the same per-step changes being used to fix kMargin, any value would appear to work.
//
// A LIMITATION, stated up front. temperatureResidual is the LAST step's change, not the worst over
// the march. At a converged state the last step is representative; at a cycling state it depends on
// the phase the march stopped in, so the non-converged ratios are NOISY and should be read as a
// sample rather than a bound. That weakens the upper end of the bracket, and the honest consequence
// is that kMargin should be taken from the CONVERGED side with margin, not fitted to the gap.
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

struct Row {
  std::string label;
  bool converged{};  // classified from logs/01 and logs/02, NOT from this run
  Real ratio{};
  Real residual{};
  Real floor{};
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

  std::printf("TD-031 -- FIXING kMargin BY MEASUREMENT\n\n");
  std::printf("  floor = eps * S_L * dt / delta,  eps = sqrt(1 + massFluxResidual) - 1\n");
  std::printf("  CONVERGED / NON-CONVERGED is classified from logs/01 and logs/02, not from the\n");
  std::printf("  per-step changes measured here -- otherwise fixing kMargin would be circular.\n\n");
  std::printf("%-26s %7s %5s %12s %12s %10s %9s\n", "configuration", "relaxT", "conv",
              "residual", "floor", "RATIO", "wall s");
  std::fflush(stdout);

  struct Config {
    Index cells;
    Real cap;
    const char* name;
  };
  const Config configs[] = {{500, 5.0, "500 cells / cap  5"},
                            {250, 5.0, "250 cells / cap  5"},
                            {500, 10.0, "500 cells / cap 10"}};

  std::vector<Row> rows;
  for (const Config& cfg : configs) {
    for (const Real t : {10.0, 18.0, 26.0}) {
      PremixedFlameSettings s;
      s.cells = cfg.cells;
      s.domainLength = kDomain;
      s.anchorFraction = kAnchorFraction;
      s.tolerance = 1.0e-3;
      s.maxOuterIterations = 40;
      s.driftSettleTolerance = 0.05;
      s.driftWindow = 32;
      s.maximumCellDiffusionNumber = cfg.cap;
      s.finalMarchRelaxationTimes = t;
      s.maximumStepsPerTrial = static_cast<Index>(20000.0 * (20.0 / cfg.cap));

      const auto t0 = std::chrono::steady_clock::now();
      const PremixedFlameResult f = solvePremixedFlame(chemistry, conditions, s);
      const Real wall = std::chrono::duration<Real>(std::chrono::steady_clock::now() - t0).count();

      const std::size_t n = f.trials.size();
      const Real dt = (n > 0) ? f.trials[n - 1].timeStep : 0.0;
      const Real eps = std::sqrt(1.0 + f.massFluxResidual) - 1.0;
      const Real floor = (f.flameThickness > 0.0)
                             ? eps * f.flameSpeed * dt / f.flameThickness
                             : 0.0;
      const Real ratio = (floor > 0.0) ? f.temperatureResidual / floor : -1.0;

      // the classification, from the recorded convergence measurement
      const bool conv = (cfg.cells == 500 && cfg.cap == 5.0 && t >= 18.0);

      std::printf("%-26s %7.0f %5s %12.4e %12.4e %10.3f %9.0f\n", cfg.name, t,
                  conv ? "YES" : "no", f.temperatureResidual, floor, ratio, wall);
      std::fflush(stdout);
      rows.push_back(Row{std::string(cfg.name) + " relaxT " + std::to_string(static_cast<int>(t)),
                         conv, ratio, f.temperatureResidual, floor});
    }
  }

  // ---------------------------------------------------------------- bracket kMargin
  Real maxConv = -1.0;
  Real minNon = 1.0e300;
  std::string maxConvAt;
  std::string minNonAt;
  for (const Row& r : rows) {
    if (r.ratio < 0.0) {
      continue;
    }
    if (r.converged) {
      if (r.ratio > maxConv) {
        maxConv = r.ratio;
        maxConvAt = r.label;
      }
    } else {
      if (r.ratio < minNon) {
        minNon = r.ratio;
        minNonAt = r.label;
      }
    }
  }

  std::printf("\n=== BRACKETING kMargin\n");
  std::printf("    max ratio over CONVERGED states      %10.3f   (%s)\n", maxConv,
              maxConvAt.c_str());
  std::printf("    min ratio over NON-CONVERGED states  %10.3f   (%s)\n", minNon,
              minNonAt.c_str());
  std::printf("\n    kMargin must be >= %.3f to admit every converged state\n", maxConv);
  std::printf("    kMargin must be <  %.3f to exclude every non-converged state\n", minNon);

  if (maxConv >= minNon) {
    std::printf("\n    *** THE BRACKET IS EMPTY. The per-step change does NOT discriminate:\n");
    std::printf("        a non-converged state achieves a ratio at or below a converged one.\n");
    std::printf("        Clause (a) therefore cannot carry the criterion on its own, and (b)\n");
    std::printf("        (consecutive windows) and (c) (the front on anchor) must. That is a\n");
    std::printf("        finding about the DESIGN, not a value to choose -- and it means the\n");
    std::printf("        margin cannot be fixed by this measurement.\n");
  } else {
    std::printf("\n    bracket is NON-EMPTY: [%.3f, %.3f)\n", maxConv, minNon);
    std::printf("    Taking kMargin from the CONVERGED side with margin, as derivation.md 3.1\n");
    std::printf("    requires -- NOT fitted to the middle of the gap, because the\n");
    std::printf("    non-converged ratios are last-step samples and depend on the phase the\n");
    std::printf("    march stopped in, so the upper end of this bracket is not a bound.\n");
    std::printf("    PROPOSED  kMargin = %.0f   (2x the converged maximum, rounded up)\n",
                std::ceil(2.0 * maxConv));
  }
  return 0;
}
