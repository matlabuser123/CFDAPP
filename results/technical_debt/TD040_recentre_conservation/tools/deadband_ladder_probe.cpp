// TD-040 -- DOES THE EIGENVALUE TRACK THE RECENTRING RATE? A dead-band ladder.
//
// THE QUESTION. `recentre()` discards fresh reactant and fabricates burnt product, -0.79 % of
// fuelThrough per translation (audit.md §1, derived to 0.014 %). TD-004's locator fix made it fire
// 3x per march and 220x across the trials, and S_L moved +6.07 %. Is the shift CAUSED by the
// translation rate, or merely concurrent with it?
//
// A WEAKNESS IN THIS KNOB, STATED FIRST. `recentreDeadBandCells` changes shift GRANULARITY: a
// smaller band fires more often with smaller shifts, and audit.md §4.4 argues the total
// displacement -- and so the ledger RATE -- is then invariant. If that holds, the middle of this
// ladder discriminates nothing, and that is a finding about the knob rather than a result.
//
// WHAT RESCUES IT. `recentreDeadBandCells = 1000` DISABLES recentring outright: |offset| cannot
// exceed 500 cells in a 500-cell domain, so the operator can never fire. That is a genuine ON/OFF
// negative control, not a granularity sweep, and it is the level that carries the test.
//
// PRE-REGISTERED READINGS, one per direction:
//
//   S_L varies MONOTONICALLY with the recentring count / ledger rate
//        -> mechanism ESTABLISHED: translation biases the eigenvalue, and the compensating inflow
//           correction (audit.md §4.2) is the fix to derive.
//   S_L is FLAT while the ledger rate varies
//        -> translation does NOT bias the eigenvalue. The +6.07 % came from something else and
//           audit.md §2 is wrong.
//   the ledger rate is FLAT across 1..20 (§4.4 holds) but S_L still varies
//        -> something OTHER than the ledger depends on the dead band; the knob is confounded and
//           the diagnosis is incomplete.
//   recentring OFF gives S_L near 4.671937 (the 1-recentring value)
//        -> consistent with fewer translations meaning a lower eigenvalue: the monotonicity above,
//           seen from its endpoint.
//   recentring OFF gives S_L near 4.955332, or neither value
//        -> the eigenvalue does not depend on translation at all, or depends on something this
//           ladder does not vary.
//
// NO PREDICTION is registered between them.
//
// Fixture: 500 cells, dt pinned 9.4243e-06, relaxT 10 -- the TD-004 A2 fixture, so the two
// reference eigenvalues above are directly comparable.
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

  const Real dx = kDomain / static_cast<Real>(kCells);
  const Real xAnchor = kAnchorFraction * kDomain;

  std::printf("TD-040 -- S_L against the RECENTRING RATE, via the dead band\n\n");
  std::printf("  deadBand 1000 DISABLES recentring: |offset| cannot exceed 500 in 500 cells.\n");
  std::printf("  pre-fix reference   S_L 4.671937 cm/s  (1 final-march recentring, 121 total)\n");
  std::printf("  post-fix reference  S_L 4.955332 cm/s  (3 final-march recentrings, 223 total)\n\n");
  std::printf("%9s %12s %9s %7s %15s %12s %11s %11s\n", "deadBand", "S_L cm/s", "recentr",
              "final", "ledgerRate", "as % fuelT", "front off", "balance");
  std::fflush(stdout);

  for (const Real band : {1.0, 3.0, 10.0, 20.0, 1000.0}) {
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
    s.recentreDeadBandCells = band;

    const PremixedFlameResult f = solvePremixedFlame(chemistry, conditions, s);
    const Real fuelThrough = f.massFlux * kFuelFraction;
    const Real pct = (fuelThrough > 0.0)
                         ? f.recentringFuelLedgerRate / fuelThrough * 100.0
                         : 0.0;

    std::printf("%9.0f %12.6f %9lld %7lld %15.6e %12.4f %11.3f %11.3e\n", band,
                f.flameSpeed * 100.0, static_cast<long long>(f.recentrings),
                static_cast<long long>(f.finalMarchRecentrings), f.recentringFuelLedgerRate, pct,
                (f.flamePosition - xAnchor) / dx, f.fuelBalanceResidual);
    std::fflush(stdout);
  }

  std::printf("\n=== HOW TO READ THIS\n");
  std::printf("  The deadBand-1000 row is the NEGATIVE CONTROL: recentring off, ledger exactly\n");
  std::printf("  zero, and whatever S_L it gives is the eigenvalue with no translation at all.\n");
  std::printf("  If rows 1..20 show a FLAT ledger rate, audit.md section 4.4 holds and the middle\n");
  std::printf("  of this ladder discriminates nothing -- read the control row against the two\n");
  std::printf("  reference eigenvalues instead, and say so rather than reading a flat line as a\n");
  std::printf("  result.\n");
  return 0;
}
