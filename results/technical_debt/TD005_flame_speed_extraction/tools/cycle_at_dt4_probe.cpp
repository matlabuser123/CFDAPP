// TD-005 C3 -- DOES THE LIMIT CYCLE SURVIVE INSIDE THE ACCURATE REGION?
//
// INVESTIGATION ONLY. No production file is changed by this probe.
//
// THE QUESTION, and why it is the highest-value measurement left in this item.
//
// logs/18 established that S_L converges in dt, that the DEFAULT dt sits outside the asymptotic
// range at a cell diffusion number D = alpha*dt/dx^2 = 3.33, and that accuracy needs D <~ 1:
//
//     dt/1  D = 3.333   S_L +3.99 % against the converged value   <- the DEFAULT
//     dt/4  D = 0.833   S_L -0.08 %
//
// Every observation of the limit cycle so far -- logs/13, logs/14, logs/15, logs/16, logs/17 --
// was made at D between 0.83 and 3.33, i.e. straddling the boundary of the accurate region, and
// mostly at the inaccurate D = 3.33. So the cycle has never been observed INSIDE the region where
// the time integration is converged.
//
// WHAT TURNS ON THIS
//
//   CYCLE GONE at dt/4   then the cycle is an artefact of an over-large cell diffusion number, and
//                        ONE unvalidated constant -- maximumCellDiffusionNumber = 20.0, realized
//                        as 3.33 -- accounts for ALL of: TD-005's C3 (5 of 12 inadmissible
//                        states), TD-004's C2 (its "degradation with march length", which is the
//                        cycle sampled at two phases), the 4 % error in every recorded S_L, and
//                        the unreachability of finalMarchTolerance. That is four open findings
//                        collapsing into one setting.
//   CYCLE SURVIVES       then the cycle is independent of D, section M.4's suggested connection is
//                        REFUTED, and C3 remains unexplained after the dt error is removed.
//   CYCLE WEAKER         partial: D contributes but is not the whole mechanism. Report the
//                        amplitude, do not round it to either verdict.
//
// THE LADDER. relaxT {10, 14, 18, 22, 26, 30} at dt/4, fixed 500 cells. The baseline at the
// DEFAULT dt gave exactly this ladder in logs/14:
//
//     relaxT   10     14     18      22     26     30
//     offset  +1.17  +2.76  -162.81  +1.30  +0.62  -162.24      pattern GGBGGB
//
// so the comparison is direct. The span is 20 relaxT units against a measured period of 14, and
// the step is 4 -- fine enough not to alias a period that may itself have MOVED with dt (it went
// 14 -> ~18 between dt and dt/2 in logs/17), which is why the ladder is denser and shorter rather
// than the wider, coarser one used earlier.
//
// PHASE IS CLASSIFIED BY HEALTH, NOT POSITION. logs/16's N1 verdict was wrong because it judged by
// offset-from-anchor in a test that had removed what holds the front there. The fuel balance is a
// property of the profile, so it is what classifies a row; the offset is printed beside it.
//
// maximumStepsPerTrial IS SCALED BY 4, for the same reason as logs/17 and logs/18: at dt/4 a trial
// needs four times the steps for the same physical time, and an unscaled cap would truncate trials
// and change the bracketing. It is a safety cap, not an accuracy tolerance.
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
constexpr Real kDtQuarter = 9.4243e-06;  // dt/4, D = 0.833

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

  const Real cellSize = kDomain / static_cast<Real>(kCells);
  const Real xAnchor = kAnchorFraction * kDomain;
  const std::vector<Real> ladder = {10.0, 14.0, 18.0, 22.0, 26.0, 30.0};
  const std::string reference = "GGBGGB";  // logs/14, same ladder, DEFAULT dt (D = 3.33)

  std::printf("TD-005 C3 -- DOES THE LIMIT CYCLE SURVIVE AT dt/4 (D = 0.833)?\n\n");
  std::printf("  500 cells, dt pinned at %.4e s -- INSIDE the accurate region (S_L -0.08 %%)\n",
              kDtQuarter);
  std::printf("  baseline on this SAME ladder at the default dt (D = 3.33), logs/14:\n");
  std::printf("    relaxT   10     14     18       22     26     30\n");
  std::printf("    offset  +1.17  +2.76  -162.81  +1.30  +0.62  -162.24   pattern %s\n",
              reference.c_str());
  std::printf("  amplitude there: ~163 cells = ~3.26 mm\n");
  std::printf("  phase classified by HEALTH (fuel balance > 0.1), not by position.\n\n");
  std::printf("%8s %10s %11s %11s %11s %9s %9s %8s %6s\n", "relaxT", "marchStp", "offset cel",
              "offset mm", "fuel bal", "delta mm", "S_L cm/s", "wall s", "phase");
  std::fflush(stdout);

  std::vector<Real> offsets;
  std::vector<Real> balances;
  std::string pattern;

  for (const Real t : ladder) {
    PremixedFlameSettings s;
    s.cells = kCells;
    s.domainLength = kDomain;
    s.anchorFraction = kAnchorFraction;
    s.tolerance = 1.0e-3;
    s.maxOuterIterations = 40;
    s.driftSettleTolerance = 0.05;
    s.driftWindow = 32;
    s.timeStep = kDtQuarter;
    s.finalMarchRelaxationTimes = t;
    s.maximumStepsPerTrial = 80000;  // 4x, matching 1/dt

    const auto t0 = std::chrono::steady_clock::now();
    const PremixedFlameResult f = solvePremixedFlame(chemistry, conditions, s);
    const Real wall = std::chrono::duration<Real>(std::chrono::steady_clock::now() - t0).count();

    const Real offsetCells = (f.flamePosition - xAnchor) / cellSize;
    const bool bad = f.fuelBalanceResidual > 0.1;
    pattern += (bad ? 'B' : 'G');
    offsets.push_back(offsetCells);
    balances.push_back(f.fuelBalanceResidual);

    std::printf("%8.0f %10lld %11.2f %11.4f %11.3e %9.4f %9.5f %8.0f %6s%s\n", t,
                static_cast<long long>(f.finalMarchSteps), offsetCells,
                (f.flamePosition - xAnchor) * 1000.0, f.fuelBalanceResidual,
                f.flameThickness * 1000.0, f.flameSpeed * 100.0, wall, bad ? "BAD" : "good",
                (f.status == PremixedFlameStatus::Converged) ? "" : "  NOT Converged");
    std::fflush(stdout);
  }

  // ---------------------------------------------------------------- the verdict
  Real peakCells = 0.0;
  Real worstBalance = 0.0;
  for (std::size_t i = 0; i < offsets.size(); ++i) {
    peakCells = std::max(peakCells, std::abs(offsets[i]));
    worstBalance = std::max(worstBalance, balances[i]);
  }
  int badRows = 0;
  for (const char c : pattern) {
    if (c == 'B') {
      ++badRows;
    }
  }

  std::printf("\n=== VERDICT\n");
  std::printf("  pattern at dt/4  %s      baseline at the default dt  %s\n", pattern.c_str(),
              reference.c_str());
  std::printf("  bad rows %d of %zu      peak |offset| %.1f cells (%.3f mm)      worst balance %.3e\n",
              badRows, ladder.size(), peakCells, peakCells * cellSize * 1000.0, worstBalance);
  std::printf("  baseline for comparison: 2 of 6 bad, peak ~163 cells, worst balance ~6.8e-01\n\n");

  if (badRows == 0 && peakCells < 20.0) {
    std::printf("  => THE CYCLE IS GONE inside the accurate region.\n");
    std::printf("     The limit cycle was an artefact of an over-large cell diffusion number, and\n");
    std::printf("     maximumCellDiffusionNumber = 20.0 (realized 3.33) accounts for ALL of:\n");
    std::printf("       * TD-005 C3 -- the 5 of 12 inadmissible states\n");
    std::printf("       * TD-004 C2 -- its 'degradation with march length', the cycle in two phases\n");
    std::printf("       * the +3.99 %% error in every recorded S_L\n");
    std::printf("       * the unreachability of finalMarchTolerance (there WAS no steady state)\n");
    std::printf("     Four open findings collapse into ONE unvalidated constant.\n");
  } else if (badRows >= 2 && peakCells > 100.0) {
    std::printf("  => THE CYCLE SURVIVES, with comparable amplitude. Section M.4's suggested\n");
    std::printf("     connection between D and the cycle is REFUTED: the cycle is independent of\n");
    std::printf("     the cell diffusion number, and C3 remains unexplained even after the dt\n");
    std::printf("     error is removed.\n");
  } else {
    std::printf("  => PARTIAL. The cycle is present but altered (%d bad rows, peak %.1f cells\n",
                badRows, peakCells);
    std::printf("     against 2 and ~163 at the default dt). D contributes but is not the whole\n");
    std::printf("     mechanism. Reported as partial -- NOT rounded to either verdict.\n");
  }
  std::fflush(stdout);
  return 0;
}
