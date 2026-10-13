// TD-035 -- A SANITIZER FIXTURE for the premixed-flame solver.
//
// WHY THIS FILE EXISTS. `ctest --preset asan` cannot run G8PremixedFlameTest: the sanitizer costs
// 12.6-14.7x on this solver (measured, TD035_sanitizer_scope/logs/02), so its 500-1500 cell cases
// land between hours and well past the 7200 s per-test cap, and at least three exceed it. The
// consequence is that the premixed-flame code -- `recentre`, `locateFront`, `shiftField`, the
// reactor sub-stepping -- has historically been covered by NO sanitizer at all.
//
// A sanitizer looks for MEMORY ERRORS, not physics. A use-after-free or an overflow in `shiftField`
// fires at 150 cells exactly as it does at 500; it is the CODE PATH that has to execute. So this is
// deliberately a DIFFERENT test from the G8 suite, not a shrunken copy of one:
//
//   * the G8 tests assert physics that NEEDS resolution -- observed order under refinement, the
//     eigenvalue plateau, the adiabatic temperature. Shrinking them would break their assertions,
//     which is weakening a test to obtain a pass and is forbidden outright (CLAUDE.md §7).
//   * this test asserts only that the reacting path RAN and produced a physically sane state. The
//     detecting is done by AddressSanitizer and UndefinedBehaviorSanitizer.
//
// MEASURED COST at this fixture (TD035_sanitizer_scope/logs/01 and /02):
//     150 cells   Release 19.2 s   ASan+UBSan 282.6 s   against the 7200 s cap -> 25.5x headroom
// and the sanitizers found nothing: 0 AddressSanitizer errors, 0 UBSan runtime errors, 0 leaks.
//
// ----------------------------------------------------------- WHY THE ASSERTIONS LOOK WEAK, AND ARE
// NOT
//
// A clean sanitizer log looks IDENTICAL whether the code is clean or the instrument never reached
// it. That is the fail-open pattern this campaign has hit eight times. So the assertions below are
// not a physics gate -- they are the proof that the instrument arrived:
//
//     status == Converged          the bisection actually resolved an eigenvalue
//     recentrings > 0              `recentre()` and `shiftField()` ACTUALLY FIRED. Without this the
//                                  fixture could pass while covering none of the code TD-035 exists
//                                  to cover.
//     finalMarchSteps > 1000       the march ran rather than aborting early
//     the state is physically sane bounded S_L, no runaway temperature, mass fractions in range
//
// The bounds are deliberately WIDE because 150 cells is not a converged configuration: `S_L` here
// is about 4.91 cm/s against 4.96 at 500 cells, and a tight bound would be a physics assertion
// smuggled into a memory test. Anything tighter belongs in the G8 suite.
//
// THIS TEST IS NOT IN CFDSpeciesTests. It has its own target, because C7 of TD-004's frozen gate
// bounds "CFDSpeciesTests excluding G8PremixedFlameTest" at EXACTLY 343 tests, and adding a test
// there would make it 344 and break a frozen criterion.
#include <gtest/gtest.h>

#include <cmath>

#include "cfd/physics/TemperatureProperty.hpp"
#include "cfd/species/MixtureThermodynamics.hpp"
#include "cfd/species/PremixedFlame.hpp"
#include "cfd/species/ReactionMechanism.hpp"
#include "cfd/species/SpeciesRegistry.hpp"
#include "cfd/thermal/Enthalpy.hpp"
#include "cfd/thermal/TemperatureDependentProperty.hpp"

namespace {

using cfd::Index;
using cfd::Real;
using namespace cfd::species;

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

// 150 cells is the measured sweet spot: 282.6 s under ASan+UBSan against the 7200 s cap, which is
// 25.5x headroom -- so the measured 12.6-14.7x sanitizer factor would have to be wrong by nearly
// tenfold before the cap is threatened.
constexpr Index kCells = 150;

SpeciesThermodynamics makeSpecies(const std::string& name) {
  return SpeciesThermodynamics(
      kWeight,
      cfd::thermal::SensibleEnthalpy(
          cfd::thermal::TemperatureDependentProperty::constant(kSpecificHeat, name), 298.15, 0.0));
}

}  // namespace

TEST(PremixedFlameSanitizerTest, TheReactingPathRunsCleanAtReducedResolution) {
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

  const cfd::physics::ConstantProperty conductivity(kConductivity);
  const PremixedFlameChemistry chemistry{&mechanism, &mixture, &formation, &kinetics};

  PremixedFlameConditions conditions;
  conditions.pressure = kPressure;
  conditions.unburntTemperature = kUnburntT;
  conditions.unburntComposition = {kFuelFraction, 1.0 - kFuelFraction};
  conditions.conductivity = &conductivity;
  conditions.specificHeat = kSpecificHeat;
  conditions.lewisNumber = 1.0;
  conditions.fuelIndex = 0;

  PremixedFlameSettings settings;
  settings.cells = kCells;
  settings.domainLength = kDomain;
  settings.anchorFraction = 0.35;
  settings.tolerance = 1.0e-3;
  settings.maxOuterIterations = 40;
  settings.driftSettleTolerance = 0.05;
  settings.driftWindow = 32;
  settings.finalMarchRelaxationTimes = 10.0;
  settings.maximumStepsPerTrial = 80000;

  const PremixedFlameResult result = solvePremixedFlame(chemistry, conditions, settings);

  // ---- the instrument arrived: these prove the code TD-035 exists to cover actually executed
  ASSERT_EQ(result.status, PremixedFlameStatus::Converged)
      << "the bisection did not resolve an eigenvalue, so the reacting path was not exercised";
  EXPECT_GT(result.recentrings, 0)
      << "recentre() and shiftField() never fired, so this fixture covers none of the translation "
         "code it exists to put under the sanitizer";
  EXPECT_GT(result.finalMarchSteps, 1000)
      << "the final march did not run long enough to exercise the reactor sub-stepping";

  // ---- the state is physically sane. WIDE bounds on purpose: 150 cells is not converged, and a
  // tight bound here would be a physics assertion smuggled into a memory test.
  EXPECT_TRUE(std::isfinite(result.flameSpeed));
  EXPECT_GT(result.flameSpeed, 0.01);  // 1 cm/s
  EXPECT_LT(result.flameSpeed, 0.20);  // 20 cm/s
  EXPECT_TRUE(std::isfinite(result.peakTemperature));
  EXPECT_GT(result.peakTemperature, kUnburntT);
  EXPECT_LT(result.peakTemperature, 2.0 * kUnburntT + 1500.0);
  EXPECT_LT(result.maxCompositionSumDeviation, 1.0e-03);

  // The negative-mass-fraction bound is the one derived in TD-004's GATE_A3 C5b -- the linear
  // solve's acceptance fraction times the fuel scale -- loosened by an order for a coarse mesh.
  EXPECT_LT(std::abs(result.minMassFraction), 5.0e-10);

  for (Index c = 0; c < kCells; ++c) {
    ASSERT_TRUE(std::isfinite(result.temperature[static_cast<std::size_t>(c)]));
  }
}
