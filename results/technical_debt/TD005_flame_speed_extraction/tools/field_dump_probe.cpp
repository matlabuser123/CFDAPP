// TD-005 -- LOOK AT THE FIELD. The first instrument in this chain that is not derived from it.
//
// INVESTIGATION ONLY. No production file is changed.
//
// WHY THIS PROBE, AND WHY NOT A FOURTH HYPOTHESIS.
//
// `logs/01` established that the solver lands in one of exactly TWO final states -- `delta ~ 1.229 mm`
// (admissible, fuel balance ~0.01) or `delta ~ 0.73-0.85 mm` (fuel balance 0.24-0.37) -- and refuted
// three explanations for which one it picks:
//
//   (i)   insufficient relaxation      refuted TWICE: driftWindow 64 is the LONGEST trial and bad;
//                                      relaxationTimesPerTrial 6.0 is the MOST relaxed and bad
//   (ii)  inlet proximity              explains anchor 0.30 only -- windows 16 and 64 sit at the
//                                      SAME anchor as the good window 32
//   (iii) the bracket width            refuted over a 100x range: sweep D never produced a bad mode
//
// Every one of those was inferred from counters DERIVED from the temperature profile -- jump
// magnitudes, balances, thicknesses -- when the thing being inferred IS the profile. TD-004 burned
// five hypotheses that way, and its own summary section 7.4 named the remedy and never applied it:
// dump the field and look at it. So this probe offers no hypothesis. It prints the two fields side
// by side.
//
// THE TWO NUMBERS THAT DISCRIMINATE, stated before the run.
//
// The bad mode has S_c = 3.04 against S_L = 4.86, i.e. the consumption integral accounts for only
// ~62 % of the fuel that mdot delivers. The missing ~38 % matches the fuel-balance residual (0.3647),
// so roughly 38 % of the fuel is unaccounted for. There are only three places it can go:
//
//   OUTLET BREAKTHROUGH   Y_F at the outlet is non-zero: fuel is leaving unburnt because the flame
//                         is not consuming it. Diagnostic: Y_F[nc-1] / Y_F,u.
//   INLET FLASHBACK       T at the inlet is well above T_u: the front has reached the boundary and
//                         the Dirichlet condition is destroying enthalpy and fuel there.
//                         Diagnostic: (T[0] - T_u)/(T_ad - T_u).
//   RECENTRING DISCARD    the frame translation threw it off an end. TD-004 instrumented this and
//                         its ledger is reported here for completeness, but TD-004's logs/09 already
//                         showed the ledger does NOT scale with the imbalance.
//
// These are independent and mutually exclusive enough that the pair of numbers decides it. That is
// the whole point: the answer comes from the field, not from another counter about the field.
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

PremixedFlameSettings baseSettings() {
  PremixedFlameSettings settings;
  settings.cells = 500;
  settings.domainLength = 0.010;
  settings.anchorFraction = 0.35;
  settings.tolerance = 1.0e-3;
  settings.maxOuterIterations = 40;
  settings.driftSettleTolerance = 0.05;
  settings.maximumStepsPerTrial = 20000;
  return settings;
}

void report(const char* label, const PremixedFlameResult& f, const PremixedFlameConditions& cond) {
  const Index nc = static_cast<Index>(f.temperature.size());
  const Real tAd = f.adiabaticTemperature;
  const Real rise = tAd - kUnburntT;
  const std::vector<Real>& yF = f.massFractions[0];

  std::printf("\n================================================================ %s\n", label);
  std::printf("  status %d   S_L %.5f cm/s   S_c %.5f cm/s   fuel balance %.4e   delta %.4f mm\n",
              static_cast<int>(f.status), f.flameSpeed * 100.0, f.consumptionSpeed * 100.0,
              f.fuelBalanceResidual, f.flameThickness * 1000.0);

  // FULL PRECISION on the quantities the C4d fix must leave BITWISE UNCHANGED. The first version
  // of this probe printed the balances at %.4e, which cannot support a bitwise claim -- a
  // five-figure agreement would have been reported as "bitwise" on no evidence.
  std::printf("\n  BITWISE-COMPANION QUANTITIES (the fix is read-only on the state)\n");
  std::printf("      flameSpeed            %.17e\n", f.flameSpeed);
  std::printf("      massFlux              %.17e\n", f.massFlux);
  std::printf("      consumptionSpeed      %.17e\n", f.consumptionSpeed);
  std::printf("      fuelBalanceResidual   %.17e\n", f.fuelBalanceResidual);
  std::printf("      energyBalanceResidual %.17e\n", f.energyBalanceResidual);
  std::printf("      massFluxResidual      %.17e\n", f.massFluxResidual);
  std::printf("      peakTemperature       %.17e\n", f.peakTemperature);
  std::printf("      flamePosition         %.17e   <- THIS one MUST change\n",
              f.flamePosition);

  // ---- C1, which logs/01 could not evaluate because it printed flameSpeed alone.
  const Real recomputed = f.massFlux / f.unburntDensity;
  std::printf("\n  C1  flameSpeed          %.17e\n", f.flameSpeed);
  std::printf("      massFlux/rho_u      %.17e\n", recomputed);
  std::printf("      massFlux %.17e   unburntDensity %.17e\n", f.massFlux, f.unburntDensity);
  std::printf("      BITWISE EQUAL: %s\n", (f.flameSpeed == recomputed) ? "YES" : "*** NO ***");

  // ---- the two discriminating numbers, as stated in the header note.
  const Real inletRise = (f.temperature[0] - kUnburntT) / rise;
  const Real outletFuel = yF[static_cast<std::size_t>(nc - 1)] / kFuelFraction;
  std::printf("\n  DISCRIMINATORS\n");
  std::printf("      inlet  (T[0]-T_u)/(T_ad-T_u)   %12.6e   <- FLASHBACK if order 1\n",
              inletRise);
  std::printf("      outlet Y_F[nc-1]/Y_F,u        %12.6e   <- BREAKTHROUGH if order 1\n",
              outletFuel);
  std::printf("      recentring fuel ledger        %12.6e kg/m2  (TD-004's, for completeness)\n",
              f.recentringFuelLedger);
  std::printf("      T_peak %.2f K   T_ad %.2f K   x_front %.5f mm\n", f.peakTemperature, tAd,
              f.flamePosition * 1000.0);

  // ---- where the fuel actually is, in five equal blocks, so a leak has a location.
  std::printf("\n  FUEL INVENTORY by fifth of the domain (fraction of the total Y_F present)\n");
  Real total = 0.0;
  for (Index c = 0; c < nc; ++c) {
    total += yF[static_cast<std::size_t>(c)] * f.density[static_cast<std::size_t>(c)];
  }
  for (int b = 0; b < 5; ++b) {
    Real part = 0.0;
    const Index lo = static_cast<Index>(b) * nc / 5;
    const Index hi = (static_cast<Index>(b) + 1) * nc / 5;
    for (Index c = lo; c < hi; ++c) {
      part += yF[static_cast<std::size_t>(c)] * f.density[static_cast<std::size_t>(c)];
    }
    std::printf("      cells %3lld-%3lld  x %5.2f-%5.2f mm   %8.5f\n", static_cast<long long>(lo),
                static_cast<long long>(hi - 1), f.x[static_cast<std::size_t>(lo)] * 1000.0,
                f.x[static_cast<std::size_t>(hi - 1)] * 1000.0,
                (total != 0.0) ? part / total : 0.0);
  }

  // ---- the profile itself, every 20th cell plus both boundaries.
  std::printf("\n  PROFILE (every 20th cell)\n");
  std::printf("      %5s %9s %10s %12s %11s %13s\n", "cell", "x mm", "T K", "Y_F", "u m/s",
              "S_E W/m3");
  for (Index c = 0; c < nc; c += 20) {
    std::printf("      %5lld %9.4f %10.2f %12.6e %11.5f %13.4e\n", static_cast<long long>(c),
                f.x[static_cast<std::size_t>(c)] * 1000.0, f.temperature[static_cast<std::size_t>(c)],
                yF[static_cast<std::size_t>(c)], f.velocity[static_cast<std::size_t>(c)],
                f.heatRelease[static_cast<std::size_t>(c)]);
  }
  const Index last = nc - 1;
  std::printf("      %5lld %9.4f %10.2f %12.6e %11.5f %13.4e   <- outlet\n",
              static_cast<long long>(last), f.x[static_cast<std::size_t>(last)] * 1000.0,
              f.temperature[static_cast<std::size_t>(last)], yF[static_cast<std::size_t>(last)],
              f.velocity[static_cast<std::size_t>(last)],
              f.heatRelease[static_cast<std::size_t>(last)]);
  std::fflush(stdout);
  (void)cond;
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

  std::printf("TD-005 -- the two final states, side by side, as FIELDS\n");
  std::printf("both runs differ ONLY in driftWindow; everything else is identical\n");
  std::printf("  window 32 -> logs/01 measured fuel balance 1.531e-02, delta 1.2289 mm  (GOOD)\n");
  std::printf("  window 16 -> logs/01 measured fuel balance 3.647e-01, delta 0.7365 mm  (BAD)\n");
  std::fflush(stdout);

  {
    auto s = baseSettings();
    s.driftWindow = 32;
    report("GOOD MODE -- driftWindow 32", solvePremixedFlame(chemistry, conditions, s), conditions);
  }
  {
    auto s = baseSettings();
    s.driftWindow = 16;
    report("BAD MODE -- driftWindow 16", solvePremixedFlame(chemistry, conditions, s), conditions);
  }

  std::printf("\nHOW TO READ THIS\n");
  std::printf("  inlet rise ~ 1 and outlet fuel ~ 0   -> FLASHBACK: the front is at the boundary\n");
  std::printf("  inlet rise ~ 0 and outlet fuel ~ 1   -> BREAKTHROUGH: unburnt fuel is leaving\n");
  std::printf("  both ~ 0                             -> neither; the loss is interior, and the\n");
  std::printf("                                          fuel-inventory blocks say where\n");
  return 0;
}
