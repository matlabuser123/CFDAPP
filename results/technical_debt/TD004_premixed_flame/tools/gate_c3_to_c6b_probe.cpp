// TD-004 -- THE FROZEN GATE FROM C3, in frozen order, stopping at the first failure.
//
// The original verification STOPPED AT C2, so C3-C5 were measured but C6a, C6b and C7 were never
// reached. C2 is now amended (C2-A1) and its clauses are satisfied, so the gate resumes at C3.
// This probe covers C3, C4, C5 and C6b, which all read from solves at relaxT 10 / 30 / 90.
// C6, C6a and C7 are separate runs and are NOT attempted here: the gate stops at the first
// failure, so spending hours on them before C3-C5 pass would be wasted.
//
// THE CRITERIA, QUOTED FROM GATE.md -- thresholds are NOT restated from memory:
//
//   C3  the balance closes at the reference configuration
//       `fuelBalanceResidual`, relaxT = 10          -> `< 0.02`, the EXISTING test threshold
//   C4  the balance is insensitive to the recentring RATE
//       fuel balance at relaxT 10 / 30 / 90         -> every point `< 0.02`
//   C5  what already works is preserved
//       T_peak == 1800.00 K exactly; x_front == 3.50000 mm; min Y_i >= 0;
//       |sum Y - 1| <= 1.2e-06
//   C6b the consumption integral agrees with the eigenvalue
//       S_c vs mdot/rho_u -> "agree to the march's own error". Measured pre-fix 4.90 vs 4.86 at
//       500 cells, so the criterion is loose; both numbers are REPORTED and the ratio given.
//
// A PREDICTION REGISTERED BEFORE THE RUN, and it is a prediction of FAILURE:
//
//   C5's `x_front == 3.50000 mm` is expected to FAIL, at about 3.49922 mm.
//   WHY, and why that is not a reason to weaken it: pre-fix, `result.flamePosition` was ASSIGNED
//   the anchor input and never measured -- TD-005's C4d found and fixed exactly that. So this
//   clause passed because it compared the anchor against itself. A real measured front cannot sit
//   at the anchor to five decimals. The clause is therefore an obsolete CONSTANT on a now-working
//   instrument, not a real detector of the property it names -- and under CLAUDE.md 5.3 that needs
//   an AMENDMENT with its own authorization and dry-run, not a quiet re-threshold here.
//   The dead band is 3 cells = 0.06 mm, so a derived tolerance exists; it is not applied by this
//   probe. The FAILURE IS REPORTED AS A FAILURE.
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

// the frozen thresholds
constexpr Real kBalanceBound = 0.02;      // C3, C4
constexpr Real kSumYBound = 1.2e-06;      // C5
constexpr Real kPeakExact = 1800.0;       // C5
constexpr Real kFrontExactMm = 3.50000;   // C5

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
  Index steps{};
  Real balance{};
  Real peak{};
  Real frontMm{};
  Real minY{};
  Real sumDev{};
  Real sL{};
  Real sC{};
  Real wall{};
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

  std::printf("TD-004 -- THE FROZEN GATE FROM C3\n\n");
  std::printf("  500 cells, PRODUCTION dt (cap 5), anchor %.3f mm. Thresholds quoted from GATE.md.\n",
              kAnchorFraction * kDomain * 1000.0);
  std::printf("  C3 balance < %.2f at relaxT 10 | C4 same at 10/30/90 | C5 preservation | C6b S_c\n\n",
              kBalanceBound);
  std::printf("%8s %10s %12s %11s %12s %12s %12s %10s %10s %8s\n", "relaxT", "steps", "balance",
              "T_peak", "front mm", "min Y", "|sumY-1|", "S_L", "S_c", "wall s");
  std::fflush(stdout);

  std::vector<Row> rows;
  for (const Real relaxT : {10.0, 30.0, 90.0}) {
    PremixedFlameSettings s;
    s.cells = kCells;
    s.domainLength = kDomain;
    s.anchorFraction = kAnchorFraction;
    s.tolerance = 1.0e-3;
    s.maxOuterIterations = 40;
    s.driftSettleTolerance = 0.05;
    s.driftWindow = 32;
    s.finalMarchRelaxationTimes = relaxT;
    s.maximumStepsPerTrial = 80000;

    const auto t0 = std::chrono::steady_clock::now();
    const PremixedFlameResult f = solvePremixedFlame(chemistry, conditions, s);
    const Real wall = std::chrono::duration<Real>(std::chrono::steady_clock::now() - t0).count();

    Row r;
    r.relaxT = relaxT;
    r.steps = f.finalMarchSteps;
    r.balance = f.fuelBalanceResidual;
    r.peak = f.peakTemperature;
    r.frontMm = f.flamePosition * 1000.0;
    r.minY = f.minMassFraction;
    r.sumDev = f.maxCompositionSumDeviation;
    r.sL = f.flameSpeed * 100.0;
    r.sC = f.consumptionSpeed * 100.0;
    r.wall = wall;
    rows.push_back(r);

    std::printf("%8.0f %10lld %12.6e %11.5f %12.6f %12.4e %12.4e %10.5f %10.5f %8.0f\n", relaxT,
                static_cast<long long>(r.steps), r.balance, r.peak, r.frontMm, r.minY, r.sumDev,
                r.sL, r.sC, r.wall);
    std::fflush(stdout);
  }

  // ---------------------------------------------------------------- the criteria, in frozen order
  std::printf("\n=== THE CRITERIA, IN FROZEN ORDER, STOPPING AT THE FIRST FAILURE\n\n");

  const Row& ref = rows[0];
  const bool c3 = ref.balance < kBalanceBound;
  std::printf("C3   balance %.6e < %.2f at relaxT 10                        %s\n", ref.balance,
              kBalanceBound, c3 ? "PASS" : "FAIL");
  if (!c3) {
    std::printf("\n*** STOPPED AT C3. Nothing below is evaluated.\n");
    return 0;
  }

  bool c4 = true;
  for (const Row& r : rows) {
    if (!(r.balance < kBalanceBound)) {
      c4 = false;
    }
  }
  std::printf("C4   balance < %.2f at relaxT 10/30/90: %.4e / %.4e / %.4e   %s\n", kBalanceBound,
              rows[0].balance, rows[1].balance, rows[2].balance, c4 ? "PASS" : "FAIL");
  if (!c4) {
    std::printf("\n*** STOPPED AT C4. Nothing below is evaluated.\n");
    return 0;
  }

  // C5, clause by clause, because a single verdict would hide which clause failed
  std::printf("C5   preservation, clause by clause at relaxT 10:\n");
  const bool c5peak = (ref.peak == kPeakExact);
  const bool c5front = (std::abs(ref.frontMm - kFrontExactMm) < 1e-05);
  const bool c5minY = (ref.minY >= 0.0);
  const bool c5sum = (ref.sumDev <= kSumYBound);
  std::printf("       T_peak    %.5f == %.2f exactly                        %s\n", ref.peak,
              kPeakExact, c5peak ? "PASS" : "FAIL");
  std::printf("       x_front   %.6f == %.5f mm exactly                   %s\n", ref.frontMm,
              kFrontExactMm, c5front ? "PASS" : "FAIL");
  std::printf("       min Y_i   %.4e >= 0                                %s\n", ref.minY,
              c5minY ? "PASS" : "FAIL");
  std::printf("       |sumY-1|  %.4e <= %.1e                           %s\n", ref.sumDev,
              kSumYBound, c5sum ? "PASS" : "FAIL");
  if (!(c5peak && c5front && c5minY && c5sum)) {
    std::printf("\n*** STOPPED AT C5. C6b is NOT evaluated.\n");
    if (!c5front && c5peak && c5minY && c5sum) {
      std::printf("\n    The ONLY failing clause is x_front, at %.6f mm -- %.3f cells from the\n",
                  ref.frontMm, (ref.frontMm - kFrontExactMm) / (kDomain / kCells * 1000.0));
      std::printf("    anchor. This was PREDICTED in this probe's header before the run:\n");
      std::printf("    pre-fix, `flamePosition` was ASSIGNED the anchor input and never measured\n");
      std::printf("    (TD-005 C4d), so the clause compared the anchor against itself. A measured\n");
      std::printf("    front cannot equal the anchor to five decimals.\n");
      std::printf("    This is an OBSOLETE CONSTANT on a now-working instrument. Per CLAUDE.md 5.3\n");
      std::printf("    it needs an AMENDMENT with its own authorization and dry-run. It is NOT\n");
      std::printf("    re-thresholded here, and the failure stands as measured.\n");
    }
    return 0;
  }

  std::printf("C6b  S_c vs S_L: %.5f vs %.5f cm/s, ratio %.5f\n", ref.sC, ref.sL, ref.sC / ref.sL);
  std::printf("       the criterion is \"agree to the march's own error\" -- REPORTED, not\n");
  std::printf("       auto-judged, because the frozen text gives no number. Pre-fix: 4.90 vs 4.86.\n");
  std::printf("\n=== C3, C4, C5 and C6b reached. C6, C6a and C7 remain and are separate runs.\n");
  return 0;
}
