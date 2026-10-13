// TD-031 -- WHERE DOES THE FINAL MARCH CONVERGE, AND IS THAT POINT INVARIANT?
//
// INVESTIGATION ONLY. No production file is changed by this probe.
//
// THE QUESTION. The convergence point was measured once, as relaxT 18, at 500 cells and cap 5
// (TD-005 logs/19). The default is 10. Setting the default from a single configuration is exactly
// the error TD-027 exists to correct -- `maximumCellDiffusionNumber = 20.0` was also a plausible
// number that nobody had measured across anything. So before any default is chosen, the
// convergence point has to be measured across a MESH pair and a CAP pair.
//
// WHY IT MIGHT MOVE, AND WHY IT MIGHT NOT. `finalMarchRelaxationTimes` multiplies the flame's own
// relaxation time alpha/S^2, so it is NOMINALLY mesh- and dt-independent: the same relaxT should be
// the same physical duration at any resolution. But TD-027 established that a quantity being
// nominally physical is not the same as its being measured invariant -- `D` was nominally a cap and
// turned out to be an identity, and the limit-cycle period was nominally in physical units and
// moved with the mesh anyway. So this is measured, not assumed.
//
//   stage 1   250 cells / cap 5   and   500 cells / cap 10     the two NEW configurations
//   stage 2   500 cells / cap 5                                the reference, re-measured fresh
//             so that all three come from ONE binary with identical settings rather than citing
//             logs/19 for one leg of a three-way comparison
//
// THE CONVERGENCE POINT, defined before the data is seen. The smallest ladder index i such that for
// every level j >= i:
//     * |front offset| <= recentreDeadBandCells (3.0)   -- the front has returned to the anchor
//     * the balances from i onward satisfy (max - min) <= mean   -- the constant-free settling
//       test already frozen in TD-004 GATE_A1 C2-A1(c) and TD-027 C4
// Both must hold. The front test alone would accept a state whose balance is still moving; the
// balance test alone would accept a settled balance at a displaced front.
//
// PRE-REGISTERED, with falsifiers:
//   INVARIANT      all three configurations converge at the same relaxT. Then the quantity is
//                  physical as documented and a default can be derived from it with margin.
//   MOVES WITH dx  the 250-cell leg differs from the 500-cell leg at the same cap.
//   MOVES WITH dt  the cap-10 leg differs from the cap-5 leg at the same mesh.
//   If it moves at all, a SINGLE default cannot be correct for every configuration, and the fix is
//   not a number -- it is a convergence CRITERION the march tests for itself.
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
constexpr Real kDeadBand = 3.0;  // recentreDeadBandCells, the solver's own default

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

// returns the convergence point as a relaxT value, or -1 if the ladder never converges
Real sweep(const PremixedFlameChemistry& chem, const PremixedFlameConditions& cond, Index cells,
           Real cap, const std::vector<Real>& ladder) {
  const Real dx = kDomain / static_cast<Real>(cells);
  const Real xAnchor = kAnchorFraction * kDomain;

  std::printf("\n=== %lld cells (dx %.1f um), maximumCellDiffusionNumber = %.1f\n",
              static_cast<long long>(cells), dx * 1e6, cap);
  std::printf("%9s %10s %12s %12s %10s %10s %9s\n", "relaxT", "marchStp", "offset cell",
              "fuel bal", "delta mm", "S_L cm/s", "wall s");
  std::fflush(stdout);

  std::vector<Real> offsets;
  std::vector<Real> balances;
  for (const Real t : ladder) {
    PremixedFlameSettings s;
    s.cells = cells;
    s.domainLength = kDomain;
    s.anchorFraction = kAnchorFraction;
    s.tolerance = 1.0e-3;
    s.maxOuterIterations = 40;
    s.driftSettleTolerance = 0.05;
    s.driftWindow = 32;
    s.maximumCellDiffusionNumber = cap;
    s.finalMarchRelaxationTimes = t;
    // the safety cap scales with 1/dt so each trial keeps the same PHYSICAL budget
    s.maximumStepsPerTrial = static_cast<Index>(20000.0 * (20.0 / cap));

    const auto t0 = std::chrono::steady_clock::now();
    const PremixedFlameResult f = solvePremixedFlame(chem, cond, s);
    const Real wall = std::chrono::duration<Real>(std::chrono::steady_clock::now() - t0).count();
    const Real off = (f.flamePosition - xAnchor) / dx;
    std::printf("%9.0f %10lld %12.2f %12.4e %10.4f %10.5f %9.0f%s\n", t,
                static_cast<long long>(f.finalMarchSteps), off, f.fuelBalanceResidual,
                f.flameThickness * 1000.0, f.flameSpeed * 100.0, wall,
                (f.status == PremixedFlameStatus::Converged) ? "" : "  NOT Converged");
    std::fflush(stdout);
    offsets.push_back(off);
    balances.push_back(f.fuelBalanceResidual);
  }

  // the convergence point, by the definition fixed in the header
  for (std::size_t i = 0; i < ladder.size(); ++i) {
    bool frontOk = true;
    for (std::size_t j = i; j < ladder.size(); ++j) {
      if (std::abs(offsets[j]) > kDeadBand) {
        frontOk = false;
      }
    }
    if (!frontOk) {
      continue;
    }
    Real lo = balances[i];
    Real hi = balances[i];
    Real sum = 0.0;
    for (std::size_t j = i; j < ladder.size(); ++j) {
      lo = std::min(lo, balances[j]);
      hi = std::max(hi, balances[j]);
      sum += balances[j];
    }
    const Real mean = sum / static_cast<Real>(ladder.size() - i);
    if ((hi - lo) <= mean) {
      std::printf("    CONVERGENCE POINT: relaxT %.0f\n", ladder[i]);
      std::printf("      from there: |offset| <= %.1f cells at every level, and balances\n",
                  kDeadBand);
      std::printf("      max %.4e - min %.4e = %.4e  <=  mean %.4e\n", hi, lo, hi - lo, mean);
      return ladder[i];
    }
  }
  std::printf("    *** NO CONVERGENCE POINT in this ladder -- the state never settles by\n");
  std::printf("        relaxT %.0f. The ladder is too short, or this configuration does not\n",
              ladder.back());
  std::printf("        converge at all.\n");
  return -1.0;
}

}  // namespace

int main(int argc, char** argv) {
  const int stage = (argc > 1) ? std::atoi(argv[1]) : 0;
  if (stage != 1 && stage != 2) {
    std::printf("usage: convergence_point_probe <stage 1|2>\n");
    return 2;
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

  const std::vector<Real> ladder = {10.0, 14.0, 18.0, 22.0, 26.0};

  std::printf("TD-031 -- THE CONVERGENCE POINT, across a mesh pair and a cap pair\n\n");
  std::printf("  the current default is relaxT 10; TD-005 logs/19 measured 18 at 500 cells / cap 5\n");
  std::printf("  convergence point = smallest relaxT from which, at EVERY later level,\n");
  std::printf("     |front offset| <= %.1f cells  AND  (max-min) <= mean on the balance\n", kDeadBand);
  std::printf("  stage %d\n", stage);
  std::fflush(stdout);

  if (stage == 1) {
    const Real a = sweep(chemistry, conditions, 250, 5.0, ladder);
    const Real b = sweep(chemistry, conditions, 500, 10.0, ladder);
    std::printf("\n=== STAGE 1 SUMMARY\n");
    std::printf("    250 cells / cap  5 : convergence point %.0f\n", a);
    std::printf("    500 cells / cap 10 : convergence point %.0f\n", b);
    std::printf("    500 cells / cap  5 : stage 2\n");
  } else {
    const Real c = sweep(chemistry, conditions, 500, 5.0, ladder);
    std::printf("\n=== STAGE 2 SUMMARY\n");
    std::printf("    500 cells / cap  5 : convergence point %.0f\n", c);
    std::printf("    TD-005 logs/19 measured 18 for this configuration; a DIFFERENT answer here\n");
    std::printf("    would mean the measurement is not reproducible and nothing can be derived\n");
    std::printf("    from it until that is understood.\n");
  }
  return 0;
}
