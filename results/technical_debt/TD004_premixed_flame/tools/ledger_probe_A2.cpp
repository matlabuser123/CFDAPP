// TD-004 -- IS THE RECENTRING LEDGER BIG ENOUGH TO EXPLAIN A 6 % EIGENVALUE SHIFT?
//
// THE QUESTION. The locator fix made `recentre()` fire 3 times per final march instead of 1, and
// `S_L` moved 4.67194 -> 4.95533 cm/s (+6.07 %), away from TD-027's Richardson limit of 4.67589.
// Two readings, and the verification cannot separate them:
//
//   (i)  the fix ALTERED THE PHYSICS -- more translations, and translation is not free
//   (ii) the PRE-FIX eigenvalue was wrong, measured through a blind locator, and 4.95533 is the
//        correction. TD-027's limit was extrapolated on the same blind solver, so it is not
//        independent of the defect.
//
// THE INSTRUMENT ALREADY EXISTS and is dimensionally perfect for this. `recentringFuelLedgerRate`
// is in kg/(m^2 s) -- the same units as `fuelThrough = mdot * Y_F,u` -- so the ledger can be put on
// exactly the scale the fuel budget uses, where convection is -100 %, reaction -103.9 % and inlet
// diffusion -5.0 %. The ledger is RESET at the start of the final march, so it reports that march's
// translations only.
//
// PRE-REGISTERED READINGS, one per direction:
//
//   |rate/fuelThrough| well below 0.1 %
//        -> translation is conservation-neutral at the scale that matters. Three of them cannot
//           move an eigenvalue 6 %, and reading (i) is strongly weakened.
//   |rate/fuelThrough| of order 1-10 %
//        -> translation is NOT neutral and is the same size as the terms that moved. Reading (i)
//           is supported and the fix is wrong as written despite the balance improving 15x.
//   the rate is the SAME on both libraries despite 1 vs 3 recentrings
//        -> the ledger is not tracking the translations it claims to, which is an instrument
//           defect and both readings above become unsupported.
//   the PRE-fix ledger is LARGER than the post-fix one
//        -> the single pre-fix translation was more damaging than three tracked ones, which would
//           invert the whole argument and needs its own investigation.
//
// Fixture: the C2-A1/trace fixture -- 500 cells, dt pinned 9.4243e-06, relaxT 10.
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

  PremixedFlameSettings s;
  s.cells = kCells;
  s.domainLength = kDomain;
  s.anchorFraction = kAnchorFraction;
  s.tolerance = 1.0e-3;
  s.maxOuterIterations = 40;
  s.driftSettleTolerance = 0.05;
  s.driftWindow = 32;
  s.timeStep = kDtQuarter;
  s.finalMarchRelaxationTimes = 10.0;
  s.maximumStepsPerTrial = 80000;

  const PremixedFlameResult f = solvePremixedFlame(chemistry, conditions, s);

  const Real fuelThrough = f.massFlux * kFuelFraction;
  const Real dx = kDomain / static_cast<Real>(kCells);
  const Real xAnchor = kAnchorFraction * kDomain;

  std::printf("TD-004 -- THE RECENTRING LEDGER, on the scale the fuel budget uses\n\n");
  std::printf("  S_L                      %.6f cm/s\n", f.flameSpeed * 100.0);
  std::printf("  front                    %.6f mm   (%.3f cells off the %.3f mm anchor)\n",
              f.flamePosition * 1000.0, (f.flamePosition - xAnchor) / dx, xAnchor * 1000.0);
  std::printf("  fuelBalanceResidual      %.6e\n", f.fuelBalanceResidual);
  std::printf("  final march steps        %lld\n", static_cast<long long>(f.finalMarchSteps));
  std::printf("  recentrings  total %lld, FINAL MARCH %lld\n",
              static_cast<long long>(f.recentrings),
              static_cast<long long>(f.finalMarchRecentrings));
  std::printf("\n  fuelThrough (normaliser) %.6e kg/(m^2 s)\n", fuelThrough);
  std::printf("  recentringFuelLedger     %.6e kg/m^2        (final march only -- it is reset)\n",
              f.recentringFuelLedger);
  std::printf("  recentringFuelLedgerRate %.6e kg/(m^2 s)\n", f.recentringFuelLedgerRate);
  if (fuelThrough > 0.0) {
    std::printf("    AS %% OF fuelThrough    %+.6f %%   <- compare: convection -100, reaction\n",
                f.recentringFuelLedgerRate / fuelThrough * 100.0);
    std::printf("                                        -103.9, inlet diffusion -5.0\n");
  }
  std::printf("  recentringEnergyLedger   %.6e J/m^2\n", f.recentringEnergyLedger);
  std::printf("  recentringEnergyLedgerRate %.6e W/m^2\n", f.recentringEnergyLedgerRate);
  if (f.finalMarchRecentrings > 0) {
    std::printf("\n  per recentring: fuel %.6e kg/m^2, energy %.6e J/m^2\n",
                f.recentringFuelLedger / static_cast<Real>(f.finalMarchRecentrings),
                f.recentringEnergyLedger / static_cast<Real>(f.finalMarchRecentrings));
  }

  std::printf("\n=== READING, against the directions in this file's header\n");
  if (fuelThrough > 0.0) {
    const Real pct = std::abs(f.recentringFuelLedgerRate / fuelThrough * 100.0);
    if (pct < 0.1) {
      std::printf("    %.6f %% of fuelThrough -- WELL BELOW 0.1 %%. Translation is\n", pct);
      std::printf("    conservation-neutral at the scale that matters, so three of them cannot\n");
      std::printf("    move an eigenvalue by 6 %%. Reading (i) is strongly WEAKENED.\n");
    } else if (pct < 1.0) {
      std::printf("    %.6f %% of fuelThrough -- INTERMEDIATE. Neither reading is clean; report\n", pct);
      std::printf("    the number and do not round to either.\n");
    } else {
      std::printf("    %.6f %% of fuelThrough -- the SAME ORDER as the terms that moved.\n", pct);
      std::printf("    Translation is NOT neutral and reading (i) is SUPPORTED: the fix is wrong\n");
      std::printf("    as written, despite the balance improving 15x.\n");
    }
  }
  std::printf("\n  This run measures ONE library. The pre-fix comparison follows in the same log.\n");
  return 0;
}
