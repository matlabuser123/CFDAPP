// TD-004 GATE_A3 -- the amended C5 (C5a..C5d), with its thresholds DERIVED IN-PROBE.
//
// Every threshold below is computed from the quantity it bounds, not written as a literal:
//
//   C5a  |T_ad - T_peak|  <= (T_ad - T_unburnt) * kSumYBound
//        T_ad comes from the case constants, kSumYBound is C5's ALREADY-FROZEN 1.2e-06 band, and
//        (T_ad - T_u) is the gain the energy closure applies to a composition error. Nothing is
//        taken from the measured deficit.
//   C5b  |min Y_i|        <= kLinearAcceptance * kFuelFraction
//        kLinearAcceptance is the 1e-9 acceptance fraction GATE.md §3.2 records worstLinearResidual
//        as pinned at; kFuelFraction is the scale the excursion is relative to.
//   C5c  |x_front - x_a|  <= (deadBand + 1) * dx
//        deadBand is the ACTUAL setting in force, so the bound scales with it -- at deadBand 1 it
//        is 0.04 mm, not 0.08 mm. A fixed bound would be vacuously loose for the M2 control.
//   C5d  |sum Y - 1|      <= kSumYBound                 carried over from C5 UNCHANGED.
//
// Absolute values are deliberate in C5a and C5b: an overshoot is as wrong as an undershoot, and
// both measured values happen to be one-signed -- which is exactly when a one-sided test looks
// adequate and is not.
//
// USAGE: c5_amended_probe [deadBandCells] [label]
//   defaults 3.0 and "reference". The harness drives M1/M2/M3 through these.
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
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
constexpr Index kCells = 500;

// the two constants the derivations stand on, both quoted from the frozen record
constexpr Real kSumYBound = 1.2e-06;        // C5's frozen band, GATE.md §3.2
constexpr Real kLinearAcceptance = 1.0e-09;  // worstLinearResidual's pinned acceptance fraction

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

int main(int argc, char** argv) {
  const Real deadBand = (argc > 1) ? std::atof(argv[1]) : 3.0;
  const std::string label = (argc > 2) ? argv[2] : "reference";

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
  const Real xAnchor = kAnchorFraction * kDomain;
  const Real tAdiabatic =
      kUnburntT + kFuelFraction * (-kMolarFormationP / kWeight) / kSpecificHeat;

  // ------------------------------------------------------------------ the thresholds, DERIVED here
  const Real boundTpeak = (tAdiabatic - kUnburntT) * kSumYBound;
  const Real boundMinY = kLinearAcceptance * kFuelFraction;
  const Real boundFront = (deadBand + 1.0) * dx;

  std::printf("=== GATE_A3  C5a..C5d   label=%s  deadBand=%.1f\n\n", label.c_str(), deadBand);
  std::printf("  T_ad from the case constants   %.15f K  (bitwise 1800.0? %s)\n", tAdiabatic,
              (tAdiabatic == 1800.0) ? "YES" : "NO");
  std::printf("  THRESHOLDS, each computed from its derivation:\n");
  std::printf("    C5a  (T_ad - T_u) * %.1e        = %.6e K\n", kSumYBound, boundTpeak);
  std::printf("    C5b  %.1e * Y_F,u              = %.6e\n", kLinearAcceptance, boundMinY);
  std::printf("    C5c  (deadBand + 1) * dx        = %.6e m = %.4f mm\n", boundFront,
              boundFront * 1000.0);
  std::printf("    C5d  the frozen band            = %.6e\n\n", kSumYBound);
  std::fflush(stdout);

  PremixedFlameSettings s;
  s.cells = kCells;
  s.domainLength = kDomain;
  s.anchorFraction = kAnchorFraction;
  s.tolerance = 1.0e-3;
  s.maxOuterIterations = 40;
  s.driftSettleTolerance = 0.05;
  s.driftWindow = 32;
  s.finalMarchRelaxationTimes = 10.0;
  s.maximumStepsPerTrial = 80000;
  s.recentreDeadBandCells = deadBand;

  // A CONTROL THAT THROWS HAS NOT EXERCISED THE CRITERION -- it crashed. That is a different
  // outcome from FAIL and must not be read as one: CLAUDE.md's comparison rule is that identical
  // crashes are not "identical". So the exception is caught, labelled, and reported as EXCEPTION
  // rather than being allowed to look like a failing clause.
  const auto t0 = std::chrono::steady_clock::now();
  PremixedFlameResult f;
  try {
    f = solvePremixedFlame(chemistry, conditions, s);
  } catch (const std::exception& e) {
    std::printf("=== EXCEPTION -- the solve threw; NO clause was evaluated\n");
    std::printf("  what(): %s\n", e.what());
    std::printf("\n  MACHINE-READABLE: label=%s C5a=EXCEPTION C5b=EXCEPTION C5c=EXCEPTION "
                "C5d=EXCEPTION\n",
                label.c_str());
    return 0;
  }
  const Real wall = std::chrono::duration<Real>(std::chrono::steady_clock::now() - t0).count();

  const Real devT = std::abs(tAdiabatic - f.peakTemperature);
  const Real devY = std::abs(f.minMassFraction);
  const Real devX = std::abs(f.flamePosition - xAnchor);

  std::printf("=== MEASURED\n");
  std::printf("  finalMarchSteps            %lld\n", static_cast<long long>(f.finalMarchSteps));
  std::printf("  peakTemperature            %.15f K\n", f.peakTemperature);
  std::printf("  minMassFraction            %.15e\n", f.minMassFraction);
  std::printf("  flamePosition              %.12f mm  (%.3f cells off anchor)\n",
              f.flamePosition * 1000.0, (f.flamePosition - xAnchor) / dx);
  std::printf("  maxCompositionSumDeviation %.15e\n", f.maxCompositionSumDeviation);
  std::printf("  flameSpeed                 %.9f cm/s\n", f.flameSpeed * 100.0);
  std::printf("  fuelBalanceResidual        %.9e\n", f.fuelBalanceResidual);
  std::printf("  recentrings                %lld (final march %lld)\n",
              static_cast<long long>(f.recentrings),
              static_cast<long long>(f.finalMarchRecentrings));
  std::printf("  wall                       %.0f s\n\n", wall);

  const bool c5a = (devT <= boundTpeak);
  const bool c5b = (devY <= boundMinY);
  const bool c5c = (devX <= boundFront);
  const bool c5d = (f.maxCompositionSumDeviation <= kSumYBound);

  std::printf("=== VERDICTS  (each clause reported; the gate's stop rule is applied by the harness)\n");
  std::printf("  C5a  |T_ad - T_peak|  %.6e <= %.6e K     %s\n", devT, boundTpeak,
              c5a ? "PASS" : "FAIL");
  std::printf("  C5b  |min Y_i|        %.6e <= %.6e       %s\n", devY, boundMinY,
              c5b ? "PASS" : "FAIL");
  std::printf("  C5c  |x_front - x_a|  %.6e <= %.6e m     %s\n", devX, boundFront,
              c5c ? "PASS" : "FAIL");
  std::printf("  C5d  |sum Y - 1|      %.6e <= %.6e       %s\n", f.maxCompositionSumDeviation,
              kSumYBound, c5d ? "PASS" : "FAIL");
  std::printf("\n  MACHINE-READABLE: label=%s C5a=%s C5b=%s C5c=%s C5d=%s\n", label.c_str(),
              c5a ? "PASS" : "FAIL", c5b ? "PASS" : "FAIL", c5c ? "PASS" : "FAIL",
              c5d ? "PASS" : "FAIL");
  return 0;
}
