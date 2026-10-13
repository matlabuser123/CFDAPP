// TD-004 AUDIT -- WHY does the flame settle 87 cells upstream when a recentring operator exists?
//
// THE ARITHMETIC THAT PROMPTS THIS. `recentre()` ignores any offset inside the dead band and
// REFUSES any offset beyond `maximumShiftCells = recentreDeadBandCells + admissibleJumpCells`:
//
//     recentreDeadBandCells = 3.0
//     admissibleJumpCells   = kJumpMargin(8) * (u_burnt + S_L) * dt / cellSize = 1.234 cells
//     => the operator ACTS only for 3.000 < |offset| < 4.234 cells
//
// The measured front sits ~87 cells off the anchor (TD030 logs/03). So either the operator never
// got the chance to act, or it has been refusing. Both leave fingerprints in counters the solver
// already reports, and those counters describe the OPERATOR'S DECISIONS rather than the field --
// which matters, because five earlier TD-004 diagnoses died from reading counters derived from the
// very field being diagnosed.
//
// PRE-REGISTERED READINGS, one per direction, written before the run:
//
//   rejectedShifts LARGE and finalMarchRecentrings ~0
//        -> the operator is REFUSING. The front escaped the 4.234-cell window and the refusal
//           branch then makes the escape permanent: it can never shift back.
//   rejectedShifts ~0 and finalMarchRecentrings LARGE
//        -> the operator is ACTING on nearly every step and still not holding the front. The
//           refusal branch is innocent and the defect is that recentring does not stick.
//   rejectedShifts ~0 and finalMarchRecentrings ~0
//        -> the operator is never INVOKED, so `locateFront` is failing. The defect is upstream of
//           recentring entirely.
//   largestShiftCells > 4
//        -> the bound was not enforced as derived, and the arithmetic above is wrong somewhere.
//
// `worstFinalOffsetCells` is reported but NOT relied on: TD-026 established it is updated only on
// steps where the CONTROL locator accepted a crossing, so it cannot see the offsets it names.
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

  std::printf("TD-004 AUDIT -- THE RECENTRING OPERATOR'S OWN DECISIONS\n\n");
  std::printf("  dead band %.1f cells; the operator ACTS only for 3.000 < |offset| < ~4.234\n",
              3.0);
  std::printf("  anchor x = %.3f mm; the measured front sits ~87 cells upstream of it\n\n",
              xAnchor * 1000.0);
  std::printf("%7s %10s %9s %10s %9s %10s %11s %10s\n", "relaxT", "front mm", "offCells",
              "recentres", "final", "rejected", "largestShf", "steps");
  std::fflush(stdout);

  for (const Real relaxT : {10.0, 18.0, 30.0}) {
    PremixedFlameSettings s;
    s.cells = kCells;
    s.domainLength = kDomain;
    s.anchorFraction = kAnchorFraction;
    s.tolerance = 1.0e-3;
    s.maxOuterIterations = 40;
    s.driftSettleTolerance = 0.05;
    s.driftWindow = 32;
    s.timeStep = kDtQuarter;
    s.finalMarchRelaxationTimes = relaxT;
    s.maximumStepsPerTrial = 80000;

    const PremixedFlameResult f = solvePremixedFlame(chemistry, conditions, s);
    const Real offCells = (f.flamePosition - xAnchor) / dx;

    std::printf("%7.0f %10.6f %9.2f %10lld %9lld %10lld %11lld %10lld\n", relaxT,
                f.flamePosition * 1000.0, offCells, static_cast<long long>(f.recentrings),
                static_cast<long long>(f.finalMarchRecentrings),
                static_cast<long long>(f.rejectedShifts),
                static_cast<long long>(f.largestShiftCells),
                static_cast<long long>(f.finalMarchSteps));
    // ---- THE LOCATOR'S OWN REJECTION RECORD. `rejectedShifts == 0` with the front 86 cells out
    //      means `recentre` was never INVOKED, so `locateFront` must be refusing -- or finding
    //      nothing. These fields say how often, how far and where. They record the LOCATOR'S
    //      decisions, not a quantity derived from the field being diagnosed, which is the
    //      distinction that killed five earlier TD-004 diagnoses.
    std::printf("        locator: maxCrossings %lld | worstRejectedJump %.3f cells at %.6f mm\n",
                static_cast<long long>(f.maxFrontCrossings), f.worstRejectedJumpCells,
                f.worstRejectedJumpPosition * 1000.0);
    std::printf("        rejected jumps: <10c %lld | 10-50c %lld | >50c %lld | at anchor %lld\n",
                static_cast<long long>(f.rejectedJumpsUnder10Cells),
                static_cast<long long>(f.rejectedJumps10To50Cells),
                static_cast<long long>(f.rejectedJumpsOver50Cells),
                static_cast<long long>(f.rejectedJumpsLandingAtAnchor));
    std::printf("        worstFrontJumpCells %.4f  (largest ACCEPTED single-step move)\n",
                f.worstFrontJumpCells);
    std::fflush(stdout);

    if (relaxT == 30.0) {
      std::printf("\n=== READING, against the directions registered in this file's header\n");
      const bool refusing = f.rejectedShifts > 100 && f.finalMarchRecentrings < 10;
      const bool acting = f.rejectedShifts < 100 && f.finalMarchRecentrings > 100;
      const bool silent = f.rejectedShifts < 100 && f.finalMarchRecentrings < 10;
      if (f.largestShiftCells > 4) {
        std::printf("    *** largestShiftCells = %lld EXCEEDS the derived bound of 4. The bound is\n",
                    static_cast<long long>(f.largestShiftCells));
        std::printf("        not enforced as derived and the arithmetic in this header is wrong.\n");
      } else if (refusing) {
        std::printf("    => THE OPERATOR IS REFUSING. %lld rejected shifts against %lld final-march\n",
                    static_cast<long long>(f.rejectedShifts),
                    static_cast<long long>(f.finalMarchRecentrings));
        std::printf("       recentrings. Once the front escapes the 4.234-cell window the refusal\n");
        std::printf("       branch makes the escape PERMANENT: it can never shift back.\n");
      } else if (acting) {
        std::printf("    => THE OPERATOR IS ACTING and still not holding the front. The refusal\n");
        std::printf("       branch is innocent; recentring does not STICK.\n");
      } else if (silent) {
        std::printf("    => THE OPERATOR IS NEVER INVOKED -- locateFront is failing, and the defect\n");
        std::printf("       is upstream of recentring entirely.\n");
      } else {
        std::printf("    => MIXED: %lld recentrings, %lld rejected. Neither reading is clean; report\n",
                    static_cast<long long>(f.finalMarchRecentrings),
                    static_cast<long long>(f.rejectedShifts));
        std::printf("       the numbers and do not round to either.\n");
      }
    }
  }
  return 0;
}
