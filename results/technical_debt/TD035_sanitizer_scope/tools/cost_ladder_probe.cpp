// TD-035 -- HOW DOES THE PREMIXED-FLAME SOLVE'S COST SCALE WITH RESOLUTION?
//
// THE DECISION THIS INFORMS. ASan costs 38x on this suite (303 s -> 11612 s, measured in TD-032),
// so G8's 4343 s becomes ~46 h and at least three tests exceed `ctest --timeout 7200`. Three
// policies were on the table: declare a stated-scope subset, build a CHEAP FIXTURE, or amend §8.
// Only the cheap fixture preserves actual sanitizer COVERAGE -- the other two record a gap.
//
// WHY A CHEAP FIXTURE IS DEFENSIBLE AT ALL. ASan looks for memory errors, not physics. A
// use-after-free or overflow in `recentre`, `locateFront`, `shiftField` or the reactor fires at 100
// cells exactly as at 500; it is the CODE PATH that must be exercised, not the convergence order.
// The G8 tests at 500-1500 cells assert PHYSICS (observed order, eigenvalue plateau, adiabatic
// temperature) and cannot simply be shrunk -- their assertions would break. A sanitizer fixture is
// therefore a DIFFERENT test with the same paths and cheap invariants, not a shrunken G8.
//
// WHAT IS MEASURED HERE. Wall time against `cells`, in Release, so the exponent is MEASURED rather
// than assumed. Cost is expected to grow faster than linearly because the step count rises with
// resolution as well as the per-step work, but "expected" is not a number and the policy needs one.
//
// Each row also reports S_L and the status, so a resolution that no longer produces a flame is
// visible -- a fixture that silently stops exercising the reacting path would be worthless for
// exactly the reason TD-035 exists.
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <stdexcept>
#include <utility>
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

const char* statusName(PremixedFlameStatus s) {
  switch (s) {
    case PremixedFlameStatus::Converged: return "Converged";
    case PremixedFlameStatus::MaxIterations: return "MaxIterations";
    case PremixedFlameStatus::NoFlame: return "NoFlame";
    default: return "other";
  }
}

}  // namespace

int main(int argc, char** argv) {
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

  std::printf("TD-035 -- COST AGAINST RESOLUTION, Release, relaxT 10\n\n");
  std::printf("%7s %10s %12s %12s %10s %14s %s\n", "cells", "wall s", "marchSteps", "recentr",
              "S_L cm/s", "ASan est (s)", "status");
  std::fflush(stdout);

  // Resolutions may be given on the command line. The ASan leg uses this to measure ONLY the two
  // candidate fixture sizes: under a sanitizer the full ladder would cost hours, which is the very
  // problem TD-035 is about.
  std::vector<Index> resolutions;
  for (int a = 1; a < argc; ++a) {
    const long v = std::atol(argv[a]);
    if (v > 0) {
      resolutions.push_back(static_cast<Index>(v));
    }
  }
  if (resolutions.empty()) {
    resolutions = {60, 100, 150, 220, 320, 500};
  }

  std::vector<std::pair<Index, Real>> ladder;
  for (const Index cells : resolutions) {
    PremixedFlameSettings s;
    s.cells = cells;
    s.domainLength = kDomain;
    s.anchorFraction = kAnchorFraction;
    s.tolerance = 1.0e-3;
    s.maxOuterIterations = 40;
    s.driftSettleTolerance = 0.05;
    s.driftWindow = 32;
    s.finalMarchRelaxationTimes = 10.0;
    s.maximumStepsPerTrial = 80000;

    const auto t0 = std::chrono::steady_clock::now();
    PremixedFlameResult f;
    bool threw = false;
    std::string what;
    try {
      f = solvePremixedFlame(chemistry, conditions, s);
    } catch (const std::exception& e) {
      threw = true;
      what = e.what();
    }
    const Real wall = std::chrono::duration<Real>(std::chrono::steady_clock::now() - t0).count();

    if (threw) {
      std::printf("%7lld %10.1f %12s %12s %10s %14s THREW: %s\n", static_cast<long long>(cells),
                  wall, "-", "-", "-", "-", what.c_str());
    } else {
      std::printf("%7lld %10.1f %12lld %12lld %10.5f %14.0f %s\n", static_cast<long long>(cells),
                  wall, static_cast<long long>(f.finalMarchSteps),
                  static_cast<long long>(f.recentrings), f.flameSpeed * 100.0, wall * 38.0,
                  statusName(f.status));
      ladder.emplace_back(cells, wall);
    }
    std::fflush(stdout);
  }

  std::printf("\n=== THE EXPONENT, fitted between consecutive rows: wall ~ cells^p\n");
  for (std::size_t i = 1; i < ladder.size(); ++i) {
    const Real rc = static_cast<Real>(ladder[i].first) / static_cast<Real>(ladder[i - 1].first);
    const Real rw = ladder[i].second / ladder[i - 1].second;
    std::printf("  %4lld -> %4lld   cost x%.2f   p = %.2f\n",
                static_cast<long long>(ladder[i - 1].first),
                static_cast<long long>(ladder[i].first), rw, std::log(rw) / std::log(rc));
  }

  std::printf("\n=== HOW TO READ THIS\n");
  std::printf("  'ASan est' is wall x 38, the factor TD-032 measured on this suite. It is an\n");
  std::printf("  EXTRAPOLATION from one measured point, not a measurement at that resolution, and\n");
  std::printf("  the policy must say so. The cap it has to clear is ctest --timeout 7200 PER TEST.\n");
  std::printf("  A row whose status is not Converged is NOT a candidate fixture at any price: a\n");
  std::printf("  cheap case that stops exercising the reacting path tests nothing.\n");
  return 0;
}
