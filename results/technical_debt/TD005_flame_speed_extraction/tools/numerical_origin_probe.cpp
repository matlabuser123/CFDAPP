// TD-005 C3 -- IS THE LIMIT CYCLE NUMERICAL? Two discriminators, from dry_run.md J.4.1.
//
// INVESTIGATION ONLY. No production file is changed by this probe.
//
// WHAT IS ALREADY ESTABLISHED (logs/13, logs/14, logs/15): a limit cycle in the front position,
// period 14 relaxation times, amplitude ~165 cells (~3.3 mm), undiminished at 8 periods, and
// S_L = 4.86230 cm/s unchanged in all 19 solves across every phase.
//
// WHY A NUMERICAL ORIGIN IS SUSPECTED. Ze = 6.68 and Le = 1.0 exactly, so Ze(Le-1) = 0. The
// classical 1D planar pulsating instability needs that product above a positive threshold of order
// ten, so this configuration sits far below it and a sustained oscillation is not physically
// expected. That is an argument; these are measurements.
//
// =================================================================================================
// TEST N1 -- RECENTRING EFFECTIVELY DISABLED.  The moving frame is the prime suspect.
// =================================================================================================
//
// Recentring translates the profile by a WHOLE number of cells and is non-conservative; it is
// TD-004's entire subject. Setting recentreDeadBandCells = 200 disables it cleanly without a
// production change: PremixedFlame.cpp:931 returns early when |offsetCells| < the band, and the
// measured excursion is ~165 cells, so the branch never fires. The front still stays inside the
// domain (anchor is cell 175, the excursion reaches cell 10), so the march remains runnable.
//
// Test A already swept the band to 10 cells and the cycle persisted -- but 10 is far below the
// 165-cell excursion, so recentring still fired there. 200 is the first value that turns it OFF.
//
//   IF the cycle NEEDS the moving frame   the good/bad pattern collapses: the bad phases at
//                                         relaxT 18, 32, 46 stop appearing.
//   FALSIFIED IF                          the same pattern appears with recentring off. Then the
//                                         frame treatment is NOT the mechanism and the suspicion
//                                         moves to the time integration or the discretisation.
//
// =================================================================================================
// TEST N2 -- THE MESH.  The classic physical-versus-numerical discriminator.
// =================================================================================================
//
//   250 cells against the 500-cell baseline, same 10 mm domain: dx 40 um against 20 um, and dt
//   scales with dx through the Courant condition, so BOTH are halved.
//
// THE POINT, and it is why this is cheap: `finalMarchRelaxationTimes` is already a PHYSICAL unit --
// a multiple of alpha/S^2 -- so it is mesh-independent by construction. The 500-cell mesh puts
// ~397 march steps in one relaxation time; the 250-cell mesh puts ~198. So:
//
//   IF the period is MESH-INDEPENDENT in relaxT units   the clock is the flame's, not the mesh's.
//                                                        The bad phases land at the SAME relaxT.
//   IF it MOVES with the mesh                            the period is set by dx or dt, which is
//                                                        a numerical origin, full stop.
//   AMPLITUDE                                            compared in MILLIMETRES, not cells: 165
//                                                        cells at 500 is 3.30 mm, which would be
//                                                        82.5 cells at 250. A physical amplitude
//                                                        is invariant in mm; a numerical one
//                                                        usually is not, and a discretisation
//                                                        artefact should SHRINK as dx falls.
//
// BOTH TESTS SHARE THE SAME relaxT LADDER {10, 18, 26, 32, 40, 46}, chosen from the period-14
// model so that 18, 32 and 46 are predicted BAD and 10, 26 and 40 predicted GOOD. A probe that
// sampled only one phase could not detect a change in the pattern at all -- which is the mistake
// J.4.1's own proposed {60, 80, 120} made for the decay test.
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
  Real relaxT{};
  Real offsetMm{};
  Real offsetCells{};
  Real fuelBalance{};
  Real flameSpeed{};
  Real thickness{};
  Index marchSteps{};
  Index recentrings{};
  int status{-1};
  Real wall{};
  bool bad{false};
};

Row run(const PremixedFlameChemistry& chem, const PremixedFlameConditions& cond,
        const PremixedFlameSettings& s, Real relaxT) {
  const Real cellSize = kDomain / static_cast<Real>(s.cells);
  const Real xAnchor = kAnchorFraction * kDomain;
  const auto t0 = std::chrono::steady_clock::now();
  const PremixedFlameResult f = solvePremixedFlame(chem, cond, s);
  Row r;
  r.relaxT = relaxT;
  r.offsetMm = (f.flamePosition - xAnchor) * 1000.0;
  r.offsetCells = (f.flamePosition - xAnchor) / cellSize;
  r.fuelBalance = f.fuelBalanceResidual;
  r.flameSpeed = f.flameSpeed;
  r.thickness = f.flameThickness;
  r.marchSteps = f.finalMarchSteps;
  r.recentrings = f.finalMarchRecentrings;
  r.status = static_cast<int>(f.status);
  r.wall = std::chrono::duration<Real>(std::chrono::steady_clock::now() - t0).count();
  // "bad" is defined on the PHYSICAL excursion, so it means the same thing on both meshes.
  r.bad = std::abs(r.offsetMm) > 1.0;
  return r;
}

void header() {
  std::printf("%8s %9s %11s %11s %11s %9s %9s %8s %8s %7s %6s\n", "relaxT", "marchStp",
              "offset mm", "offset cel", "fuel bal", "S_L cm/s", "delta mm", "recentr", "wall s",
              "status", "phase");
  std::fflush(stdout);
}

void printRow(const Row& r) {
  std::printf("%8.0f %9lld %11.4f %11.2f %11.3e %9.5f %9.4f %8lld %8.0f %7d %6s\n", r.relaxT,
              static_cast<long long>(r.marchSteps), r.offsetMm, r.offsetCells, r.fuelBalance,
              r.flameSpeed * 100.0, r.thickness * 1000.0,
              static_cast<long long>(r.recentrings), r.wall, r.status, r.bad ? "BAD" : "good");
  std::fflush(stdout);
}

std::string pattern(const std::vector<Row>& rows) {
  std::string p;
  for (const Row& r : rows) {
    p += (r.bad ? 'B' : 'G');
  }
  return p;
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

  const std::vector<Real> ladder = {10.0, 18.0, 26.0, 32.0, 40.0, 46.0};
  const std::string reference = "GBGBGB";  // the period-14 model's prediction for this ladder

  std::printf("TD-005 C3 -- IS THE LIMIT CYCLE NUMERICAL?\n\n");
  std::printf("  ladder relaxT {10, 18, 26, 32, 40, 46}; the period-14 model predicts %s\n",
              reference.c_str());
  std::printf("  500-cell baseline measured exactly that (logs/14), amplitude ~3.30 mm\n");
  std::printf("  \"BAD\" is |offset| > 1.0 mm -- a PHYSICAL threshold, so it means the same thing\n");
  std::printf("  on both meshes.\n\n");

  // ===================================================== N1 -- recentring off, 500 cells
  std::printf("=== TEST N1 -- RECENTRING DISABLED (deadBand 200 cells, 500-cell mesh)\n");
  std::printf("    Collapse of the pattern => the moving frame IS the mechanism.\n");
  std::printf("    Same pattern => the frame is NOT the mechanism; suspicion moves to the\n");
  std::printf("    time integration or the discretisation.\n\n");
  header();
  std::vector<Row> n1;
  for (const Real t : ladder) {
    PremixedFlameSettings s;
    s.cells = 500;
    s.domainLength = kDomain;
    s.anchorFraction = kAnchorFraction;
    s.tolerance = 1.0e-3;
    s.maxOuterIterations = 40;
    s.driftSettleTolerance = 0.05;
    s.maximumStepsPerTrial = 20000;
    s.driftWindow = 32;
    s.finalMarchRelaxationTimes = t;
    s.recentreDeadBandCells = 200.0;  // > the 165-cell excursion: recentring never fires
    n1.push_back(run(chemistry, conditions, s, t));
    printRow(n1.back());
  }
  {
    const std::string p = pattern(n1);
    Index totalRecentrings = 0;
    for (const Row& r : n1) {
      totalRecentrings += r.recentrings;
    }
    std::printf("\n    pattern %s   (baseline %s)   final-march recentrings total: %lld\n",
                p.c_str(), reference.c_str(), static_cast<long long>(totalRecentrings));
    if (totalRecentrings != 0) {
      std::printf("    *** TEST N1 INVALID: recentring still fired, so it was not disabled.\n");
    } else if (p == reference) {
      std::printf("    => SAME PATTERN with recentring OFF. The moving frame is NOT the mechanism.\n");
    } else {
      std::printf("    => PATTERN CHANGED with recentring off. The moving frame IS implicated.\n");
    }
    std::fflush(stdout);
  }

  // ===================================================== N2 -- 250-cell mesh
  std::printf("\n=== TEST N2 -- THE MESH: 250 cells (dx 40 um, dt doubled) vs the 500-cell baseline\n");
  std::printf("    Same pattern in relaxT units => the period is set by the FLAME's clock.\n");
  std::printf("    Pattern moves => the period is set by dx or dt, i.e. NUMERICAL.\n");
  std::printf("    Amplitude is compared in MILLIMETRES: 3.30 mm at 500 cells.\n\n");
  header();
  std::vector<Row> n2;
  for (const Real t : ladder) {
    PremixedFlameSettings s;
    s.cells = 250;
    s.domainLength = kDomain;
    s.anchorFraction = kAnchorFraction;
    s.tolerance = 1.0e-3;
    s.maxOuterIterations = 40;
    s.driftSettleTolerance = 0.05;
    s.maximumStepsPerTrial = 20000;
    s.driftWindow = 32;
    s.finalMarchRelaxationTimes = t;
    n2.push_back(run(chemistry, conditions, s, t));
    printRow(n2.back());
  }
  {
    const std::string p = pattern(n2);
    Real peakMm = 0.0;
    for (const Row& r : n2) {
      peakMm = std::max(peakMm, std::abs(r.offsetMm));
    }
    std::printf("\n    pattern %s   (500-cell baseline %s)\n", p.c_str(), reference.c_str());
    std::printf("    peak |offset| %.3f mm at 250 cells   against 3.300 mm at 500 cells\n", peakMm);
    if (p == reference && peakMm > 2.0 && peakMm < 5.0) {
      std::printf("    => MESH-INDEPENDENT in both phase and physical amplitude. The clock and the\n");
      std::printf("       size are the flame's, not the mesh's. A numerical origin is NOT supported\n");
      std::printf("       by this test, and J.4's Le argument and this result now CONFLICT -- which\n");
      std::printf("       must be reported, not resolved by preference.\n");
    } else if (p != reference) {
      std::printf("    => THE PHASE MOVED WITH THE MESH. The period is set by the discretisation:\n");
      std::printf("       a NUMERICAL origin, and J.4's argument is corroborated.\n");
    } else {
      std::printf("    => the phase pattern held but the AMPLITUDE changed with the mesh, which is\n");
      std::printf("       a numerical signature in the amplitude alone. Report both.\n");
    }
    std::fflush(stdout);
  }

  std::printf("\nNOT RUN: the Lewis-number sweep (J.4.1's first test). Le is already 1.0 and the\n");
  std::printf("flame oscillates, so varying it upward would move TOWARD the physical instability\n");
  std::printf("threshold and could not distinguish a numerical cycle from a physical one appearing\n");
  std::printf("on top of it. It is the weakest of the three and is left for a later item.\n");
  return 0;
}
