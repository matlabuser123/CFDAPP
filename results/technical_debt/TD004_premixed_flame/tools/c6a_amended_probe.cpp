// TD-004 GATE_A6 -- THE POST-FREEZE VERIFICATION of C6a-bal and C6a-ord.
//
// CLAUDE.md §5.3 step 6: "Rerun fresh. Reuse no earlier result." dry_run_A6.md evaluated both
// clauses on logs/A5_01's post-fix numbers; those are the DRY-RUN and are deliberately not reused.
// This probe re-solves the family.
//
// THE FROZEN CRITERIA, quoted from GATE_A6.md -- thresholds are NOT restated from memory:
//
//   C6a-bal   fuel balance(cells) < 0.02 * (500 / cells)^2
//             derived from the scheme's FORMAL order h^2, not from the measured 2.17-2.41, so the
//             bound is conservative. The 500-cell value stays 0.02, C3's reference threshold.
//
//   C6a-ord   (a) both successive differences must exceed 4 LATTICE QUANTA, where one quantum is
//                 (2^(1/1024) - 1) of S_L at the middle level (TD-043). Below that the order is
//                 UNRESOLVED, which is a STOP and NOT a pass.
//             (b) when resolved, the order's FULL interval must lie within [1.5, 2.5]:
//                     p_lo = log2((n1-1)/(n2+1))      p_hi = log2((n1+1)/(n2-1))
//
//   C6a's four ADMISSIBILITY clauses are carried over verbatim from GATE.md and are also checked:
//   every level converged, every level > 4 cells of front, differences shrink, no sign change.
//
// THE FIXTURE is fixture_probe.cpp's, exactly: 500-cell family {63, 125, 250, 500}, anchorFraction
// 0.35, tolerance 1e-3, maxOuterIterations 40, driftSettleTolerance 0.05, maximumStepsPerTrial
// 20000, no pinned dt, no finalMarchRelaxationTimes override. The gate's triplet is {125, 250, 500};
// 63 is solved because the admissibility and difference-shrinking clauses are defined over the
// family fixture_probe reports, and because dry_run_A6.md §4.2 found {63,125,250} fails on BOTH
// libraries -- so its value is worth recording rather than assuming.
//
// WHAT IS EXPECTED, from the dry-run, stated so a deviation is visible rather than absorbed:
//   C6a-bal   PASS at all three levels (0.1505 / 0.0283 / 0.0063 against 0.3200 / 0.0800 / 0.0200)
//   C6a-ord   UNRESOLVED -- the fine difference was 2.0 quanta, below the 4-quantum floor
// If this fresh run disagrees with either, the fixture is not reproducible and that is the finding.
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

// the frozen constants
constexpr Real kBalanceAt500 = 0.02;      // C3's reference threshold, unchanged
constexpr Index kReferenceCells = 500;    // the level the h^2 scaling is anchored at
constexpr Real kQuantumFloor = 4.0;       // C6a-ord (a)
constexpr Real kBandLo = 1.5;             // C6a-ord (b), UNCHANGED from C6a
constexpr Real kBandHi = 2.5;
constexpr Real kResolutionGuard = 4.0;    // C6a admissibility: > 4 cells of front

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

struct Level {
  Index cells{};
  Real sL{};
  Real balance{};
  Real resCells{};
  bool converged{false};
  Real wall{};
  bool ok{false};
};

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

  const Real quantumRel = std::pow(2.0, 1.0 / 1024.0) - 1.0;

  std::printf("TD-004 GATE_A6 -- POST-FREEZE VERIFICATION of C6a-bal and C6a-ord\n\n");
  std::printf("  lattice quantum (relative)  %.9e   = %.6f %% of S_L   [TD-043]\n", quantumRel,
              quantumRel * 100.0);
  std::printf("  C6a-bal bound  0.02 * (500/cells)^2 :  125 -> %.4f   250 -> %.4f   500 -> %.4f\n",
              kBalanceAt500 * 16.0, kBalanceAt500 * 4.0, kBalanceAt500);
  std::printf("  C6a-ord  floor %.0f quanta per difference; band [%.1f, %.1f] on the FULL interval\n\n",
              kQuantumFloor, kBandLo, kBandHi);
  std::printf("%7s %11s %12s %10s %11s %9s\n", "cells", "S_L cm/s", "fuel bal", "res cells",
              "converged", "wall s");
  std::fflush(stdout);

  std::vector<Level> levels;
  for (const Index cells : {63, 125, 250, 500}) {
    PremixedFlameSettings s;
    s.cells = cells;
    s.domainLength = kDomain;
    s.anchorFraction = kAnchorFraction;
    s.tolerance = 1.0e-3;
    s.maxOuterIterations = 40;
    s.driftSettleTolerance = 0.05;
    s.maximumStepsPerTrial = 20000;

    Level lv;
    lv.cells = cells;
    const auto t0 = std::chrono::steady_clock::now();
    PremixedFlameResult f;
    try {
      f = solvePremixedFlame(chemistry, conditions, s);
    } catch (const std::exception& e) {
      std::printf("%7lld  THREW: %s\n", static_cast<long long>(cells), e.what());
      std::fflush(stdout);
      levels.push_back(lv);
      continue;
    }
    lv.wall = std::chrono::duration<Real>(std::chrono::steady_clock::now() - t0).count();
    lv.sL = f.flameSpeed * 100.0;
    lv.balance = f.fuelBalanceResidual;
    lv.resCells = f.flameThickness / (kDomain / static_cast<Real>(cells));
    lv.converged = (f.status == PremixedFlameStatus::Converged);
    lv.ok = true;
    levels.push_back(lv);

    std::printf("%7lld %11.5f %12.4e %10.2f %11s %9.0f\n", static_cast<long long>(cells), lv.sL,
                lv.balance, lv.resCells, lv.converged ? "yes" : "NO", lv.wall);
    std::fflush(stdout);
  }

  // the gate's triplet is {125, 250, 500} -- indices 1, 2, 3
  if (levels.size() < 4 || !levels[1].ok || !levels[2].ok || !levels[3].ok) {
    std::printf("\n*** a level of the {125,250,500} triplet did not complete. C6a cannot be\n");
    std::printf("    evaluated and no verdict is issued.\n");
    return 0;
  }
  const Level& a = levels[1];
  const Level& b = levels[2];
  const Level& c = levels[3];

  std::printf("\n=== C6a ADMISSIBILITY (carried over verbatim from GATE.md)\n\n");
  const bool convAll = a.converged && b.converged && c.converged;
  const bool resAll = (a.resCells > kResolutionGuard) && (b.resCells > kResolutionGuard) &&
                      (c.resCells > kResolutionGuard);
  const Real d1 = b.sL - a.sL;
  const Real d2 = c.sL - b.sL;
  const bool shrink = std::abs(d2) < std::abs(d1);
  const bool sameSign = (d1 > 0.0) == (d2 > 0.0);
  std::printf("  every level converged                         %s\n", convAll ? "yes" : "NO");
  std::printf("  every level > %.0f cells of front               %s  (%.2f, %.2f, %.2f)\n",
              kResolutionGuard, resAll ? "yes" : "NO", a.resCells, b.resCells, c.resCells);
  std::printf("  differences shrink  |%.4e| < |%.4e|   %s\n", d2, d1, shrink ? "yes" : "NO");
  std::printf("  no sign change                                %s\n", sameSign ? "yes" : "NO");
  const bool admissible = convAll && resAll && shrink && sameSign;
  std::printf("  -> %s\n", admissible ? "ADMISSIBLE" : "NOT ADMISSIBLE");

  std::printf("\n=== C6a-bal   fuel balance < 0.02 * (500/cells)^2\n\n");
  bool bal = true;
  for (const Level* lv : {&a, &b, &c}) {
    const Real bound =
        kBalanceAt500 * std::pow(static_cast<Real>(kReferenceCells) / static_cast<Real>(lv->cells),
                                 2.0);
    const bool ok = lv->balance < bound;
    bal = bal && ok;
    std::printf("  %3lld cells   %.4e  <  %.4f   %s\n", static_cast<long long>(lv->cells),
                lv->balance, bound, ok ? "PASS" : "FAIL");
  }
  std::printf("  -> C6a-bal %s\n", bal ? "PASS" : "FAIL");

  std::printf("\n=== C6a-ord   resolution precondition, then the full interval\n\n");
  const Real q = b.sL * quantumRel;
  const Real n1 = d1 / q;
  const Real n2 = d2 / q;
  std::printf("  quantum at the middle level   %.6e cm/s\n", q);
  std::printf("  differences in quanta         n1 = %.2f   n2 = %.2f   (floor %.0f)\n", n1, n2,
              kQuantumFloor);
  const bool resolved = (std::abs(n1) >= kQuantumFloor && std::abs(n2) >= kQuantumFloor);
  if (!resolved) {
    std::printf("  -> C6a-ord UNRESOLVED. A difference is below the %.0f-quantum floor, so the\n",
                kQuantumFloor);
    std::printf("     observed order cannot be determined at this tolerance. Per GATE_A6.md §3(a)\n");
    std::printf("     this is a STOP and NOT a pass.\n");
  } else {
    const Real p = std::log2(d1 / d2);
    const Real plo = std::log2((n1 - 1.0) / (n2 + 1.0));
    const Real phi = std::log2((n1 + 1.0) / (n2 - 1.0));
    const bool inband = (plo >= kBandLo && phi <= kBandHi);
    std::printf("  p = %.3f   full interval (%.2f, %.2f)   band [%.1f, %.1f]\n", p, plo, phi,
                kBandLo, kBandHi);
    std::printf("  -> C6a-ord %s\n", inband ? "PASS" : "FAIL");
  }

  std::printf("\n=== C6a OVERALL, in GATE_A6's order\n\n");
  std::printf("  admissibility  %s\n", admissible ? "ADMISSIBLE" : "NOT ADMISSIBLE");
  std::printf("  C6a-bal        %s\n", bal ? "PASS" : "FAIL");
  std::printf("  C6a-ord        %s\n", resolved ? "evaluated above" : "UNRESOLVED");
  if (admissible && bal && !resolved) {
    std::printf("\n  C6a does NOT pass: its order clause is UNRESOLVED at this tolerance. The gate\n");
    std::printf("  STOPS at C6a, so C6b and C7 are NOT evaluated. Resolving it needs a finer\n");
    std::printf("  settings.tolerance (TD-043), which is a production change outside this gate.\n");
  }

  std::printf("\n=== THE {63, 125, 250} TRIPLET, recorded not gated\n\n");
  if (levels[0].ok) {
    const Real e1 = levels[1].sL - levels[0].sL;
    const Real e2 = levels[2].sL - levels[1].sL;
    const Real qq = levels[1].sL * quantumRel;
    std::printf("  diffs %+.4e %+.4e cm/s = %.1f and %.1f quanta\n", e1, e2, e1 / qq, e2 / qq);
    if ((e1 > 0.0) == (e2 > 0.0) && e2 != 0.0) {
      std::printf("  p = %.3f   -- dry_run_A6.md §4.2 found this triplet fails on BOTH libraries,\n",
                  std::log2(e1 / e2));
      std::printf("  so the asymptotic range for this family starts ABOVE 63 cells.\n");
    }
  }
  return 0;
}
