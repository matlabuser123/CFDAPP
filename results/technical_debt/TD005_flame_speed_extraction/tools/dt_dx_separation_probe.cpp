// TD-005 C3 -- SEPARATE THE TIME INTEGRATION FROM THE SPATIAL DISCRETISATION.
//
// INVESTIGATION ONLY. No production file is changed by this probe.
//
// WHERE THIS STANDS. logs/16 established that the limit cycle is NUMERICAL -- the period, measured
// in the flame's OWN time unit (relaxation times = alpha/S^2), moved from 14 at dx = 20 um to ~18
// at dx = 40 um. But in that comparison dt is DERIVED from dx:
//
//     dt = min( cellCourant * dx / u_in ,  maximumCellDiffusionNumber * dx^2 / alpha_max )
//
// so doubling dx also changed dt (measured: ~397 march steps per relaxation time at 500 cells
// against ~103 at 250, i.e. dt roughly 3.9x larger). BOTH knobs moved, so logs/16 cannot say which
// one carries the period. `timeStep` is an explicit override for exactly this purpose -- a positive
// value replaces the derived dt -- so the two can be separated without touching production.
//
// =================================================================================================
// TEST T1 -- THE DECISIVE ONE: redo the mesh comparison with dt HELD FIXED
// =================================================================================================
//
//   250 cells with timeStep pinned to 3.76972e-05 s, which is the value the 500-CELL mesh derives
//   (read from the trial logs). So dx doubles while dt does not move at all.
//
//   PERIOD RETURNS TO ~14   the 14 -> 18 shift in logs/16 was caused by dt, not dx.
//                           => THE TIME INTEGRATION carries the period.
//   PERIOD STAYS AT ~18     the shift survives with dt fixed.
//                           => THE SPATIAL DISCRETISATION carries the period.
//
// =================================================================================================
// TEST T2 -- THE CONVERSE: hold dx fixed and halve dt
// =================================================================================================
//
//   500 cells with timeStep = 1.88486e-05 s, half the derived value, same mesh.
//
//   PERIOD STAYS AT 14      the time discretisation is CONVERGED at this dt and does not set the
//                           period => spatial.
//   PERIOD MOVES            dt sets the period => temporal.
//
// T1 AND T2 MUST AGREE. If T1 says temporal and T2 says spatial (or the reverse), the period is set
// by an INTERACTION of the two -- most likely a cell-Courant or cell-diffusion number rather than
// either knob alone -- and that is a third outcome, to be reported rather than forced into one of
// the first two.
//
// ONE SETTING IS SCALED DELIBERATELY, AND IT IS NOT A WEAKENING. `maximumStepsPerTrial` is a
// SAFETY CAP on how long a search trial may run. Halving dt doubles the steps a trial needs to
// cover the same physical time, so leaving the cap at 20000 would TRUNCATE trials and change the
// bracketing -- a confound. It is scaled by the same factor as dt so each trial retains the same
// physical budget. The cap is not an accuracy tolerance and nothing is relaxed by moving it.
//
// WHY relaxT IS STILL THE RIGHT ABSCISSA. `finalMarchRelaxationTimes` multiplies alpha/S^2, a
// physical time, and the solver converts it to steps by dividing by dt. So the same relaxT is the
// same PHYSICAL march duration at every dt -- which is exactly what must be held fixed while dt
// varies underneath it.
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

// The dt the 500-cell mesh derives, read from the trial lines of logs/06 and logs/01.
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

struct Row {
  Real relaxT{};
  Real offsetMm{};
  Real fuelBalance{};
  Real thickness{};
  Real flameSpeed{};
  Index marchSteps{};
  int status{-1};
  Real wall{};
  bool bad{false};
};

// HEALTH, not position. logs/16's N1 verdict was wrong because it judged by offset-from-anchor
// while the test had removed the mechanism that holds the front there. The fuel balance and the
// flame thickness are properties of the PROFILE and are not touched by dt or dx choices in the
// same way, so they are what classifies a row here.
Row run(const PremixedFlameChemistry& chem, const PremixedFlameConditions& cond,
        const PremixedFlameSettings& s, Real relaxT) {
  const Real xAnchor = kAnchorFraction * kDomain;
  const auto t0 = std::chrono::steady_clock::now();
  const PremixedFlameResult f = solvePremixedFlame(chem, cond, s);
  Row r;
  r.relaxT = relaxT;
  r.offsetMm = (f.flamePosition - xAnchor) * 1000.0;
  r.fuelBalance = f.fuelBalanceResidual;
  r.thickness = f.flameThickness;
  r.flameSpeed = f.flameSpeed;
  r.marchSteps = f.finalMarchSteps;
  r.status = static_cast<int>(f.status);
  r.wall = std::chrono::duration<Real>(std::chrono::steady_clock::now() - t0).count();
  r.bad = f.fuelBalanceResidual > 0.1;  // health, with a wide margin either side of the two modes
  return r;
}

void header() {
  std::printf("%8s %9s %11s %11s %9s %9s %8s %7s %6s\n", "relaxT", "marchStp", "offset mm",
              "fuel bal", "delta mm", "S_L cm/s", "wall s", "status", "phase");
  std::fflush(stdout);
}

void printRow(const Row& r) {
  std::printf("%8.0f %9lld %11.4f %11.3e %9.4f %9.5f %8.0f %7d %6s\n", r.relaxT,
              static_cast<long long>(r.marchSteps), r.offsetMm, r.fuelBalance,
              r.thickness * 1000.0, r.flameSpeed * 100.0, r.wall, r.status,
              r.bad ? "BAD" : "good");
  std::fflush(stdout);
}

std::string pattern(const std::vector<Row>& rows) {
  std::string p;
  for (const Row& r : rows) {
    p += (r.bad ? 'B' : 'G');
  }
  return p;
}

std::vector<Row> sweep(const PremixedFlameChemistry& chem, const PremixedFlameConditions& cond,
                       Index cells, Real dt, Index stepCap, const std::vector<Real>& ladder) {
  std::vector<Row> rows;
  for (const Real t : ladder) {
    PremixedFlameSettings s;
    s.cells = cells;
    s.domainLength = kDomain;
    s.anchorFraction = kAnchorFraction;
    s.tolerance = 1.0e-3;
    s.maxOuterIterations = 40;
    s.driftSettleTolerance = 0.05;
    s.maximumStepsPerTrial = stepCap;
    s.driftWindow = 32;
    s.finalMarchRelaxationTimes = t;
    s.timeStep = dt;  // explicit override: dt no longer follows dx
    rows.push_back(run(chem, cond, s, t));
    printRow(rows.back());
  }
  return rows;
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
  const std::string ref500 = "GBGBGB";  // 500 cells, derived dt: period 14
  const std::string ref250 = "BGBBGB";  // 250 cells, derived dt: period ~18 (logs/16)

  std::printf("TD-005 C3 -- SEPARATING dt FROM dx\n\n");
  std::printf("  ladder relaxT {10, 18, 26, 32, 40, 46}; relaxT is PHYSICAL march duration\n");
  std::printf("  reference patterns, both with dt DERIVED from dx (logs/14, logs/16):\n");
  std::printf("    500 cells, dt 3.770e-05  ->  %s   period 14 relaxation times\n", ref500.c_str());
  std::printf("    250 cells, dt ~1.45e-04  ->  %s   period ~18\n", ref250.c_str());
  std::printf("  phase is classified by HEALTH (fuel balance > 0.1), not by front position --\n");
  std::printf("  logs/16's N1 verdict was wrong because it judged by position.\n\n");

  // ============================================================ T1: dx doubles, dt held fixed
  std::printf("=== TEST T1 -- 250 cells with dt PINNED at the 500-cell value %.5e s\n", kDt500);
  std::printf("    dx doubles; dt does not move.\n");
  std::printf("    pattern ~ %s (period 14) => the 14->18 shift was dt   => TIME INTEGRATION\n",
              ref500.c_str());
  std::printf("    pattern ~ %s (period ~18) => the shift survives dt    => SPATIAL\n\n",
              ref250.c_str());
  header();
  const std::vector<Row> t1 = sweep(chemistry, conditions, 250, kDt500, 20000, ladder);
  const std::string p1 = pattern(t1);
  std::printf("\n    T1 pattern %s    (500-cell/period-14 %s, 250-cell/period-18 %s)\n",
              p1.c_str(), ref500.c_str(), ref250.c_str());
  if (p1 == ref500) {
    std::printf("    => MATCHES THE PERIOD-14 PATTERN with dx doubled. dt carried the shift:\n");
    std::printf("       THE TIME INTEGRATION SETS THE PERIOD.\n");
  } else if (p1 == ref250) {
    std::printf("    => MATCHES THE 250-CELL PATTERN with dt held fixed. dx carried the shift:\n");
    std::printf("       THE SPATIAL DISCRETISATION SETS THE PERIOD.\n");
  } else {
    std::printf("    => NEITHER reference pattern. The period is set by a COMBINATION of dt and\n");
    std::printf("       dx -- a cell-Courant or cell-diffusion number rather than either alone.\n");
  }
  std::fflush(stdout);

  // ============================================================ T2: dx fixed, dt halved
  const Real dtHalf = 0.5 * kDt500;
  std::printf("\n=== TEST T2 -- 500 cells with dt HALVED to %.5e s\n", dtHalf);
  std::printf("    dx fixed; dt halves. maximumStepsPerTrial doubled to 40000 so each trial keeps\n");
  std::printf("    the same PHYSICAL budget -- a safety cap, not an accuracy tolerance.\n");
  std::printf("    pattern ~ %s => dt is converged and does not set the period => SPATIAL\n",
              ref500.c_str());
  std::printf("    pattern moves => dt sets the period => TIME INTEGRATION\n\n");
  header();
  const std::vector<Row> t2 = sweep(chemistry, conditions, 500, dtHalf, 40000, ladder);
  const std::string p2 = pattern(t2);
  std::printf("\n    T2 pattern %s    (500-cell baseline at the full dt: %s)\n", p2.c_str(),
              ref500.c_str());
  if (p2 == ref500) {
    std::printf("    => UNCHANGED under a 2x finer dt. The time discretisation is converged here\n");
    std::printf("       and does not set the period => SPATIAL.\n");
  } else {
    std::printf("    => THE PATTERN MOVED under a finer dt => THE TIME INTEGRATION SETS IT.\n");
  }
  std::fflush(stdout);

  // ============================================================ do the two tests agree?
  std::printf("\n=== CONSISTENCY OF T1 AND T2\n");
  const bool t1Temporal = (p1 == ref500);
  const bool t2Temporal = (p2 != ref500);
  if (t1Temporal == t2Temporal) {
    std::printf("    The two tests AGREE: the period is set by the %s.\n",
                t1Temporal ? "TIME INTEGRATION" : "SPATIAL DISCRETISATION");
  } else {
    std::printf("    *** THE TWO TESTS DISAGREE. T1 says %s and T2 says %s.\n",
                t1Temporal ? "temporal" : "spatial", t2Temporal ? "temporal" : "spatial");
    std::printf("    That is the third outcome this probe was written to allow: the period is set\n");
    std::printf("    by an INTERACTION of dt and dx -- a cell-Courant or cell-diffusion number --\n");
    std::printf("    and neither knob owns it. Report it; do not pick one.\n");
  }
  return 0;
}
