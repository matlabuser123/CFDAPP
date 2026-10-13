// TD-005 -- BASELINE: WHAT IS THE EXTRACTION METHOD'S REPRODUCIBILITY, AND WHAT STATE IS BEHIND IT?
//
// INVESTIGATION ONLY. No production file is changed by this probe. It is the mandatory pre-freeze
// dry-run (CLAUDE.md section 5.2): it establishes whether each candidate criterion is vacuous on
// the UNCHANGED tree. Thresholds are NOT to be read off this output -- they are derived in GATE.md
// from the quantities' own floors. This probe decides only non-vacuity.
//
// ===================================================================================
// WHY THIS PROBE EXISTS, and what the recorded evidence says
// ===================================================================================
//
// results/p18/g8_closure/flame_solver_findings.md section 3.1 records the method's reproducibility
// as 4.3 % (drift window 32 -> S_L 4.50 cm/s; window 64 -> 4.32 cm/s) and states the requirement:
// "method reproducibility << 2.8 %" because G8's band 35.0-37.0 cm/s is +-2.8 % about 36. That
// measurement PREDATES `relaxationTimesPerTrial`, which the header says replaced the fixed 128-step
// trial precisely because "S_L depended on the drift window at the 4.3 % level". So the recorded
// blocker may no longer reproduce -- and CLAUDE.md section 8 says a known failure stays known only
// while it reproduces its recorded values exactly. First job: re-measure it.
//
// ===================================================================================
// THE DEFECT THIS PROBE IS BUILT TO EXPOSE: THE SWEEPS ARE FAIL-OPEN
// ===================================================================================
//
// `TheAnswerIsInsensitiveToTheStepsPerTrial` PASSES (TD-004 logs/06, 744 s) and asserts on
// `flameSpeed` ALONE. Its own printed output, in the same log:
//
//     16 drift window: S_L = 4.85901 cm/s  S_c = 3.04353 cm/s  delta = 0.736 mm
//                      fuel balance = 0.373632                     <-- 37 % OUT
//     32 drift window: S_L = 4.86230 cm/s  S_c = 4.90006 cm/s  delta = 1.229 mm
//                      fuel balance = 0.043280
//
// The drift-window-16 configuration returns a state that violates fuel conservation by 37 % and
// misses the consumption identity by 37 %, and the test passes because S_L agrees to 0.07 %. The
// plateau test has the same shape (anchor 0.30: S_c = 3.034, delta = 0.734; anchor 0.40:
// S_c = 4.901, delta = 1.230) and also asserts on `flameSpeed` alone.
//
// So "the answer is insensitive to the method knobs" is currently certified by instruments that
// cannot see that one configuration is not a steady flame. A reproducible number extracted from an
// inadmissible state is not a reproducible measurement; it is a coincidence that has not yet been
// tested. THIS PROBE REPORTS THE ADMISSIBILITY EVIDENCE ALONGSIDE EVERY S_L.
//
// Note on provenance: those rows are from TD-004's PRE-fix tree. TD-004 then changed `locateFront`
// and `recentre` (kept, not reverted). This probe therefore re-measures everything on the CURRENT
// tree rather than reasoning from the pre-fix log.
//
// ===================================================================================
// THE KNOBS SWEPT, and why each one is a METHOD parameter rather than a physical input
// ===================================================================================
//
//   A  driftWindow                how many steps a settling window spans. Pure numerics.
//   B  anchorFraction            where the front is pinned. THE PLATEAU -- the cold-boundary
//                                mechanism that SELECTS the eigenvalue, so this is the sweep the
//                                method's validity actually rests on. Its test TIMES OUT at 900 s
//                                (4 solves, ~250 s each), so it is currently unverified.
//   C  relaxationTimesPerTrial    the convergence requirement that replaced the fixed 128 steps.
//                                The header says "the answer must be shown insensitive to it" and
//                                NO test sweeps it. Measured here for the first time.
//   D  tolerance                 the bracket width. Not an insensitivity knob but a PRECISION
//                                ladder: S_L must CONVERGE as the bracket tightens, and the
//                                reported `massFluxResidual` must track it.
//
// Physical inputs (pressure, cp, lambda, A, E, Y_F,u) are deliberately NOT swept: changing those
// changes the flame, and the answer is supposed to move. Only A x 10 is a control, and that test
// already passes.
//
// ===================================================================================
// FAIL-CLOSED GUARDS, because an earlier probe in this campaign printed a false plateau
// ===================================================================================
//
// A TD-004 probe swept `finalMarchSteps` -- a 200000 safety ceiling that controls nothing -- and
// three identical rows read as convergence. So every sweep here asserts that its independent
// variable MOVED something observable, and prints *** SWEEP INVALID *** if all levels come back
// bit-identical. A knob that changes nothing cannot demonstrate insensitivity to itself.
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

// The G8 synthetic fixture, verbatim from tests/unit/species/test_premixed_flame.cpp.
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

// makeSettings() from the test, verbatim. This is the gated fixture, so the probe must not
// quietly use a cheaper one.
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

struct Row {
  std::string level;
  Real flameSpeed{};
  Real consumptionSpeed{};
  Real mismatch{};
  Real fuelBalance{};
  Real thickness{};
  Real peakTemperature{};
  Real flamePosition{};
  Real massFluxResidual{};
  Real sumDeviation{};
  Real minY{};
  Index totalSteps{};
  Index finalMarchSteps{};
  Index outerIterations{};
  bool steady{false};
  int status{-1};
  Real wall{};
};

Row solveOne(const Problem& problem, const cfd::physics::TemperatureProperty& conductivity,
             const PremixedFlameSettings& settings, const std::string& level) {
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

  const auto t0 = std::chrono::steady_clock::now();
  const PremixedFlameResult f = solvePremixedFlame(chemistry, conditions, settings);
  const Real wall = std::chrono::duration<Real>(std::chrono::steady_clock::now() - t0).count();

  Row r;
  r.level = level;
  r.flameSpeed = f.flameSpeed;
  r.consumptionSpeed = f.consumptionSpeed;
  r.mismatch = (f.flameSpeed != 0.0)
                   ? std::abs(f.consumptionSpeed - f.flameSpeed) / f.flameSpeed
                   : -1.0;
  r.fuelBalance = f.fuelBalanceResidual;
  r.thickness = f.flameThickness;
  r.peakTemperature = f.peakTemperature;
  r.flamePosition = f.flamePosition;
  r.massFluxResidual = f.massFluxResidual;
  r.sumDeviation = f.maxCompositionSumDeviation;
  r.minY = f.minMassFraction;
  r.totalSteps = f.totalTimeSteps;
  r.finalMarchSteps = f.finalMarchSteps;
  r.outerIterations = f.outerIterations;
  r.steady = f.steadyStateReached;
  r.status = static_cast<int>(f.status);
  r.wall = wall;
  return r;
}

void header(const char* title, const char* knob) {
  std::printf("\n=== SWEEP %s -- %s\n", title, knob);
  std::printf("%14s %10s %10s %9s %10s %9s %9s %10s %10s %7s %7s %8s\n", "level", "S_L cm/s",
              "S_c cm/s", "mismatch", "fuel bal", "delta mm", "x_f mm", "bracket", "|sumY-1|",
              "steps", "march", "wall s");
  std::fflush(stdout);
}

void printRow(const Row& r) {
  std::printf("%14s %10.5f %10.5f %9.5f %10.3e %9.4f %9.5f %10.3e %10.3e %7lld %7lld %8.1f%s\n",
              r.level.c_str(), r.flameSpeed * 100.0, r.consumptionSpeed * 100.0, r.mismatch,
              r.fuelBalance, r.thickness * 1000.0, r.flamePosition * 1000.0, r.massFluxResidual,
              r.sumDeviation, static_cast<long long>(r.totalSteps),
              static_cast<long long>(r.finalMarchSteps), r.wall,
              r.status == 0 ? "" : "   <-- NOT Converged");
  std::fflush(stdout);
}

// The fail-closed guard. A sweep whose levels come back bit-identical has not demonstrated
// insensitivity -- it has demonstrated that the knob is disconnected.
void closeSweep(const std::vector<Row>& rows) {
  if (rows.size() < 2) {
    std::printf("  *** SWEEP INVALID: fewer than two levels completed\n");
    std::fflush(stdout);
    return;
  }
  bool anyMoved = false;
  for (std::size_t i = 1; i < rows.size(); ++i) {
    if (rows[i].totalSteps != rows[0].totalSteps ||
        rows[i].flameSpeed != rows[0].flameSpeed ||
        rows[i].fuelBalance != rows[0].fuelBalance) {
      anyMoved = true;
    }
  }
  Real lo = rows[0].flameSpeed;
  Real hi = rows[0].flameSpeed;
  Real worstBalance = rows[0].fuelBalance;
  Real worstMismatch = rows[0].mismatch;
  for (const Row& r : rows) {
    lo = std::min(lo, r.flameSpeed);
    hi = std::max(hi, r.flameSpeed);
    worstBalance = std::max(worstBalance, r.fuelBalance);
    worstMismatch = std::max(worstMismatch, r.mismatch);
  }
  std::printf("  S_L spread (hi-lo)/lo = %.6f   worst fuel balance = %.4e   worst mismatch = %.5f\n",
              (lo != 0.0) ? (hi - lo) / lo : -1.0, worstBalance, worstMismatch);
  if (!anyMoved) {
    std::printf("  *** SWEEP INVALID: every level returned an identical solve -- the knob is\n");
    std::printf("      disconnected and this sweep demonstrates nothing about insensitivity.\n");
  } else {
    std::printf("  guard passed: the independent variable moved the solve.\n");
  }
  std::printf("  READ: a small S_L spread with a LARGE worst balance/mismatch is the fail-open\n");
  std::printf("        case -- the number is stable while one state is not a steady flame.\n");
  std::fflush(stdout);
}

}  // namespace

int main() {
  const Problem problem = makeProblem();
  const cfd::physics::ConstantProperty conductivity(kConductivity);

  std::printf("TD-005 -- flame-speed extraction: reproducibility and the state behind it\n");
  std::printf("fixture: 500 cells / 10 mm / anchor 0.35 / tolerance 1e-3 (the gated makeSettings)\n");
  std::printf("reference thresholds for reading only, NOT gate thresholds:\n");
  std::printf("  existing test bounds   S_L spread < 0.02   fuel balance < 0.05   mismatch < 0.01\n");
  std::printf("  G8 band 35.0-37.0 cm/s is +-2.8 %%, so findings 3.1 requires reproducibility << 2.8 %%\n");
  std::fflush(stdout);

  // ---- A. driftWindow. The knob whose 4.3 % dependence is the RECORDED blocker.
  {
    header("A", "driftWindow (recorded blocker: 4.3 % at windows 32 vs 64, PRE relaxationTimes)");
    std::vector<Row> rows;
    for (const Index window : {16, 32, 64}) {
      auto s = baseSettings();
      s.driftWindow = window;
      rows.push_back(solveOne(problem, conductivity, s, "window " + std::to_string(window)));
      printRow(rows.back());
    }
    closeSweep(rows);
  }

  // ---- B. anchorFraction. THE PLATEAU: the mechanism that selects the eigenvalue, and the
  //         criterion the suite cannot currently run at all.
  {
    header("B", "anchorFraction -- the cold-boundary PLATEAU, test times out at 900 s");
    std::vector<Row> rows;
    for (const Real fraction : {0.30, 0.40, 0.50}) {
      auto s = baseSettings();
      s.anchorFraction = fraction;
      char label[32];
      std::snprintf(label, sizeof(label), "anchor %.2f", fraction);
      rows.push_back(solveOne(problem, conductivity, s, label));
      printRow(rows.back());
    }
    closeSweep(rows);
  }

  // ---- C. relaxationTimesPerTrial. Swept by NO test, though the header requires it.
  {
    header("C", "relaxationTimesPerTrial -- the convergence requirement, swept by no test");
    std::vector<Row> rows;
    for (const Real times : {1.5, 3.0, 6.0}) {
      auto s = baseSettings();
      s.relaxationTimesPerTrial = times;
      char label[32];
      std::snprintf(label, sizeof(label), "relaxT/trial %.1f", times);
      rows.push_back(solveOne(problem, conductivity, s, label));
      printRow(rows.back());
    }
    closeSweep(rows);
  }

  // ---- D. tolerance. A PRECISION ladder, not an insensitivity sweep: S_L must converge and
  //         `bracket` must track the requested width.
  {
    header("D", "tolerance -- the bracket on mdot; S_L must CONVERGE as it tightens");
    std::vector<Row> rows;
    for (const Real tol : {1.0e-2, 1.0e-3, 1.0e-4}) {
      auto s = baseSettings();
      s.tolerance = tol;
      char label[32];
      std::snprintf(label, sizeof(label), "tol %.0e", tol);
      rows.push_back(solveOne(problem, conductivity, s, label));
      printRow(rows.back());
    }
    closeSweep(rows);
    std::printf("  NOTE: this sweep is NOT judged by a small spread. A bracket of 1e-2 is a\n");
    std::printf("        LOOSER measurement and is allowed to differ; what must hold is that the\n");
    std::printf("        reported `bracket` <= the requested tolerance and that successive\n");
    std::printf("        tightenings stop moving S_L.\n");
  }

  std::printf("\nDONE. This is a DRY-RUN for non-vacuity only. Thresholds are derived in GATE.md\n");
  std::printf("from each quantity's own floor, never read off these numbers.\n");
  return 0;
}
