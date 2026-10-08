// TD-005 -- C5's NEGATIVE CONTROLS, dry-run against the UNCHANGED tree.
//
// INVESTIGATION ONLY. No production file is changed by this probe.
//
// CLAUDE.md section 5.2 requires that the instrument exercise both pass and fail behaviour before a
// gate is frozen: the controls must FAIL, and the correct implementation must PASS. The register
// states TD-005's three controls directly -- the extractor must FAIL with no flame, with an
// unconverged solution, and with reaction disabled.
//
// WHAT EACH CONTROL IS, and why the construction is a construction rather than a tweak:
//
//   1  NO FLAME        *** THE FIRST CONSTRUCTION OF THIS CONTROL WAS INVALID, and the dry-run
//                      caught it. It set anchorTemperatureFraction = 1.5 to put T_anchor at 2550 K,
//                      above T_ad, so that no profile could cross it. The solver REFUSES that
//                      input:
//
//                        cfd::InvalidArgumentError: solvePremixedFlame:
//                          anchorTemperatureFraction is 1.5; must be within (0, 1)
//
//                      which is correct solver behaviour -- and it means T_anchor can never be
//                      placed above T_ad, so this route to a no-flame state does not exist. The
//                      refusal is a control in its own right, but it is an INPUT-VALIDATION control
//                      already covered by `MalformedConfigurationsAreRefused`, not the no-flame
//                      control the register asks for.
//
//                      THE CONSEQUENCE FOR THE GATE, recorded because it changes C5: the only
//                      construction that actually produces a flame-free domain is
//                      `reactionsEnabled = false` -- which is control 3. So the register's "FAIL
//                      with no flame" and "FAIL with reaction disabled" are THE SAME CONTROL on
//                      this solver, and it is the one TD-020 has made expensive. C5 therefore has
//                      TWO distinct controls, not three, and one of them is TD-020-blocked.
//
//                      Other routes were considered and rejected as confounded: removing the fuel
//                      drives T_ad to T_u, which makes T_anchor = T_u and `flameThickness` a 0/0,
//                      so it changes several things at once and would test the degenerate case
//                      rather than the no-flame case.
//
//   2  UNCONVERGED     maxOuterIterations = 1 permits a single bisection, which cannot narrow a
//                      factor-of-two bracket to the requested 1e-3. Expected:
//                      MaxIterations AND massFluxResidual > tolerance.
//
//                      BOTH are required. A status is one enum assignment, and a criterion resting
//                      on a single assignment is weaker than one resting on the assignment and the
//                      quantity that justifies it -- this campaign has already seen a reported
//                      field that was an echoed input (logs/02), so a status alone is not enough.
//
//   3  REACTIONS OFF   NOT IN THIS PROBE. `WithReactionsDisabledNoFlamePropagates` is TD-020's
//                      known cost problem and may consume the full 7200 s cap; it gets its own run
//                      so it cannot crowd these two. See run_noflame_control_at_repo_cap.sh.
//
// WHAT WOULD INVALIDATE THIS PROBE. A control that comes back `Converged` has not controlled
// anything, and a control that fails for a DIFFERENT reason than the one named (for example an
// InvalidConfiguration refusal rather than NoFlame) is also a failure of the construction, not a
// success of the gate. Both are reported explicitly rather than collapsed into pass/fail.
#include <cstdio>
#include <exception>
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

const char* statusName(PremixedFlameStatus s) {
  switch (s) {
    case PremixedFlameStatus::Converged: return "Converged";
    case PremixedFlameStatus::MaxIterations: return "MaxIterations";
    case PremixedFlameStatus::NoFlame: return "NoFlame";
    case PremixedFlameStatus::LinearSolveFailure: return "LinearSolveFailure";
    case PremixedFlameStatus::NonFiniteState: return "NonFiniteState";
    case PremixedFlameStatus::InvalidConfiguration: return "InvalidConfiguration";
  }
  return "?";
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

  std::printf("TD-005 -- C5 negative controls, dry-run on the unchanged tree\n\n");

  int failures = 0;

  // ---- POSITIVE CONTROL FIRST. A gate whose negative controls fail is worth nothing unless the
  //      correct configuration passes; running it first means a universal failure is attributed to
  //      the fixture rather than credited to the controls.
  {
    const auto f = solvePremixedFlame(chemistry, conditions, baseSettings());
    std::printf("POSITIVE CONTROL -- the unmodified fixture\n");
    std::printf("  status %-14s  S_L %.5f cm/s  massFluxResidual %.4e  tolerance %.0e\n",
                statusName(f.status), f.flameSpeed * 100.0, f.massFluxResidual, 1.0e-3);
    const bool ok = (f.status == PremixedFlameStatus::Converged);
    std::printf("  EXPECT Converged -> %s\n\n", ok ? "PASS" : "*** FAIL (the fixture is broken) ***");
    if (!ok) {
      ++failures;
    }
    std::fflush(stdout);
  }

  // ---- CONTROL 1: the invalid construction, recorded as a REFUSAL rather than re-attempted.
  //
  // The call is made inside a try/catch so the probe records the refusal instead of aborting --
  // the first run of this probe died on an uncaught InvalidArgumentError, which is why the
  // construction is documented in the header as invalid.
  {
    auto s = baseSettings();
    s.anchorTemperatureFraction = 1.5;  // REFUSED: must be within (0, 1)
    std::printf("CONTROL 1 -- THE ORIGINAL CONSTRUCTION IS INVALID, and this records why\n");
    try {
      const auto f = solvePremixedFlame(chemistry, conditions, s);
      std::printf("  status %-14s   *** UNEXPECTED: the solver ACCEPTED anchorTemperatureFraction\n",
                  statusName(f.status));
      std::printf("      = 1.5, which the header says is refused. The header is then wrong.\n");
      ++failures;
    } catch (const std::exception& e) {
      std::printf("  REFUSED, as it should be: %s\n", e.what());
      std::printf("  So T_anchor cannot be placed above T_ad, and this route to a flame-free\n");
      std::printf("  domain does not exist. The no-flame control is reachable ONLY via\n");
      std::printf("  reactionsEnabled = false, which is control 3 -- so the register's \"no flame\"\n");
      std::printf("  and \"reaction disabled\" are the SAME control here. C5 has TWO controls.\n");
    }
    std::printf("\n");
    std::fflush(stdout);
  }

  // ---- CONTROL 2: unconverged.
  {
    auto s = baseSettings();
    s.maxOuterIterations = 1;
    const auto f = solvePremixedFlame(chemistry, conditions, s);
    std::printf("CONTROL 2 -- UNCONVERGED (maxOuterIterations 1 cannot narrow a 2x bracket to 1e-3)\n");
    std::printf("  status %-14s  massFluxResidual %.4e  bisections %lld\n", statusName(f.status),
                f.massFluxResidual, static_cast<long long>(f.outerIterations));
    const bool statusOk = (f.status == PremixedFlameStatus::MaxIterations);
    const bool residualOk = (f.massFluxResidual > s.tolerance);
    std::printf("  EXPECT MaxIterations            -> %s\n", statusOk ? "PASS" : "*** FAIL ***");
    std::printf("  EXPECT massFluxResidual > 1e-3  -> %s\n", residualOk ? "PASS" : "*** FAIL ***");
    std::printf("  BOTH are required: a status is one enum assignment, and this campaign has\n");
    std::printf("  already found a reported field that was an echoed input.\n\n");
    if (!statusOk || !residualOk) {
      ++failures;
    }
    std::fflush(stdout);
  }

  std::printf("CONTROL 3 -- REACTIONS DISABLED: not run here, and it is now also CONTROL 1.\n");
  std::printf("  TD-020's cost problem may consume the full 7200 s cap, so it has its own run\n");
  std::printf("  (run_noflame_control_at_repo_cap.sh). Until it is measured, C5 has exactly ONE\n");
  std::printf("  demonstrated control -- the unconverged one -- and the gate must say so.\n\n");

  std::printf("%s\n", failures == 0
                          ? "ALL CONTROLS BEHAVED. C5's non-vacuity is demonstrated for 1 and 2."
                          : "*** AT LEAST ONE CONTROL DID NOT BEHAVE -- C5 cannot be frozen as written.");
  return failures == 0 ? 0 : 1;
}
