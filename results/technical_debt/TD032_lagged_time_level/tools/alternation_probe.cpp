// TD-032 -- THE STEP-GAP ORDERING CRITERION. Gate instrument for C0, C1, C2 (and it records the
// raw numbers C3, C4, C5 and C7 are read from).
//
// WHAT IT MEASURES. `finalMarchSteps` is a hard ceiling on the final march budget, so setting it to
// N, N+1, ... N+4 stops the march at exactly those step counts and the reported field is the state
// AT that step. The solver is deterministic, so run N+1 reproduces run N's first N steps and the
// five runs are five CONSECUTIVE states. No production change, and no production diagnostic.
//
//     across = max over the 4 ONE-step pairs  of  max_c |T_{k+1}(c) - T_k(c)|
//     within = max over the 3 TWO-step pairs  of  max_c |T_{k+2}(c) - T_k(c)|
//     R      = across / within          CRITERION: R <= 1
//
// WHY R <= 1 IS A DERIVATION. For a sequence relaxing monotonically the successive increments share
// a sign, so a two-step difference is the sum of two same-signed one-step differences and is at
// least as large as either: within >= across. R > 1 requires the one-step difference to EXCEED the
// two-step difference, which is possible only if the sequence is ordered by PARITY rather than by
// step gap. The threshold is the sign of that inequality, not a chosen constant, and it bounds a
// dimensionless ratio so it needs no scaling with mesh, dt or state.
//
// THE VACUITY BRANCH, fixed in GATE.md before this was run and NOT selectable afterwards. If
// within <= 1e-9 K the ratio is a quotient of round-off; R is then NOT evaluated and the criterion
// instead requires across <= 1e-9 K -- a field stationary to round-off, a strictly stronger pass.
// Round-off floor: eps * T_scale * sqrt(nc) = 2.2e-16 * 1800 * 22 = 8.8e-12 K, so 1e-9 K sits two
// orders above it.
//
// THE EXIT CODE IS NOT THE VERDICT. On the UNCHANGED tree C1 and C2 are EXPECTED to fail -- that is
// the gate's fail-direction non-vacuity and its C8 negative control. A nonzero exit here means the
// PROBE malfunctioned (C0 failed, or a solve did not converge), never that a criterion failed.
#include <algorithm>
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

// ---- the fixture, identical to TD031/logs/05 so the baseline is comparable
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
constexpr Index kMaxStepsPerTrial = 80000;

// ---- the frozen thresholds
constexpr Real kRatioBound = 1.0;        // C1, C2
constexpr Real kVacuityFloor = 1.0e-9;   // K -- below this the ratio carries no information
constexpr Real kResidualBound = 1.0e-3;  // C3

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

// ---------------------------------------------------------------------------------------------
// THE CRITERION, as one function over a list of consecutive fields. C0 applies it to synthetic
// sequences, C1 and C2 to the solver's. ONE implementation, so the self-test tests what is used.
// ---------------------------------------------------------------------------------------------
struct Ordering {
  Real across{};
  Real within{};
  Real ratio{};
  Index acrossCell{};
  Index withinCell{};
  bool vacuous{};
  bool pass{};
};

Real fieldDistance(const std::vector<Real>& a, const std::vector<Real>& b, Index& worst) {
  Real d = 0.0;
  worst = 0;
  const std::size_t n = std::min(a.size(), b.size());
  for (std::size_t c = 0; c < n; ++c) {
    const Real diff = std::abs(a[c] - b[c]);
    if (diff > d) {
      d = diff;
      worst = static_cast<Index>(c);
    }
  }
  return d;
}

Ordering evaluate(const std::vector<std::vector<Real>>& f) {
  Ordering o;
  for (std::size_t k = 0; k + 1 < f.size(); ++k) {
    Index w = 0;
    const Real d = fieldDistance(f[k], f[k + 1], w);
    if (d > o.across) {
      o.across = d;
      o.acrossCell = w;
    }
  }
  for (std::size_t k = 0; k + 2 < f.size(); ++k) {
    Index w = 0;
    const Real d = fieldDistance(f[k], f[k + 2], w);
    if (d > o.within) {
      o.within = d;
      o.withinCell = w;
    }
  }
  o.vacuous = (o.within <= kVacuityFloor);
  if (o.vacuous) {
    o.ratio = -1.0;                        // deliberately not computed
    o.pass = (o.across <= kVacuityFloor);  // the strictly stronger requirement
  } else {
    o.ratio = o.across / o.within;
    o.pass = (o.ratio <= kRatioBound);
  }
  return o;
}

void report(const char* label, const Ordering& o) {
  std::printf("    %-10s across %.6e K (cell %lld)   within %.6e K (cell %lld)\n", label, o.across,
              static_cast<long long>(o.acrossCell), o.within,
              static_cast<long long>(o.withinCell));
  if (o.vacuous) {
    std::printf("    %-10s within <= %.0e K: THE RATIO IS VACUOUS and is NOT evaluated.\n", "",
                kVacuityFloor);
    std::printf("    %-10s the gate's fallback applies: across <= %.0e K -> %s\n", "",
                kVacuityFloor, o.pass ? "PASS" : "FAIL");
  } else {
    std::printf("    %-10s R = across/within = %.6e   against the bound %.1f -> %s\n", "",
                o.ratio, kRatioBound, o.pass ? "PASS" : "FAIL");
  }
}

struct Run {
  std::vector<Real> temperature;
  Real flameSpeed{};
  Real sumDeviation{};
  Real temperatureResidual{};
  Real worstStateChange{};
  Real flamePosition{};
  Real wall{};
  Index marchSteps{};
  bool ok{};
};

PremixedFlameSettings makeSettings(Index cells, Real relaxT, Index budget) {
  PremixedFlameSettings s;
  s.cells = cells;
  s.domainLength = kDomain;
  s.anchorFraction = kAnchorFraction;
  s.tolerance = 1.0e-3;
  s.maxOuterIterations = 40;
  s.driftSettleTolerance = 0.05;
  s.driftWindow = 32;
  s.maximumCellDiffusionNumber = 5.0;
  s.finalMarchRelaxationTimes = relaxT;
  s.maximumStepsPerTrial = kMaxStepsPerTrial;
  s.finalMarchSteps = budget;
  return s;
}

}  // namespace

int main() {
  std::printf("TD-032 -- THE STEP-GAP ORDERING CRITERION\n\n");
  std::printf("  criterion   R = across/within <= %.1f, or (within <= %.0e K) -> across <= %.0e K\n",
              kRatioBound, kVacuityFloor, kVacuityFloor);
  std::printf("  the exit code is NOT the verdict: on the unchanged tree C1 and C2 are EXPECTED\n");
  std::printf("  to FAIL, which is the gate's fail-direction non-vacuity and its C8 control.\n\n");

  int probeFailures = 0;

  // =============================================================== C0 -- instrument self-test
  std::printf("=== C0  INSTRUMENT SELF-TEST (no solver code at all)\n");
  {
    // (a) a monotone relaxation -- increments share a sign, so within >= across
    std::vector<std::vector<Real>> mono;
    for (int k = 0; k < 5; ++k) {
      const Real t = 300.0 + 1500.0 * std::exp(-static_cast<Real>(k) / 2.0);
      mono.push_back(std::vector<Real>{t, t * 0.5, t * 0.25});
    }
    const Ordering a = evaluate(mono);
    std::printf("  (a) monotone relaxation, T_k = 300 + 1500 exp(-k/2)\n");
    report("", a);
    const bool aOk = a.pass && !a.vacuous;
    std::printf("      must PASS with a MEANINGFUL ratio -> %s\n", aOk ? "ok" : "*** BROKEN ***");
    if (!aOk) {
      ++probeFailures;
    }

    // (b) a parity alternation with a within-branch drift -- the structure TD031/logs/05 measured
    std::vector<std::vector<Real>> alt;
    for (int k = 0; k < 5; ++k) {
      const Real base = (k % 2 == 0) ? 300.0 : 1800.0;
      const Real t = base - 0.001 * static_cast<Real>(k);
      alt.push_back(std::vector<Real>{t, t, t});
    }
    const Ordering b = evaluate(alt);
    std::printf("  (b) parity alternation with a within-branch drift of 0.001 K per step\n");
    report("", b);
    const bool bOk = !b.pass && !b.vacuous && b.ratio > kRatioBound;
    std::printf("      must FAIL on the RATIO -> %s\n", bOk ? "ok" : "*** BROKEN ***");
    if (!bOk) {
      ++probeFailures;
    }

    // (c) a PURE alternation, within == 0 -- must fail through the VACUITY branch, not the ratio
    std::vector<std::vector<Real>> pure;
    for (int k = 0; k < 5; ++k) {
      const Real t = (k % 2 == 0) ? 300.0 : 1800.0;
      pure.push_back(std::vector<Real>{t, t, t});
    }
    const Ordering c = evaluate(pure);
    std::printf("  (c) a PURE alternation, within == 0 exactly\n");
    report("", c);
    const bool cOk = c.vacuous && !c.pass;
    std::printf("      must be VACUOUS and still FAIL via the fallback -> %s\n",
                cOk ? "ok" : "*** BROKEN ***");
    if (!cOk) {
      ++probeFailures;
    }

    // (d) a stationary field -- vacuous AND passing, which is the only legitimate vacuous PASS
    std::vector<std::vector<Real>> flat(5, std::vector<Real>{1800.0, 1200.0, 300.0});
    const Ordering d = evaluate(flat);
    std::printf("  (d) a STATIONARY field, across == within == 0\n");
    report("", d);
    const bool dOk = d.vacuous && d.pass;
    std::printf("      must be VACUOUS and PASS via the fallback -> %s\n",
                dOk ? "ok" : "*** BROKEN ***");
    if (!dOk) {
      ++probeFailures;
    }
  }
  std::printf("\n  C0 VERDICT: %s\n\n", probeFailures == 0 ? "PASS" : "*** FAIL -- STOP ***");
  std::fflush(stdout);
  if (probeFailures != 0) {
    std::printf("The instrument is wrong. Per GATE.md section 3, stop: nothing below means\n");
    std::printf("anything until C0 passes.\n");
    return 1;
  }

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

  const auto solve = [&](Index cells, Real relaxT, Index budget) -> Run {
    Run r;
    const auto t0 = std::chrono::steady_clock::now();
    const PremixedFlameResult f =
        solvePremixedFlame(chemistry, conditions, makeSettings(cells, relaxT, budget));
    r.wall = std::chrono::duration<Real>(std::chrono::steady_clock::now() - t0).count();
    r.temperature = f.temperature;
    r.flameSpeed = f.flameSpeed;
    r.sumDeviation = f.maxCompositionSumDeviation;
    r.temperatureResidual = f.temperatureResidual;
    r.worstStateChange = f.worstStateChange;
    r.flamePosition = f.flamePosition;
    r.marchSteps = f.finalMarchSteps;
    r.ok = (f.status == PremixedFlameStatus::Converged) && !f.temperature.empty();
    return r;
  };

  // ======================================================= FIXTURE DETERMINATION (not a criterion)
  //
  // C2's five budgets must be FIXED numbers in the frozen gate, so the 250-cell fixture's derived
  // budget is read off here with the ceiling raised out of the way. This is fixture setup, not a
  // measurement of any criterion, and it is labelled as such in the log.
  std::printf("=== FIXTURE DETERMINATION for C2 (not a criterion)\n");
  std::printf("    250 cells, cap 5, relaxT 18, ceiling raised to 1e6 so the DERIVED budget wins\n");
  std::fflush(stdout);
  const Run determine = solve(250, 18.0, 1000000);
  if (!determine.ok) {
    std::printf("    *** the determination solve did not converge. PROBE DEFECT or a solver\n");
    std::printf("        failure -- either way nothing below can be measured.\n");
    return 1;
  }
  const Index base250 = determine.marchSteps;
  std::printf("    derived final march budget at 250 cells: %lld steps  (%.0f s)\n",
              static_cast<long long>(base250), determine.wall);
  std::printf("    C2 will use %lld .. %lld\n\n", static_cast<long long>(base250 - 4),
              static_cast<long long>(base250));
  std::fflush(stdout);

  // ======================================================= C1 and C2
  struct Fixture {
    const char* name;
    Index cells;
    Real relaxT;
    Index first;
  };
  const Fixture fixtures[] = {{"C1  500 cells / cap 5 / relaxT 18", 500, 18.0, 30945},
                              {"C2  250 cells / cap 5 / relaxT 18", 250, 18.0, base250 - 4}};

  for (const Fixture& fx : fixtures) {
    std::printf("=== %s\n", fx.name);
    std::printf("    %10s %10s %14s %16s %14s %14s %10s\n", "budget", "marchStp", "S_L cm/s",
                "T resid", "sum dev", "front mm", "wall s");
    std::fflush(stdout);

    std::vector<std::vector<Real>> fields;
    std::vector<Real> walls;
    Run last;
    bool allOk = true;
    for (Index b = fx.first; b < fx.first + 5; ++b) {
      const Run r = solve(fx.cells, fx.relaxT, b);
      if (!r.ok) {
        std::printf("    %10lld   *** the solve did not converge -- PROBE/SOLVER FAILURE\n",
                    static_cast<long long>(b));
        allOk = false;
        break;
      }
      std::printf("    %10lld %10lld %14.5f %16.6e %14.6e %14.6f %10.0f\n",
                  static_cast<long long>(b), static_cast<long long>(r.marchSteps),
                  r.flameSpeed * 100.0, r.temperatureResidual, r.sumDeviation,
                  r.flamePosition * 1000.0, r.wall);
      std::fflush(stdout);
      fields.push_back(r.temperature);
      walls.push_back(r.wall);
      last = r;
    }
    if (!allOk) {
      ++probeFailures;
      std::printf("\n");
      continue;
    }

    std::printf("\n");
    const Ordering o = evaluate(fields);
    report("", o);

    // ---- the raw numbers C3, C4, C5 and C7 are read from, at this fixture's LAST budget
    std::vector<Real> post(walls.begin() + 1, walls.end());
    std::sort(post.begin(), post.end());
    const Real median = 0.5 * (post[1] + post[2]);
    std::printf("\n    FOR C3/C4/C5/C7, at the last budget:\n");
    std::printf("      temperatureResidual  %.6e   against C3's bound %.0e -> %s\n",
                last.temperatureResidual, kResidualBound,
                last.temperatureResidual <= kResidualBound ? "PASS" : "FAIL");
    std::printf("      worstStateChange     %.6e   (max over every judged step)\n",
                last.worstStateChange);
    std::printf("      maxCompositionSumDeviation  %.6e   <- C4's baseline, direction only\n",
                last.sumDeviation);
    std::printf("      S_L                  %.6f cm/s          <- C5's baseline\n",
                last.flameSpeed * 100.0);
    std::printf("      post-warm-up wall times %.0f %.0f %.0f %.0f s, MEDIAN %.1f s  <- C7\n",
                post[0], post[1], post[2], post[3], median);
    std::printf("\n");
    std::fflush(stdout);
  }

  std::printf("=== HOW TO READ THIS RUN\n");
  std::printf("  On the UNCHANGED tree, C1 and C2 FAILING is the REQUIRED result: it is the\n");
  std::printf("  fail-direction non-vacuity GATE.md section 2 demands and it is the C8 negative\n");
  std::printf("  control. C0 passing with a MEANINGFUL ratio in case (a) is what makes the pass\n");
  std::printf("  direction reachable. If C1 or C2 PASSED here, the gate would be vacuous and must\n");
  std::printf("  not be frozen.\n");
  return probeFailures == 0 ? 0 : 1;
}
