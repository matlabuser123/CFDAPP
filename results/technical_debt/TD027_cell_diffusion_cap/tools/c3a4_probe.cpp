// TD-027 AMENDMENT A4 -- THE DRY-RUN of C3-A4, on the UNCHANGED baseline.
//
// CLAUDE.md §5.3: an amendment is frozen only AFTER its own dry-run and non-vacuity check. This
// probe is that dry-run. It changes no production file and no test; it is a read-only measurement
// built from a copy of c2a2_probe.cpp's problem setup, which is NOT modified (that file is C2-A2's
// evidence).
//
// WHAT C3 GOT WRONG, and what C3-A4 changes (audit_C3.md §3, amendment_A4_proposal.md §1):
//
//   C3:    E / (sqrt(1+tolerance) - 1)  <= 2
//          E is a difference of S_L values, each a bisection bracket CENTRE resolvable only in
//          units of the terminal bracket q; and the denominator is set by the SAME tolerance that
//          sets q. Since q is in (tol/2, tol] and sqrt(1+t)-1 ~ t/2, the ratio q/B is ALWAYS in
//          (1,2], so C3 could only return m*(q/B) for integer m. Its threshold of 2 sat in the
//          unreachable gap between m=1 (1.3546) and m=2 (2.7092), and its lowest score, 0, was a
//          ladder that had resolved nothing.
//
//   C3-A4: E / massFluxResidual <= 2, with E measured at a FINER tolerance.
//          B is what the solver PUBLISHES, measured, not the sqrt bound -- the two differ by the
//          q/B factor above, and C3 judged a claim the solver does not make.
//          E is measured at tol_meas = 1e-4 so that q = 8.46e-05 and E ~ 2.03e-03 is resolved to
//          24 quanta (~4 %) instead of 3. Numerator and denominator are then independent and the
//          ratio is CONTINUOUS, so the threshold is reachable as a boundary.
//
//   THE THRESHOLD OF 2 IS UNCHANGED. So is maximumCellDiffusionNumber (5.0) and the production
//   tolerance (1e-4). §5.3 forbids re-deriving a threshold because a measurement moved, and this
//   amendment does not: it replaces an instrument shown unable to express the threshold.
//
// PRE-REGISTERED PREDICTIONS (amendment_A4_proposal.md §3). A deviation is a RESULT, not something
// to absorb:
//
//     E at tol_meas                      ~ 0.2031 %   (must agree with C2-A2's 0.203139 %)
//     B production tol 1e-4              8.461627e-05  ->  ratio ~24.01  FAIL
//     B fixture    tol 1e-3              6.771307e-04  ->  ratio ~ 3.00  FAIL  (= A3's recorded 3.00)
//     B loose      tol 5e-3              2.711275e-03  ->  ratio ~ 0.75  PASS  <- the PASS control
//
// NON-VACUITY. The loose configuration must PASS and the two shipped ones must FAIL, from the same
// instrument in the same run, so it is shown to span both verdicts. The cap-20 / cap-10 rejections
// C3 was derived on are cross-checked arithmetically against their recorded errors at the end.
//
// FAIL-CLOSED. Every trial's `settled` flag is checked; an unsettled trial VOIDS the run and no
// verdict is issued. A non-Converged status at the finer tolerance also voids it -- proposal §6
// names that as a stop condition, because it would mean the extra bisections hit a cap.
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
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

// Identical to c2a2_probe.cpp, so the ladder is the same problem.
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

constexpr Real kBaseDt = 9.4243e-06;    // cell diffusion number 5.0 -- the shipped cap
constexpr Index kBaseTrialCap = 80000;  // scaled as 1/dt
constexpr Real kTolMeas = 1.0e-4;       // §2.1: loosest tolerance giving q <= E/20
constexpr Real kThreshold = 2.0;        // C3's constant, UNCHANGED

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

struct Run {
  bool ok{false};
  bool unsettled{false};
  Real speedCmS{};
  Real bracket{};  // result.massFluxResidual -- the PUBLISHED precision
  int status{};
  long long bisections{};
  Real wall{};
};

PremixedFlameSettings baseSettings(Real dt, Index trialCap, Real tol) {
  PremixedFlameSettings s;
  s.cells = kCells;
  s.domainLength = kDomain;
  s.anchorFraction = kAnchorFraction;
  s.tolerance = tol;
  s.maxOuterIterations = 40;
  s.driftSettleTolerance = 0.05;
  s.driftWindow = 32;
  s.timeStep = dt;
  s.finalMarchRelaxationTimes = 10.0;
  s.maximumStepsPerTrial = trialCap;
  return s;
}

Run solveOne(const PremixedFlameChemistry& chem, const PremixedFlameConditions& cond,
             const PremixedFlameSettings& s) {
  Run r;
  const auto t0 = std::chrono::steady_clock::now();
  PremixedFlameResult f;
  try {
    f = solvePremixedFlame(chem, cond, s);
  } catch (const std::exception& e) {
    std::printf("      THREW: %s\n", e.what());
    return r;
  }
  r.wall = std::chrono::duration<Real>(std::chrono::steady_clock::now() - t0).count();
  for (const PremixedFlameTrial& t : f.trials) {
    if (t.measured && !t.settled) {
      r.unsettled = true;
    }
  }
  r.speedCmS = f.flameSpeed * 100.0;
  r.bracket = f.massFluxResidual;
  r.status = static_cast<int>(f.status);
  r.bisections = static_cast<long long>(f.outerIterations);
  r.ok = (f.status == PremixedFlameStatus::Converged);
  return r;
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

  std::printf("TD-027 AMENDMENT A4 -- DRY-RUN of C3-A4 on the unchanged baseline\n\n");
  std::printf("  C3-A4:  E / massFluxResidual <= %.1f     (threshold UNCHANGED from C3)\n",
              kThreshold);
  std::printf("  E measured on a 4-level dt ladder at tol_meas = %.0e (q = 8.461627e-05),\n",
              kTolMeas);
  std::printf("  so E ~ 2.03e-03 is resolved to ~24 quanta instead of C3's 3.\n\n");
  std::printf("=== PART 1: the dt ladder at tol_meas, which measures E\n\n");
  std::printf("%6s %14s %11s %12s %14s %7s %9s\n", "level", "dt", "trial cap", "S_L cm/s",
              "published B", "bisect", "wall s");
  std::fflush(stdout);

  std::vector<Real> speeds;
  bool voided = false;
  for (int k = 0; k < 4; ++k) {
    const Real factor = std::pow(2.0, static_cast<Real>(k));
    const Real dt = kBaseDt / factor;
    const Index trialCap = static_cast<Index>(static_cast<Real>(kBaseTrialCap) * factor);
    const Run r = solveOne(chemistry, conditions, baseSettings(dt, trialCap, kTolMeas));
    std::printf("%6d %14.6e %11lld %12.6f %14.6e %7lld %9.0f%s%s\n", k, dt,
                static_cast<long long>(trialCap), r.speedCmS, r.bracket, r.bisections, r.wall,
                r.unsettled ? "  <- UNSETTLED" : "", r.ok ? "" : "  <- NOT CONVERGED");
    std::fflush(stdout);
    if (!r.ok || r.unsettled) {
      voided = true;
      break;
    }
    speeds.push_back(r.speedCmS);
  }

  if (voided || speeds.size() < 4) {
    std::printf("\n*** RUN VOIDED (proposal §6). Either a trial failed to settle or a level did\n");
    std::printf("    not converge at the finer tolerance -- which would mean the extra bisections\n");
    std::printf("    hit a cap. NO VERDICT is issued; this is the fail-closed path.\n");
    return 0;
  }

  const Real finest = speeds[3];
  const Real measured = std::abs(speeds[0] - finest) / finest * 100.0;
  const Real tail = std::abs(speeds[3] - speeds[2]) / finest * 100.0;
  const Real E = measured + tail;

  std::printf("\n  E = measured %.6f %% + tail %.6f %% = %.6f %%\n", measured, tail, E);
  std::printf("  C2-A2 recorded 0.1353 %% + 0.0677 %% = 0.203139 %% at tol 1e-3.\n");
  std::printf("  AGREEMENT CHECK (proposal §6): |E - 0.203139| = %.6f %% %s\n",
              std::abs(E - 0.203139),
              (std::abs(E - 0.203139) <= 0.05) ? "-- consistent" : "-- *** DEVIATION, see §6");

  std::printf("\n=== PART 2: B, the bracket each configuration PUBLISHES at the default dt\n\n");
  std::printf("%-26s %10s %14s %9s %8s\n", "configuration", "tolerance", "published B", "ratio",
              "verdict");

  struct Cfg {
    const char* name;
    Real tol;
  };
  // Labels corrected before the freeze: commit 7c2056a changed the shipped default from 1.0e-4 to
  // 1.0e-3, so the SHIPPED configuration and the gated fixture are now the SAME tolerance and the
  // gate's verdict is unambiguous. 1.0e-4 is retained as the FORMER default, because it is also
  // tol_meas and its bracket is what E is resolved against.
  const Cfg cfgs[] = {{"former default (pre-7c2056a)", 1.0e-4},
                      {"SHIPPED default + fixture", 1.0e-3},
                      {"loose -- the PASS control", 5.0e-3}};

  int passes = 0;
  int fails = 0;
  for (const Cfg& c : cfgs) {
    const Run r = solveOne(chemistry, conditions, baseSettings(kBaseDt, kBaseTrialCap, c.tol));
    if (!r.ok || r.unsettled) {
      std::printf("%-26s %10.0e   VOIDED (status %d%s)\n", c.name, c.tol, r.status,
                  r.unsettled ? ", unsettled" : "");
      std::fflush(stdout);
      continue;
    }
    const Real ratio = E / 100.0 / r.bracket;  // E is in %, bracket is relative
    const bool pass = ratio <= kThreshold;
    if (pass) {
      ++passes;
    } else {
      ++fails;
    }
    std::printf("%-26s %10.0e %14.6e %9.3f %8s\n", c.name, c.tol, r.bracket, ratio,
                pass ? "PASS" : "FAIL");
    std::fflush(stdout);
  }

  std::printf("\n=== PART 3: NON-VACUITY\n\n");
  std::printf("  the instrument returned %d PASS and %d FAIL in this run.  %s\n", passes, fails,
              (passes >= 1 && fails >= 1)
                  ? "NON-VACUOUS: it spans both verdicts"
                  : "*** VACUOUS -- the instrument did not exercise both outcomes");
  std::printf("\n  cross-check against the two caps C3 was DERIVED on, using their recorded\n");
  std::printf("  errors and the fixture's published bracket 6.771307e-04:\n");
  const Real bFixture = 6.771307e-04;
  const Real recorded[][2] = {{20.0, 3.99}, {10.0, 0.422}, {5.0, 0.203139}};
  for (const auto& row : recorded) {
    const Real ratio = row[1] / 100.0 / bFixture;
    std::printf("    cap %-4.1f  E = %7.4f %%   ratio %8.3f   %s\n", row[0], row[1], ratio,
                (ratio <= kThreshold) ? "PASS" : "FAIL");
  }
  std::printf("\n  C3-A4 must still reject cap 20 and cap 10 -- the rejections C3 was built to\n");
  std::printf("  make. If either passes above, A4 is NOT a replacement for C3 and is withdrawn.\n");
  return 0;
}
