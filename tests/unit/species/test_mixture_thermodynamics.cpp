// P18.3 -- composition-dependent ideal-gas mixture thermodynamics.
//
// EVERY EXPECTED VALUE IN THIS FILE WAS COMPUTED OUTSIDE CFDApp, with Python's
// decimal module at 50 significant digits, and is embedded as a literal. The
// generator and its full output are preserved at
// results/p18/mixture_properties/data/reference_values.txt. No production
// mixture-property function was used to build any reference (authorization
// section 27).
//
// Thresholds: G5's acceptance clause -- each identity to <= 8 * eps * scale --
// and G5's BITWISE single-species limit. Neither is derived here; both are
// inherited from the frozen gate.
#include <gtest/gtest.h>

#include <cmath>
#include <limits>
#include <string>
#include <vector>

#include "cfd/compressible/ThermodynamicProperties.hpp"
#include "cfd/core/Exception.hpp"
#include "cfd/mesh/MeshGeometry.hpp"
#include "cfd/species/MassFractionState.hpp"
#include "cfd/species/MixtureThermodynamics.hpp"
#include "cfd/species/SpeciesRegistry.hpp"
#include "cfd/thermal/Enthalpy.hpp"
#include "cfd/thermal/TemperatureDependentProperty.hpp"

using cfd::Index;
using cfd::InvalidArgumentError;
using cfd::Real;
using cfd::compressible::ThermodynamicProperties;
using cfd::fields::ScalarField;
using cfd::mesh::Mesh;
using cfd::species::evaluateMixtureFields;
using cfd::species::kUniversalGasConstant;
using cfd::species::MassFractionState;
using cfd::species::MixtureThermodynamics;
using cfd::species::SpeciesRegistry;
using cfd::species::SpeciesThermodynamics;
using cfd::thermal::SensibleEnthalpy;
using cfd::thermal::TemperatureDependentProperty;

namespace {

constexpr Real kEps = std::numeric_limits<Real>::epsilon();

// The G5 allowance: 8 eps times the magnitude of the quantity being judged.
Real closure(Real scale) { return 8.0 * kEps * std::abs(scale); }

// ---- the test species ----
//
// Chosen so that a wrong averaging basis is UNMISTAKABLE: H2 and CO2 differ by
// 21.8x in molecular weight and 17.0x in cp. Section 13 asks for exactly this.
constexpr Real kMH2 = 0.002016;   // kg/mol
constexpr Real kMCO2 = 0.04401;   // kg/mol
constexpr Real kMN2 = 0.0280134;  // kg/mol
constexpr Real kCpH2 = 14307.0;   // J/(kg K)
constexpr Real kCpCO2 = 844.0;    // J/(kg K)
constexpr Real kCpN2 = 1040.0;    // J/(kg K)

// Reference R_i = R_u / M_i, from reference_values.txt.
constexpr Real kRH2 = 4124.237409798234;
constexpr Real kRCO2 = 188.9221226574242;
constexpr Real kRN2 = 296.80305204485137;

constexpr Real kP = 101325.0;
constexpr Real kT = 300.0;

SensibleEnthalpy constantCp(Real cp, const std::string& name) {
  // P15's own constant() factory plus its integrated sensible enthalpy -- no new
  // polynomial logic and no cp*T substitution (sections 5 and 15).
  return SensibleEnthalpy(TemperatureDependentProperty::constant(cp, name), 298.15, 0.0);
}

SpeciesThermodynamics h2() { return SpeciesThermodynamics(kMH2, constantCp(kCpH2, "cp_H2")); }
SpeciesThermodynamics co2() { return SpeciesThermodynamics(kMCO2, constantCp(kCpCO2, "cp_CO2")); }
SpeciesThermodynamics n2() { return SpeciesThermodynamics(kMN2, constantCp(kCpN2, "cp_N2")); }

MixtureThermodynamics binaryMixture() {
  std::vector<SpeciesThermodynamics> species;
  species.push_back(h2());
  species.push_back(co2());
  return MixtureThermodynamics(SpeciesRegistry({"H2", "CO2"}), std::move(species));
}

MixtureThermodynamics ternaryMixture() {
  std::vector<SpeciesThermodynamics> species;
  species.push_back(h2());
  species.push_back(co2());
  species.push_back(n2());
  return MixtureThermodynamics(SpeciesRegistry({"H2", "CO2", "N2"}), std::move(species));
}

MixtureThermodynamics pureH2Mixture() {
  std::vector<SpeciesThermodynamics> species;
  species.push_back(h2());
  return MixtureThermodynamics(SpeciesRegistry({"H2"}), std::move(species));
}

}  // namespace

// ===================================================================
// Per-species data -- section 2
// ===================================================================

TEST(SpeciesThermodynamicsTest, GasConstantIsDerivedFromMolecularWeight) {
  // R_i = R_u / M_i, against the independent reference.
  EXPECT_NEAR(h2().gasConstant(), kRH2, closure(kRH2));
  EXPECT_NEAR(co2().gasConstant(), kRCO2, closure(kRCO2));
  EXPECT_NEAR(n2().gasConstant(), kRN2, closure(kRN2));
  // and it really is R_u/M, not a stored duplicate that could drift
  EXPECT_DOUBLE_EQ(h2().gasConstant(), kUniversalGasConstant / kMH2);
}

TEST(SpeciesThermodynamicsTest, HeatCapacitiesAndGammaAgreeWithTheReference) {
  EXPECT_NEAR(h2().specificHeatPressure(kT), kCpH2, closure(kCpH2));
  EXPECT_NEAR(h2().specificHeatVolume(kT), 10182.762590201766, closure(10182.0));
  EXPECT_NEAR(h2().specificHeatRatio(kT), 1.405021463798707, closure(1.4));
  EXPECT_NEAR(co2().specificHeatVolume(kT), 655.0778773425758, closure(655.0));
  EXPECT_NEAR(co2().specificHeatRatio(kT), 1.2883964322285097, closure(1.29));
  EXPECT_NEAR(n2().specificHeatRatio(kT), 1.3993598908895994, closure(1.4));
}

TEST(SpeciesThermodynamicsTest, RejectsInvalidMolecularWeight) {
  const Real nan = std::numeric_limits<Real>::quiet_NaN();
  const Real inf = std::numeric_limits<Real>::infinity();
  EXPECT_THROW(SpeciesThermodynamics(0.0, constantCp(1000.0, "cp")), InvalidArgumentError);
  EXPECT_THROW(SpeciesThermodynamics(-1.0e-3, constantCp(1000.0, "cp")), InvalidArgumentError);
  EXPECT_THROW(SpeciesThermodynamics(nan, constantCp(1000.0, "cp")), InvalidArgumentError);
  EXPECT_THROW(SpeciesThermodynamics(inf, constantCp(1000.0, "cp")), InvalidArgumentError);
}

TEST(SpeciesThermodynamicsTest, RejectsAGasWithGammaAtOrBelowOne) {
  // cv = cp - R <= 0 is gamma <= 1: not an admissible ideal gas. Built by giving
  // a heavy-molecule species an implausibly small cp, so cp < R.
  const SpeciesThermodynamics bad(kMH2, constantCp(100.0, "cp_tiny"));  // cp=100 << R=4124
  EXPECT_THROW(bad.specificHeatVolume(kT), InvalidArgumentError);
  EXPECT_THROW(bad.specificHeatRatio(kT), InvalidArgumentError);
}

// ===================================================================
// Molecular weight -- section 3, control N1
// ===================================================================

TEST(MixtureThermodynamicsTest, MolecularWeightUsesTheMassFractionRelation) {
  // 1/M_mix = sum Y_i/M_i, against the independent reference.
  const auto mixture = binaryMixture();
  EXPECT_NEAR(mixture.molecularWeight({0.5, 0.5}), 0.003855393038717247,
              closure(0.003855393038717247));
  EXPECT_NEAR(mixture.molecularWeight({0.25, 0.75}), 0.007089708737864077,
              closure(0.007089708737864077));
  EXPECT_NEAR(ternaryMixture().molecularWeight({0.2, 0.3, 0.5}), 0.008072876581236042,
              closure(0.008072876581236042));
}

TEST(MixtureThermodynamicsTest, N1_ArithmeticAverageMolecularWeightIsDetected) {
  // CONTROL N1. The wrong formula is M = sum Y_i M_i. With H2/CO2 at 50/50 the
  // independent reference gives:
  //     correct (harmonic)   0.003855393038717247
  //     wrong   (arithmetic) 0.023013            -- 5.969x too large
  // so the criterion cannot fail to discriminate.
  const auto mixture = binaryMixture();
  const Real correct = mixture.molecularWeight({0.5, 0.5});
  const Real wrong = (0.5 * kMH2) + (0.5 * kMCO2);

  EXPECT_NEAR(wrong, 0.023013, closure(0.023013)) << "the reference wrong value itself";
  EXPECT_GT(wrong / correct, 5.0) << "the two formulas must differ unmistakably on this mixture";
  EXPECT_FALSE(std::abs(correct - wrong) <= closure(correct))
      << "the closure tolerance must NOT admit the arithmetic form";
}

TEST(MixtureThermodynamicsTest, N1_IsVacuousNearAPureSpeciesAndThatIsWhyItUsesAMixture) {
  // Recorded because it is the trap: near a pure species BOTH formulas agree.
  // The reference gives a ratio of 1.0000 for 1e-6 CO2 in H2. A control built on
  // a near-pure composition would pass while detecting nothing, so N1 above
  // deliberately uses 50/50.
  const auto mixture = binaryMixture();
  const Real correct = mixture.molecularWeight({1.0 - 1.0e-6, 1.0e-6});
  const Real wrong = ((1.0 - 1.0e-6) * kMH2) + (1.0e-6 * kMCO2);
  EXPECT_LT(std::abs(wrong / correct - 1.0), 1.0e-4)
      << "near-pure: the formulas agree, so this composition cannot discriminate";
}

TEST(MixtureThermodynamicsTest, MolecularWeightIsCompositionSymmetric) {
  // Section 3: swapping the species AND their mass fractions must give the same
  // mixture molecular weight.
  std::vector<SpeciesThermodynamics> swapped;
  swapped.push_back(co2());
  swapped.push_back(h2());
  const MixtureThermodynamics reversed(SpeciesRegistry({"CO2", "H2"}), std::move(swapped));
  EXPECT_DOUBLE_EQ(binaryMixture().molecularWeight({0.25, 0.75}),
                   reversed.molecularWeight({0.75, 0.25}));
}

// ===================================================================
// Gas constant, and the two formulations cross-checked -- section 4
// ===================================================================

TEST(MixtureThermodynamicsTest, GasConstantAgreesWithTheReference) {
  EXPECT_NEAR(binaryMixture().gasConstant({0.5, 0.5}), 2156.579766227829, closure(2156.58));
  EXPECT_NEAR(binaryMixture().gasConstant({0.25, 0.75}), 1172.7509444426266, closure(1172.75));
  EXPECT_NEAR(ternaryMixture().gasConstant({0.2, 0.3, 0.5}), 1029.9256447792998, closure(1029.93));
}

TEST(MixtureThermodynamicsTest, TheTwoGasConstantFormulationsAgree) {
  // Section 4: R_mix = sum Y_i R_i must equal R_u / M_mix. Both are exact for an
  // ideal-gas mixture; they are computed by different routes, so agreement to
  // round-off is evidence and disagreement beyond it would mean one is wrong.
  //
  // The MAXIMUM closure error over the matrix is reported.
  const auto binary = binaryMixture();
  const auto ternary = ternaryMixture();
  Real worst = 0.0;
  const std::vector<std::vector<Real>> binaryCases{
      {0.5, 0.5}, {0.25, 0.75}, {0.75, 0.25}, {1.0, 0.0}, {0.0, 1.0}, {1.0 - 1.0e-6, 1.0e-6}};
  for (const auto& composition : binaryCases) {
    const Real a = binary.gasConstant(composition);
    const Real b = binary.gasConstantFromMolecularWeight(composition);
    worst = std::max(worst, std::abs(a - b) / std::abs(a));
    EXPECT_NEAR(a, b, closure(a)) << "Y = {" << composition[0] << ", " << composition[1] << "}";
  }
  const std::vector<std::vector<Real>> ternaryCases{
      {0.2, 0.3, 0.5}, {1.0, 0.0, 0.0}, {0.0, 0.0, 1.0}, {1.0 / 3.0, 1.0 / 3.0, 1.0 / 3.0}};
  for (const auto& composition : ternaryCases) {
    const Real a = ternary.gasConstant(composition);
    const Real b = ternary.gasConstantFromMolecularWeight(composition);
    worst = std::max(worst, std::abs(a - b) / std::abs(a));
    EXPECT_NEAR(a, b, closure(a));
  }
  EXPECT_LE(worst, 8.0 * kEps) << "max relative R_mix closure error = " << worst;
}

// ===================================================================
// cp, cv, gamma -- sections 5, 6, 7, and controls N3, N4, N5
// ===================================================================

TEST(MixtureThermodynamicsTest, SpecificHeatIsMassWeighted) {
  EXPECT_NEAR(binaryMixture().specificHeatPressure({0.5, 0.5}, kT), 7575.5, closure(7575.5));
  EXPECT_NEAR(binaryMixture().specificHeatPressure({0.25, 0.75}, kT), 4209.75, closure(4209.75));
  EXPECT_NEAR(ternaryMixture().specificHeatPressure({0.2, 0.3, 0.5}, kT), 3634.6, closure(3634.6));
}

TEST(MixtureThermodynamicsTest, N3_SimpleMeanSpecificHeatIsDetected) {
  // CONTROL N3: a simple (unweighted) mean instead of mass weighting. For the
  // 25/75 mixture the mass-weighted value is 4209.75 and the simple mean is
  // (14307 + 844)/2 = 7575.5 -- different by 80%.
  const Real massWeighted = binaryMixture().specificHeatPressure({0.25, 0.75}, kT);
  const Real simpleMean = (kCpH2 + kCpCO2) / 2.0;
  EXPECT_NEAR(massWeighted, 4209.75, closure(4209.75));
  EXPECT_NEAR(simpleMean, 7575.5, closure(7575.5));
  EXPECT_FALSE(std::abs(massWeighted - simpleMean) <= closure(massWeighted))
      << "the tolerance must not admit an unweighted mean";
}

TEST(MixtureThermodynamicsTest, CvAndGammaComeFromTheExistingClosure) {
  // Sections 6 and 7: cv = cp - R and gamma = cp/cv, and neither is stored
  // independently -- they are derived by ThermodynamicProperties.
  const auto properties = binaryMixture().properties({0.5, 0.5}, kT);
  EXPECT_NEAR(properties.specificHeatVolume(), 5418.920233772171, closure(5418.92));
  EXPECT_NEAR(properties.specificHeatRatio(), 1.3979722293728265, closure(1.4));
  EXPECT_NEAR(ternaryMixture().properties({0.2, 0.3, 0.5}, kT).specificHeatVolume(),
              2604.6743552207004, closure(2604.67));
  EXPECT_NEAR(ternaryMixture().properties({0.2, 0.3, 0.5}, kT).specificHeatRatio(),
              1.395414360614777, closure(1.4));
}

TEST(MixtureThermodynamicsTest, N5_DirectlyAveragedGammaIsDetected) {
  // CONTROL N5: gamma must EMERGE from cp and cv, never be averaged from gamma_i.
  // For 50/50 H2/CO2 the correct value is 1.39797 while the mass-weighted mean of
  // the species gammas is (1.40502 + 1.28840)/2 = 1.34671 -- a 3.7% error, far
  // outside the closure tolerance.
  const Real correct = binaryMixture().properties({0.5, 0.5}, kT).specificHeatRatio();
  const Real averaged = (0.5 * h2().specificHeatRatio(kT)) + (0.5 * co2().specificHeatRatio(kT));
  EXPECT_NEAR(correct, 1.3979722293728265, closure(1.4));
  EXPECT_GT(std::abs(correct - averaged), 0.03) << "averaged gamma = " << averaged;
  EXPECT_FALSE(std::abs(correct - averaged) <= closure(correct));
}

// ===================================================================
// Thermodynamic closure -- section 8, THE principal G5 criterion
// ===================================================================

TEST(MixtureThermodynamicsTest, ClosureIdentitiesHoldAcrossTheMatrix) {
  // Section 8: cp - cv = R and gamma = cp/cv, over pure, binary, ternary,
  // near-pure and equal mixtures, at several temperatures.
  //
  // The MAXIMUM error in each identity is reported.
  const auto binary = binaryMixture();
  const auto ternary = ternaryMixture();
  Real worstCpCvR = 0.0;
  Real worstGamma = 0.0;

  auto check = [&](const MixtureThermodynamics& mixture, const std::vector<Real>& composition,
                   Real temperature) {
    const auto properties = mixture.properties(composition, temperature);
    const Real cp = properties.specificHeatPressure();
    const Real cv = properties.specificHeatVolume();
    const Real r = properties.gasConstant();
    const Real gamma = properties.specificHeatRatio();

    const Real cpCvR = std::abs((cp - cv) - r);
    EXPECT_LE(cpCvR, closure(r)) << "cp - cv - R at T = " << temperature;
    worstCpCvR = std::max(worstCpCvR, cpCvR / std::abs(r));

    const Real gammaError = std::abs(gamma - (cp / cv));
    EXPECT_LE(gammaError, closure(gamma)) << "gamma vs cp/cv at T = " << temperature;
    worstGamma = std::max(worstGamma, gammaError / std::abs(gamma));

    EXPECT_GT(cv, 0.0);
    EXPECT_GT(gamma, 1.0) << "an ideal gas must have gamma > 1";
  };

  for (const Real temperature : {250.0, 300.0, 800.0, 2000.0}) {
    check(binary, {1.0, 0.0}, temperature);
    check(binary, {0.0, 1.0}, temperature);
    check(binary, {0.5, 0.5}, temperature);
    check(binary, {0.25, 0.75}, temperature);
    check(binary, {1.0 - 1.0e-9, 1.0e-9}, temperature);
    check(ternary, {0.2, 0.3, 0.5}, temperature);
    check(ternary, {1.0 / 3.0, 1.0 / 3.0, 1.0 / 3.0}, temperature);
  }
  EXPECT_LE(worstCpCvR, 8.0 * kEps) << "max relative |cp - cv - R| = " << worstCpCvR;
  EXPECT_LE(worstGamma, 8.0 * kEps) << "max relative gamma error = " << worstGamma;
}

// ===================================================================
// The single-species limit -- section 10, G5's BITWISE clause
// ===================================================================

TEST(MixtureThermodynamicsTest, SingleSpeciesLimitReproducesP17Bitwise) {
  // G5: "THE SINGLE-SPECIES LIMIT IS BITWISE ... anything other than bitwise
  // means the mixture path is not a generalisation of the existing one."
  //
  // Compared against a ThermodynamicProperties built DIRECTLY the way P17 builds
  // it, from (R_1, cp_1). Every quantity it derives must match bit for bit.
  const auto mixture = pureH2Mixture();
  const auto mixed = mixture.properties({1.0}, kT);
  const ThermodynamicProperties p17(kUniversalGasConstant / kMH2, kCpH2);

  EXPECT_EQ(mixed.gasConstant(), p17.gasConstant());
  EXPECT_EQ(mixed.specificHeatPressure(), p17.specificHeatPressure());
  EXPECT_EQ(mixed.specificHeatVolume(), p17.specificHeatVolume());
  EXPECT_EQ(mixed.specificHeatRatio(), p17.specificHeatRatio());
  EXPECT_EQ(mixed.density(kP, kT), p17.density(kP, kT));
  EXPECT_EQ(mixed.speedOfSound(kT), p17.speedOfSound(kT));
}

TEST(MixtureThermodynamicsTest, SingleSpeciesLimitIsBitwiseAtEveryPureEndpointOfAMixture) {
  // Section 11: for EVERY species i, Y_i = 1 with the rest zero must recover that
  // species' own properties. Run on the TERNARY mixture, so the zero terms are
  // genuinely present in the sums and must contribute exactly nothing.
  const auto mixture = ternaryMixture();
  const std::vector<SpeciesThermodynamics> pure{h2(), co2(), n2()};
  for (Index i = 0; i < 3; ++i) {
    std::vector<Real> composition(3, 0.0);
    composition[i] = 1.0;
    const auto mixed = mixture.properties(composition, kT);
    const ThermodynamicProperties expected(pure[i].gasConstant(), pure[i].specificHeatPressure(kT));
    EXPECT_EQ(mixed.gasConstant(), expected.gasConstant()) << "species " << i;
    EXPECT_EQ(mixed.specificHeatPressure(), expected.specificHeatPressure()) << "species " << i;
    EXPECT_EQ(mixed.specificHeatVolume(), expected.specificHeatVolume()) << "species " << i;
    EXPECT_EQ(mixed.specificHeatRatio(), expected.specificHeatRatio()) << "species " << i;
    EXPECT_EQ(mixed.density(kP, kT), expected.density(kP, kT)) << "species " << i;
  }
}

TEST(MixtureThermodynamicsTest, SingleSpeciesMolecularWeightRecoveryIsMeasuredNotClaimedBitwise) {
  // M_mix = 1/(1/M_1) carries a RECIPROCAL PAIR, and 1/(1/x) is not bitwise for
  // 17.2% of random doubles (audit.md section 3). P17 has no molecular weight at
  // all, so G5's bitwise clause -- which binds "P17's existing single-species
  // value" -- does not reach M.
  //
  // So the recovery is MEASURED against the 8-eps closure allowance and REPORTED,
  // not asserted bitwise. Claiming bitwise here would be claiming a property of
  // IEEE arithmetic that does not hold.
  const auto mixture = ternaryMixture();
  const std::vector<Real> molecularWeights{kMH2, kMCO2, kMN2};
  Real worst = 0.0;
  for (Index i = 0; i < 3; ++i) {
    std::vector<Real> composition(3, 0.0);
    composition[i] = 1.0;
    const Real recovered = mixture.molecularWeight(composition);
    worst = std::max(worst, std::abs(recovered - molecularWeights[i]) / molecularWeights[i]);
    EXPECT_NEAR(recovered, molecularWeights[i], closure(molecularWeights[i])) << "species " << i;
  }
  EXPECT_LE(worst, 8.0 * kEps) << "max relative single-species M recovery error = " << worst;
}

// ===================================================================
// Composition-dependent EOS -- sections 9, 18, 19
// ===================================================================

TEST(MixtureThermodynamicsTest, DensityAgreesWithTheReference) {
  EXPECT_NEAR(binaryMixture().density({0.5, 0.5}, kP, kT), 0.15661372942897156,
              closure(0.15661372942897156));
  EXPECT_NEAR(binaryMixture().density({0.25, 0.75}, kP, kT), 0.28799806267521055,
              closure(0.28799806267521055));
  EXPECT_NEAR(ternaryMixture().density({0.2, 0.3, 0.5}, kP, kT), 0.3279362949277524,
              closure(0.3279362949277524));
}

TEST(MixtureThermodynamicsTest, N6_DensityActuallyChangesWithCompositionAtFixedPAndT) {
  // CONTROL N6 and section 18: "A mixture API that calculates R_mix but leaves
  // density using the old constant R is incomplete."
  //
  // Identical p and T, four different compositions. The densities must differ by
  // a large factor, and each must equal p/(R_mix T) computed from the reported
  // R_mix -- so density is provably routed through the composition.
  const auto mixture = binaryMixture();
  const std::vector<std::vector<Real>> compositions{
      {1.0, 0.0}, {0.75, 0.25}, {0.5, 0.5}, {0.0, 1.0}};
  std::vector<Real> densities;
  for (const auto& composition : compositions) {
    const Real rho = mixture.density(composition, kP, kT);
    const Real rMix = mixture.gasConstant(composition);
    EXPECT_NEAR(rho, kP / (rMix * kT), closure(rho)) << "rho must be p/(R_mix T)";
    densities.push_back(rho);
  }
  // Pure CO2 against pure H2 is the full molecular-weight ratio, ~21.8x.
  EXPECT_GT(densities.back() / densities.front(), 20.0)
      << "density did not respond to composition: " << densities.front() << " vs "
      << densities.back();
  // strictly increasing as the heavy species replaces the light one
  for (Index i = 1; i < densities.size(); ++i) {
    EXPECT_GT(densities[i], densities[i - 1]);
  }
}

TEST(MixtureThermodynamicsTest, EosRoundTripsAreConsistent) {
  // Section 19: (p,T,Y) -> rho, (rho,T,Y) -> p, (p,rho,Y) -> T. Worst error
  // reported.
  const auto binary = binaryMixture();
  const auto ternary = ternaryMixture();
  Real worst = 0.0;
  auto roundTrip = [&](const MixtureThermodynamics& mixture, const std::vector<Real>& composition,
                       Real pressure, Real temperature) {
    const auto properties = mixture.properties(composition, temperature);
    const Real rho = properties.density(pressure, temperature);
    const Real pBack = properties.equationOfState().pressure(rho, temperature);
    const Real tBack = properties.equationOfState().temperature(pressure, rho);
    EXPECT_NEAR(pBack, pressure, closure(pressure));
    EXPECT_NEAR(tBack, temperature, closure(temperature));
    worst = std::max(worst, std::abs(pBack - pressure) / pressure);
    worst = std::max(worst, std::abs(tBack - temperature) / temperature);
  };
  for (const Real pressure : {50000.0, 101325.0, 500000.0}) {
    for (const Real temperature : {250.0, 300.0, 1200.0}) {
      roundTrip(binary, {0.5, 0.5}, pressure, temperature);
      roundTrip(binary, {0.25, 0.75}, pressure, temperature);
      roundTrip(binary, {1.0, 0.0}, pressure, temperature);
      roundTrip(ternary, {0.2, 0.3, 0.5}, pressure, temperature);
    }
  }
  EXPECT_LE(worst, 8.0 * kEps) << "worst relative EOS round-trip error = " << worst;
}

// ===================================================================
// Enthalpy and internal energy -- section 16
// ===================================================================

TEST(MixtureThermodynamicsTest, MixtureEnthalpyIsMassWeightedAndInternalEnergyFollows) {
  // h_mix = sum Y_i h_i(T), each h_i INTEGRATED by P15 -- so for a constant cp
  // with T_ref = 298.15 the reference is cp*(T - T_ref), computed here
  // independently rather than by calling the production function.
  const auto mixture = binaryMixture();
  const Real temperature = 500.0;
  const Real expectedH2 = kCpH2 * (temperature - 298.15);
  const Real expectedCO2 = kCpCO2 * (temperature - 298.15);
  const Real expected = (0.5 * expectedH2) + (0.5 * expectedCO2);
  EXPECT_NEAR(mixture.enthalpy({0.5, 0.5}, temperature), expected, closure(expected));

  // e = h - R T, with R_mix
  const Real rMix = mixture.gasConstant({0.5, 0.5});
  EXPECT_NEAR(mixture.internalEnergy({0.5, 0.5}, temperature), expected - (rMix * temperature),
              closure(expected));
}

TEST(MixtureThermodynamicsTest, SingleSpeciesEnthalpyMatchesP15Exactly) {
  // The mixture must not re-base P15's reference conventions (section 15).
  const auto mixture = pureH2Mixture();
  const auto species = h2();
  for (const Real temperature : {300.0, 500.0, 1500.0}) {
    EXPECT_EQ(mixture.enthalpy({1.0}, temperature), species.enthalpy(temperature));
  }
}

// ===================================================================
// Composition sensitivity -- section 14
// ===================================================================

TEST(MixtureThermodynamicsTest, PropertiesVaryContinuouslyAcrossTheCompositionRange) {
  // Section 14: sweep Y_A from 0 to 1 and check continuity and endpoints.
  //
  // MONOTONICITY IS ASSERTED ONLY WHERE IT IS DERIVED. R_mix = Y_A R_A + Y_B R_B
  // is LINEAR in Y_A, so it is strictly monotone; cp_mix likewise. rho =
  // p/(R_mix T) is then strictly monotone in the opposite sense. gamma_mix is
  // NOT asserted monotone -- it is a ratio of two affine functions and section 14
  // says not to impose monotonicity that has not been derived.
  const auto mixture = binaryMixture();
  const Index steps = 40;
  Real previousR = -1.0;
  Real previousRho = std::numeric_limits<Real>::max();
  for (Index k = 0; k <= steps; ++k) {
    const Real yA = static_cast<Real>(k) / static_cast<Real>(steps);
    const std::vector<Real> composition{yA, 1.0 - yA};
    const auto properties = mixture.properties(composition, kT);
    const Real r = properties.gasConstant();
    const Real rho = properties.density(kP, kT);
    EXPECT_GT(r, previousR) << "R_mix is linear in Y_A and must increase";
    EXPECT_LT(rho, previousRho) << "rho = p/(R_mix T) must decrease as R_mix increases";
    EXPECT_GT(properties.specificHeatRatio(), 1.0);
    previousR = r;
    previousRho = rho;
  }
  // endpoints, bitwise against the pure species
  EXPECT_EQ(mixture.properties({1.0, 0.0}, kT).gasConstant(), h2().gasConstant());
  EXPECT_EQ(mixture.properties({0.0, 1.0}, kT).gasConstant(), co2().gasConstant());
}

// ===================================================================
// Invalid input -- sections 20, 21, control N7
// ===================================================================

TEST(MixtureThermodynamicsTest, RejectsMismatchedShapes) {
  const auto mixture = binaryMixture();
  EXPECT_THROW(mixture.gasConstant({0.5, 0.3, 0.2}), InvalidArgumentError);
  EXPECT_THROW(mixture.molecularWeight({1.0}), InvalidArgumentError);
  EXPECT_THROW(mixture.specificHeatPressure({1.0}, kT), InvalidArgumentError);
  EXPECT_THROW(mixture.species(2), InvalidArgumentError);

  std::vector<SpeciesThermodynamics> one;
  one.push_back(h2());
  EXPECT_THROW(MixtureThermodynamics(SpeciesRegistry({"H2", "CO2"}), std::move(one)),
               InvalidArgumentError);
}

TEST(MixtureThermodynamicsTest, N7_AnInvalidCompositionCannotReachTheMixtureThroughTheTypedPath) {
  // CONTROL N7. The mixture takes its per-cell composition from a P18.1
  // MassFractionState, which REFUSES an invalid composition at construction --
  // so an invalid composition cannot reach the property evaluation at all. That
  // is stronger than checking one, and it is why section 20's "do not duplicate
  // P18.1 validation" is satisfiable.
  const Mesh mesh = cfd::mesh::MeshGeometry::createCartesian2D(2, 2, 1.0, 1.0);
  const Index n = mesh.numberOfCells();
  EXPECT_THROW(MassFractionState(SpeciesRegistry({"H2", "CO2"}), n, {0.5, 0.4}),
               InvalidArgumentError);  // sums to 0.9
  EXPECT_THROW(MassFractionState(SpeciesRegistry({"H2", "CO2"}), n, {-0.1, 1.1}),
               InvalidArgumentError);  // out of bounds, sum exactly 1
  EXPECT_THROW(MassFractionState(SpeciesRegistry({"H2", "CO2"}), n,
                                 {std::numeric_limits<Real>::quiet_NaN(), 1.0}),
               InvalidArgumentError);

  // And no renormalisation happens anywhere: a valid state's composition comes
  // back out of the mixture path untouched.
  const MassFractionState state(SpeciesRegistry({"H2", "CO2"}), n, {0.25, 0.75});
  const auto composition = state.cellComposition(0);
  EXPECT_EQ(composition[0], 0.25);
  EXPECT_EQ(composition[1], 0.75);
}

TEST(MixtureThermodynamicsTest, RejectsAnAllZeroCompositionForMolecularWeight) {
  // Not reachable through MassFractionState, but the free function must still
  // refuse rather than return inf.
  EXPECT_THROW(binaryMixture().molecularWeight({0.0, 0.0}), InvalidArgumentError);
}

// ===================================================================
// The P18.2 -> P18.3 join -- section 25
// ===================================================================

TEST(MixtureThermodynamicsTest, SpatiallyVaryingCompositionGivesSpatiallyVaryingProperties) {
  // Section 25: each cell must obtain properties from ITS OWN local composition.
  const Mesh mesh = cfd::mesh::MeshGeometry::createCartesian2D(8, 1, 1.0, 1.0);
  const Index n = mesh.numberOfCells();

  ScalarField h2Field(n);
  ScalarField co2Field(n);
  for (Index c = 0; c < n; ++c) {
    const Real t = static_cast<Real>(c) / static_cast<Real>(n - 1);
    h2Field[c] = 0.05 + (0.9 * t);
    co2Field[c] = 1.0 - h2Field[c];
  }
  const MassFractionState state(SpeciesRegistry({"H2", "CO2"}), {h2Field, co2Field});
  const ScalarField pressure(n, kP);
  const ScalarField temperature(n, kT);

  const auto fields = evaluateMixtureFields(binaryMixture(), state, pressure, temperature);

  // Every field must actually vary across the domain, not merely exist.
  EXPECT_GT(fields.gasConstant[n - 1] / fields.gasConstant[0], 5.0);
  EXPECT_GT(fields.specificHeatPressure[n - 1] / fields.specificHeatPressure[0], 5.0);
  EXPECT_GT(fields.density[0] / fields.density[n - 1], 5.0);
  EXPECT_GT(fields.molecularWeight[0] / fields.molecularWeight[n - 1], 5.0);

  // And each cell must agree with a direct evaluation of its own composition.
  const auto mixture = binaryMixture();
  for (Index c = 0; c < n; ++c) {
    const auto composition = state.cellComposition(c);
    const auto properties = mixture.properties(composition, kT);
    EXPECT_EQ(fields.gasConstant[c], properties.gasConstant()) << "cell " << c;
    EXPECT_EQ(fields.specificHeatVolume[c], properties.specificHeatVolume()) << "cell " << c;
    EXPECT_EQ(fields.specificHeatRatio[c], properties.specificHeatRatio()) << "cell " << c;
    EXPECT_EQ(fields.density[c], properties.density(kP, kT)) << "cell " << c;
  }
}

TEST(MixtureThermodynamicsTest, RejectsAStateWhoseRegistryDiffersFromTheMixture) {
  // Ordering is part of a registry's value (P18.1), so a mismatch would read
  // every composition against the wrong species -- silently.
  const Mesh mesh = cfd::mesh::MeshGeometry::createCartesian2D(2, 2, 1.0, 1.0);
  const Index n = mesh.numberOfCells();
  const MassFractionState reversed(SpeciesRegistry({"CO2", "H2"}), n, {0.5, 0.5});
  const ScalarField pressure(n, kP);
  const ScalarField temperature(n, kT);
  EXPECT_THROW(evaluateMixtureFields(binaryMixture(), reversed, pressure, temperature),
               InvalidArgumentError);
}

// ===================================================================
// Determinism -- section 22
// ===================================================================

TEST(MixtureThermodynamicsTest, MixturePropertiesAreBitwiseDeterministic) {
  const auto mixture = ternaryMixture();
  const std::vector<Real> composition{0.2, 0.3, 0.5};
  const Real firstM = mixture.molecularWeight(composition);
  const auto firstProperties = mixture.properties(composition, kT);
  const Real firstRho = mixture.density(composition, kP, kT);
  const Real firstH = mixture.enthalpy(composition, kT);

  for (int repeat = 0; repeat < 50; ++repeat) {
    EXPECT_EQ(mixture.molecularWeight(composition), firstM) << "repeat " << repeat;
    const auto properties = mixture.properties(composition, kT);
    EXPECT_EQ(properties.gasConstant(), firstProperties.gasConstant());
    EXPECT_EQ(properties.specificHeatPressure(), firstProperties.specificHeatPressure());
    EXPECT_EQ(properties.specificHeatVolume(), firstProperties.specificHeatVolume());
    EXPECT_EQ(properties.specificHeatRatio(), firstProperties.specificHeatRatio());
    EXPECT_EQ(mixture.density(composition, kP, kT), firstRho);
    EXPECT_EQ(mixture.enthalpy(composition, kT), firstH);
  }
}

// ===================================================================
// The remaining controls -- N2, N4, N8 -- and section 6's cv cross-check
// ===================================================================

TEST(MixtureThermodynamicsTest, N2_RMixIsNotLeftConstant) {
  // CONTROL N2: R_mix must respond to composition. If it were left at a constant
  // (a single species' value, say) every downstream quantity would be wrong
  // while still satisfying the closure identities -- so the identities alone
  // cannot catch this, and a dedicated control is needed.
  const auto mixture = binaryMixture();
  const Real pureH2 = mixture.gasConstant({1.0, 0.0});
  const Real half = mixture.gasConstant({0.5, 0.5});
  const Real pureCO2 = mixture.gasConstant({0.0, 1.0});

  // The spread across the composition range is the full R ratio, ~21.8x.
  EXPECT_GT(pureH2 / pureCO2, 20.0) << "R_mix did not respond to composition";
  // and the 50/50 value is the exact arithmetic mean of the two, because
  // R_mix = sum Y_i R_i is LINEAR in Y -- a derived property, not an observation.
  EXPECT_NEAR(half, 0.5 * (pureH2 + pureCO2), closure(half));
  // A constant-R substitution would make all three equal; they are not.
  EXPECT_FALSE(std::abs(half - pureH2) <= closure(half));
  EXPECT_FALSE(std::abs(half - pureCO2) <= closure(half));
}

TEST(MixtureThermodynamicsTest, N4_CvFromTheClosureAgreesWithTheSpeciesSum) {
  // SECTION 6 asks for cv_mix = cp_mix - R_mix to be verified INDEPENDENTLY
  // against the species-sum representation, and the discrepancy reported.
  //
  // THE TWO ARE ALGEBRAICALLY IDENTICAL, and that is worth stating because it is
  // the reason the check is a consistency test rather than a physics test:
  //     sum_i Y_i cv_i = sum_i Y_i (cp_i - R_i) = cp_mix - R_mix
  // So they must agree to round-off. A DISAGREEMENT beyond that would mean one of
  // the weightings is wrong -- which is exactly what N3 and N4 guard.
  //
  // This is also CONTROL N4: cv is never stored independently, so it cannot drift
  // from cp and R. If it were computed by a separate path with a different
  // weighting, this cross-check would fail.
  const auto binary = binaryMixture();
  const auto ternary = ternaryMixture();
  const std::vector<SpeciesThermodynamics> binarySpecies{h2(), co2()};
  const std::vector<SpeciesThermodynamics> ternarySpecies{h2(), co2(), n2()};
  Real worst = 0.0;

  auto check = [&](const MixtureThermodynamics& mixture,
                   const std::vector<SpeciesThermodynamics>& species,
                   const std::vector<Real>& composition, Real temperature) {
    const Real fromClosure = mixture.properties(composition, temperature).specificHeatVolume();
    Real fromSpeciesSum = 0.0;
    for (Index i = 0; i < composition.size(); ++i) {
      fromSpeciesSum += composition[i] * species[i].specificHeatVolume(temperature);
    }
    EXPECT_NEAR(fromClosure, fromSpeciesSum, closure(fromClosure))
        << "cv from cp-R = " << fromClosure << " but from sum Y_i cv_i = " << fromSpeciesSum;
    worst = std::max(worst, std::abs(fromClosure - fromSpeciesSum) / std::abs(fromClosure));
  };

  for (const Real temperature : {250.0, 300.0, 1200.0}) {
    check(binary, binarySpecies, {0.5, 0.5}, temperature);
    check(binary, binarySpecies, {0.25, 0.75}, temperature);
    check(binary, binarySpecies, {1.0, 0.0}, temperature);
    check(ternary, ternarySpecies, {0.2, 0.3, 0.5}, temperature);
    check(ternary, ternarySpecies, {1.0 / 3.0, 1.0 / 3.0, 1.0 / 3.0}, temperature);
  }
  EXPECT_LE(worst, 8.0 * kEps) << "max relative cv discrepancy between the two forms = " << worst;
}

TEST(MixtureThermodynamicsTest, N8_TheSingleSpeciesP17ResultIsUnchanged) {
  // CONTROL N8, stated as its own test so the intent is explicit: routing a
  // single species through the mixture path must not change P17's numbers AT ALL.
  //
  // This is the control G5 names first ("a mixture of one species that does NOT
  // reproduce P17's value bitwise must FAIL"), and it is checked here for a
  // matrix of pressures and temperatures rather than one point -- a single point
  // could agree by luck.
  const auto mixture = pureH2Mixture();
  const ThermodynamicProperties p17(kUniversalGasConstant / kMH2, kCpH2);

  for (const Real temperature : {200.0, 300.0, 700.0, 1800.0, 3000.0}) {
    const auto mixed = mixture.properties({1.0}, temperature);
    EXPECT_EQ(mixed.gasConstant(), p17.gasConstant()) << "T = " << temperature;
    EXPECT_EQ(mixed.specificHeatPressure(), p17.specificHeatPressure()) << "T = " << temperature;
    EXPECT_EQ(mixed.specificHeatVolume(), p17.specificHeatVolume()) << "T = " << temperature;
    EXPECT_EQ(mixed.specificHeatRatio(), p17.specificHeatRatio()) << "T = " << temperature;
    EXPECT_EQ(mixed.speedOfSound(temperature), p17.speedOfSound(temperature))
        << "T = " << temperature;
    for (const Real pressure : {1000.0, 101325.0, 5.0e6}) {
      EXPECT_EQ(mixed.density(pressure, temperature), p17.density(pressure, temperature))
          << "p = " << pressure << " T = " << temperature;
      EXPECT_EQ(mixed.equationOfState().pressure(1.0, temperature),
                p17.equationOfState().pressure(1.0, temperature));
      EXPECT_EQ(mixed.equationOfState().temperature(pressure, 1.0),
                p17.equationOfState().temperature(pressure, 1.0));
    }
  }
}

TEST(MixtureThermodynamicsTest, TemperatureDependentCpFlowsThroughTheMixture) {
  // SECTION 15: if P15 supports cp_i(T), the mixture must use that same source
  // rather than reverting to a constant.
  //
  // A two-term polynomial cp(T) = a0 + a1 T for each species, so cp_mix must vary
  // with T -- and the reference is computed here independently from the
  // coefficients, not by calling the production function.
  const Real a0H2 = 14000.0;
  const Real a1H2 = 1.5;
  const Real a0CO2 = 800.0;
  const Real a1CO2 = 0.2;
  std::vector<SpeciesThermodynamics> species;
  species.push_back(SpeciesThermodynamics(
      kMH2, SensibleEnthalpy(TemperatureDependentProperty({a0H2, a1H2}, 200.0, 3000.0, "cp_H2(T)"),
                             298.15, 0.0)));
  species.push_back(SpeciesThermodynamics(
      kMCO2,
      SensibleEnthalpy(TemperatureDependentProperty({a0CO2, a1CO2}, 200.0, 3000.0, "cp_CO2(T)"),
                       298.15, 0.0)));
  const MixtureThermodynamics mixture(SpeciesRegistry({"H2", "CO2"}), std::move(species));

  const std::vector<Real> composition{0.4, 0.6};
  Real previous = -1.0;
  for (const Real temperature : {300.0, 600.0, 1200.0, 2400.0}) {
    const Real expected =
        (0.4 * (a0H2 + (a1H2 * temperature))) + (0.6 * (a0CO2 + (a1CO2 * temperature)));
    const Real actual = mixture.specificHeatPressure(composition, temperature);
    EXPECT_NEAR(actual, expected, closure(expected)) << "T = " << temperature;
    // and it must genuinely vary with T, or the test proves nothing
    EXPECT_GT(actual, previous) << "cp_mix must increase with T for a1 > 0";
    previous = actual;

    // the closure must still hold at every temperature
    const auto properties = mixture.properties(composition, temperature);
    EXPECT_NEAR((properties.specificHeatPressure() - properties.specificHeatVolume()),
                properties.gasConstant(), closure(properties.gasConstant()));
    EXPECT_GT(properties.specificHeatRatio(), 1.0);
  }

  // gamma_mix now varies with BOTH T and Y -- which is precisely the
  // "variable-gamma" capability TotalEnergyState's refusal names as its
  // prerequisite. See single_species_compatibility.md for the extension point.
  const Real gammaCold = mixture.properties(composition, 300.0).specificHeatRatio();
  const Real gammaHot = mixture.properties(composition, 2400.0).specificHeatRatio();
  EXPECT_NE(gammaCold, gammaHot) << "gamma must respond to temperature under a cp(T) model";
}
