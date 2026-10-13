// TD-005 / TD-006 -- THE TEMPORAL CONVERGENCE OF S_L. The measurement G8 has needed since
// flame_solver_findings.md section 3.1 was written.
//
// INVESTIGATION ONLY. No production file is changed by this probe.
//
// WHY. logs/17 measured S_L = 4.86230 cm/s at the derived dt and 4.65615 at dt/2 -- a 4.24 % shift
// from the time step alone, against a G8 band of +-2.80 %. Two points give a difference, not an
// order, and cannot say whether S_L converges at all. CLAUDE.md section 6 requires an observed
// order, Richardson extrapolation and a GCI for exactly this situation, and requires that the error
// not change sign (an order is undefined across a zero crossing).
//
// THE LADDER. dt, dt/2, dt/4, dt/8 at FIXED dx (500 cells), refinement ratio r = 2 throughout, so
// the family is self-similar in the one parameter being refined. That is the property the suite's
// own mesh test LACKS: TheFlameSpeedConvergesUnderMeshRefinement refines cells, dt is DERIVED from
// dx, and the spatial and temporal errors have OPPOSITE SIGNS (+0.81 % and -4.24 %), so they
// partially cancel and that test passes while neither component is converged.
//
// THE ECONOMY THAT MAKES THIS AFFORDABLE, and why it is not a shortcut. S_L is fixed by the
// BRACKETING, which completes before the final march begins; the final march only produces the
// reported STATE. That is not an assumption -- logs/14 and logs/15 measured S_L = 4.86230 at every
// finalMarchRelaxationTimes from 10 to 46, nineteen solves, identical to six figures. So the final
// march is set to 1.0 relaxation time here, which cuts cost without touching the quantity being
// measured. The reported balances are therefore at a SHORT march and are logged for context only --
// they are NOT the admissibility measurement, and must not be read as one.
//
// maximumStepsPerTrial IS SCALED BY 1/dt. It is a safety cap on trial length, and at dt/8 a trial
// needs eight times the steps to cover the same physical time; leaving it fixed would truncate
// trials and change the bracketing. Scaling it preserves each trial's physical budget. It is not an
// accuracy tolerance and nothing is relaxed.
//
// PRE-REGISTERED PREDICTION, with falsifiers in both directions.
//
//   Implicit Euler is FIRST order in time, so the expectation is p ~ 1 with the successive
//   differences roughly HALVING. Extrapolating the one interval already measured:
//
//       S(dt)   = 4.86230      measured
//       S(dt/2) = 4.65615      measured, difference -0.20615
//       S(dt/4) ~ 4.5531       predicted if p = 1 (difference -0.1031)
//       S(dt/8) ~ 4.4015 ... 4.5016  depending on p; the Richardson limit for p = 1 is ~4.4500
//
//   CONFIRMED IF    p lands in [0.7, 1.3] on both intervals and the differences shrink
//                   monotonically. Then S_L converges, Richardson gives the limit, and the
//                   total correction from the current default is about -8.5 % -- which would
//                   mean every S_L figure in this repository is about 8 % high.
//   FALSIFIED IF    the differences do NOT shrink. Then S_L does not converge in dt and the
//                   problem is not resolution but formulation.
//   ALSO FALSIFIED  if the error CHANGES SIGN across the ladder. An observed order is undefined
//                   across a zero crossing (CLAUDE.md section 6), and the probe says so rather
//                   than printing a meaningless number.
//   UNEXPECTED      p ~ 2 would mean the march is not behaving as first-order implicit Euler,
//                   which is a finding about the integrator and not about the flame.
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
constexpr Real kDt500 = 3.76972e-05;

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

  std::printf("TD-005 / TD-006 -- TEMPORAL CONVERGENCE OF S_L, at FIXED dx\n\n");
  std::printf("  %lld cells over %.0f mm (dx = %.1f um), refinement ratio r = 2 in dt only\n",
              static_cast<long long>(kCells), kDomain * 1000.0,
              kDomain / static_cast<Real>(kCells) * 1e6);
  std::printf("  final march pinned at 1.0 relaxation time: S_L comes from the BRACKETING, which\n");
  std::printf("  completes first. Verified -- 19 solves over relaxT 10..46 gave identical S_L.\n");
  std::printf("  The balances below are at a SHORT march and are CONTEXT ONLY, not admissibility.\n");
  std::printf("  G8's band 35.0-37.0 cm/s is +-2.80 %%. Implicit Euler should give p ~ 1.\n\n");
  std::printf("%10s %13s %11s %12s %9s %11s %9s %8s\n", "dt s", "divisor", "S_L cm/s",
              "bracket", "marchStp", "fuel bal", "steps", "wall s");
  std::fflush(stdout);

  std::vector<Real> speeds;
  std::vector<Real> dts;
  const std::vector<Index> divisors = {1, 2, 4, 8};

  for (const Index d : divisors) {
    const Real dt = kDt500 / static_cast<Real>(d);
    PremixedFlameSettings s;
    s.cells = kCells;
    s.domainLength = kDomain;
    s.anchorFraction = kAnchorFraction;
    s.tolerance = 1.0e-3;
    s.maxOuterIterations = 40;
    s.driftSettleTolerance = 0.05;
    s.driftWindow = 32;
    s.timeStep = dt;
    s.finalMarchRelaxationTimes = 1.0;
    // the safety cap scales with 1/dt so each trial keeps the same PHYSICAL budget
    s.maximumStepsPerTrial = 20000 * d;

    const auto t0 = std::chrono::steady_clock::now();
    const PremixedFlameResult f = solvePremixedFlame(chemistry, conditions, s);
    const Real wall = std::chrono::duration<Real>(std::chrono::steady_clock::now() - t0).count();

    char div[16];
    std::snprintf(div, sizeof(div), "dt/%lld", static_cast<long long>(d));
    std::printf("%10.4e %13s %11.5f %12.4e %9lld %11.3e %9lld %8.0f%s\n", dt, div,
                f.flameSpeed * 100.0, f.massFluxResidual,
                static_cast<long long>(f.finalMarchSteps), f.fuelBalanceResidual,
                static_cast<long long>(f.totalTimeSteps), wall,
                (f.status == PremixedFlameStatus::Converged) ? "" : "   <-- NOT Converged");
    std::fflush(stdout);
    speeds.push_back(f.flameSpeed * 100.0);
    dts.push_back(dt);
  }

  // ---------------------------------------------------------------- convergence analysis
  std::printf("\n=== CONVERGENCE ANALYSIS (CLAUDE.md section 6)\n\n");
  std::printf("  successive differences:\n");
  std::vector<Real> diffs;
  for (std::size_t i = 1; i < speeds.size(); ++i) {
    const Real d = speeds[i] - speeds[i - 1];
    diffs.push_back(d);
    std::printf("    S(dt/%zu) - S(dt/%zu) = %+.5f cm/s   (%+.3f %% of %.5f)\n",
                static_cast<std::size_t>(1) << i, static_cast<std::size_t>(1) << (i - 1), d,
                d / speeds[i - 1] * 100.0, speeds[i - 1]);
  }

  // sign change check -- an order is undefined across a zero crossing
  bool signChange = false;
  for (std::size_t i = 1; i < diffs.size(); ++i) {
    if ((diffs[i] < 0.0) != (diffs[i - 1] < 0.0)) {
      signChange = true;
    }
  }
  std::printf("\n  error changes sign across the ladder: %s\n", signChange ? "YES" : "no");
  if (signChange) {
    std::printf("  *** AN OBSERVED ORDER IS UNDEFINED ACROSS A SIGN CHANGE (CLAUDE.md section 6).\n");
    std::printf("      No order is reported. The differences above stand on their own, and the\n");
    std::printf("      non-monotone behaviour is itself the finding.\n");
    return 0;
  }

  bool shrinking = true;
  for (std::size_t i = 1; i < diffs.size(); ++i) {
    if (std::abs(diffs[i]) >= std::abs(diffs[i - 1])) {
      shrinking = false;
    }
  }
  std::printf("  differences shrink monotonically: %s\n", shrinking ? "YES" : "*** NO ***");
  if (!shrinking) {
    std::printf("  *** S_L DOES NOT CONVERGE IN dt over this ladder. The problem is not\n");
    std::printf("      resolution but formulation, and no dt gives a defensible flame speed.\n");
    return 0;
  }

  std::printf("\n  observed order  p = log2(|d_k| / |d_k+1|):\n");
  std::vector<Real> orders;
  for (std::size_t i = 1; i < diffs.size(); ++i) {
    const Real p = std::log2(std::abs(diffs[i - 1]) / std::abs(diffs[i]));
    orders.push_back(p);
    std::printf("    interval %zu -> %zu :  p = %.4f\n", i, i + 1, p);
  }

  // Richardson extrapolation on the finest pair, using the last observed order
  const Real p = orders.back();
  const Real fFine = speeds.back();
  const Real fCoarse = speeds[speeds.size() - 2];
  const Real limit = fFine + (fFine - fCoarse) / (std::pow(2.0, p) - 1.0);
  std::printf("\n  Richardson limit (dt -> 0), using p = %.4f:  S_L = %.5f cm/s\n", p, limit);
  std::printf("  correction from the DEFAULT dt (%.5f):  %+.2f %%\n", speeds.front(),
              (limit - speeds.front()) / speeds.front() * 100.0);

  // GCI on the finest pair, Fs = 1.25 for three or more grids
  const Real eps = (fFine - fCoarse) / fCoarse;
  const Real gci = 1.25 * std::abs(eps) / (std::pow(2.0, p) - 1.0) * 100.0;
  std::printf("  GCI on the finest pair (Fs = 1.25):  %.3f %%\n", gci);
  std::printf("  G8's band is +-2.80 %%, so the finest dt here is %s to decide G8.\n",
              (gci < 2.8) ? "TIGHT ENOUGH" : "STILL TOO COARSE");

  std::printf("\n  HOW TO READ THIS\n");
  std::printf("    p in [0.7, 1.3] on both intervals is first-order implicit Euler behaving, and\n");
  std::printf("    the Richardson limit is then the defensible S_L. A correction of order -8 %%\n");
  std::printf("    from the default would mean every S_L figure in this repository is high by\n");
  std::printf("    about that much, including the one TD-006 would quote to G8.\n");
  return 0;
}
