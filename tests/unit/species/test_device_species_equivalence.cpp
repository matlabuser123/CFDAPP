// P18.8 -- the cross-comparison section 4 requires for restated formulas, LINK ONE OF TWO.
//
// include/cfd/gpu/DeviceSpeciesThermochemistry.hpp restates, for the device, the P18.3-P18.6
// thermochemistry that cannot cross because MixtureThermodynamics owns std::vector-backed property
// models and ReactionMechanism owns std::strings. This file compares those restatements against
// PRODUCTION, and what it demands is BITWISE agreement -- not closeness -- because the two are
// supposed to be the same arithmetic in the same order.
//
// WHY THIS FILE IS NOT UNDER tests/unit/gpu. The shared header uses CFD_HLLC_HOST_DEVICE, which is
// empty under gcc, so it compiles into the CPU build unchanged. Putting the test here means it runs
// in the CPU-ONLY configuration too, so the restatement is checked against production on a machine
// with no GPU and a CUDA-OFF regression cannot silently stop covering it. This project has a
// recorded finding about exactly that blind spot.
//
// WHAT THIS FILE DOES NOT PROVE. It compares production against the shared header COMPILED FOR THE
// HOST. It says nothing about whether nvcc's DEVICE code agrees with nvcc's host code -- that is
// the other link, measured in tests/unit/gpu/test_device_species_gpu_equivalence.cpp. A single
// end-to-end comparison would conflate the two and, on a failure, name neither. The structure is
// P17.10's, followed deliberately.
#include <gtest/gtest.h>

#include <cmath>
#include <limits>
#include <string>
#include <vector>

#include "cfd/core/Exception.hpp"
#include "cfd/gpu/DeviceSpeciesThermochemistry.hpp"
#include "cfd/species/ArrheniusKinetics.hpp"
#include "cfd/species/HeatRelease.hpp"
#include "cfd/species/MassFractionState.hpp"
#include "cfd/species/MixtureThermodynamics.hpp"
#include "cfd/species/ReactionMechanism.hpp"
#include "cfd/species/SpeciesRegistry.hpp"
#include "cfd/thermal/Enthalpy.hpp"
#include "cfd/thermal/TemperatureDependentProperty.hpp"

using cfd::Index;
using cfd::InvalidArgumentError;
using cfd::NumericalError;
using cfd::Real;
using cfd::fields::ScalarField;
using cfd::gpu::DeviceArrhenius;
using cfd::gpu::DeviceMechanism;
using cfd::gpu::DeviceReaction;
using cfd::gpu::DeviceReactorStep;
using cfd::gpu::DeviceSpecies;
using cfd::gpu::DeviceSpeciesControls;
using cfd::gpu::DeviceSpeciesStatus;
using cfd::gpu::kDeviceUniversalGasConstant;
using cfd::species::ArrheniusParameters;
using cfd::species::arrheniusProgressRates;
using cfd::species::energyDensity;
using cfd::species::FormationEnthalpies;
using cfd::species::kUniversalGasConstant;
using cfd::species::MassFractionState;
using cfd::species::MixtureThermodynamics;
using cfd::species::molarConcentrations;
using cfd::species::Reaction;
using cfd::species::ReactionDirection;
using cfd::species::reactionHeatRelease;
using cfd::species::ReactionKinetics;
using cfd::species::ReactionMechanism;
using cfd::species::ReactionProgressRates;
using cfd::species::ReactionRateParameters;
using cfd::species::reactionSpeciesSources;
using cfd::species::SpeciesRegistry;
using cfd::species::SpeciesThermodynamics;
using cfd::species::standardReactionEnthalpy;
using cfd::species::temperatureFromSensibleEnergy;
using cfd::thermal::SensibleEnthalpy;
using cfd::thermal::TemperatureDependentProperty;

namespace {

// ---------------------------------------------------------------- the paired case
// ONE set of scalars builds BOTH the production objects and the device POD, so the two cannot
// describe different physics. That is the whole point: a test that built them separately could
// pass while comparing two different mechanisms.
struct CaseScalars {
  std::vector<std::string> names;
  std::vector<Real> molecularWeight;
  std::vector<Real> specificHeat;
  std::vector<Real> formationEnthalpy;
  // one reaction per entry: reactant and product coefficient per species
  std::vector<std::vector<Real>> reactant;
  std::vector<std::vector<Real>> product;
  std::vector<bool> reversible;
  std::vector<Real> preExponential;
  std::vector<Real> exponent;
  std::vector<Real> activationEnergy;
  std::vector<Real> reversePreExponential;
  Real referenceTemperature{298.15};
};

SpeciesRegistry registryOf(const CaseScalars& c) { return SpeciesRegistry(c.names); }

MixtureThermodynamics mixtureOf(const CaseScalars& c) {
  std::vector<SpeciesThermodynamics> species;
  for (std::size_t i = 0; i < c.names.size(); ++i) {
    species.push_back(SpeciesThermodynamics(
        c.molecularWeight[i], SensibleEnthalpy(TemperatureDependentProperty::constant(
                                                   c.specificHeat[i], "cp_" + c.names[i]),
                                               c.referenceTemperature, 0.0)));
  }
  return MixtureThermodynamics(registryOf(c), std::move(species));
}

FormationEnthalpies formationOf(const CaseScalars& c) {
  return FormationEnthalpies(registryOf(c), c.formationEnthalpy);
}

ReactionMechanism mechanismOf(const CaseScalars& c) {
  std::vector<Reaction> reactions;
  for (std::size_t r = 0; r < c.reactant.size(); ++r) {
    std::vector<cfd::species::ReactionTerm> reactants;
    std::vector<cfd::species::ReactionTerm> products;
    for (std::size_t i = 0; i < c.names.size(); ++i) {
      if (c.reactant[r][i] != 0.0) {
        reactants.push_back({c.names[i], c.reactant[r][i]});
      }
      if (c.product[r][i] != 0.0) {
        products.push_back({c.names[i], c.product[r][i]});
      }
    }
    reactions.push_back(Reaction(
        "R" + std::to_string(r), registryOf(c), reactants, products,
        c.reversible[r] ? ReactionDirection::Reversible : ReactionDirection::Irreversible));
  }
  return ReactionMechanism(registryOf(c), std::move(reactions), mixtureOf(c));
}

ReactionKinetics kineticsOf(const CaseScalars& c, const ReactionMechanism& mechanism) {
  std::vector<ReactionRateParameters> parameters;
  for (std::size_t r = 0; r < c.reactant.size(); ++r) {
    std::optional<ArrheniusParameters> reverse;
    if (c.reversible[r]) {
      reverse =
          ArrheniusParameters(c.reversePreExponential[r], c.exponent[r], c.activationEnergy[r]);
    }
    parameters.push_back(
        {"R" + std::to_string(r),
         ArrheniusParameters(c.preExponential[r], c.exponent[r], c.activationEnergy[r]), reverse});
  }
  return ReactionKinetics(mechanism, std::move(parameters));
}

// THE DEVICE POD, built from the SAME scalars. R_i is computed here on the host exactly as
// SpeciesThermodynamics computes it -- R_u / M_i -- so it cannot drift.
DeviceMechanism deviceOf(const CaseScalars& c) {
  DeviceMechanism out{};
  out.speciesCount = static_cast<Index>(c.names.size());
  out.reactionCount = static_cast<Index>(c.reactant.size());
  for (std::size_t i = 0; i < c.names.size(); ++i) {
    DeviceSpecies& s = out.species[i];
    s.molecularWeight = c.molecularWeight[i];
    s.gasConstant = kUniversalGasConstant / c.molecularWeight[i];
    s.specificHeat = c.specificHeat[i];
    s.formationEnthalpy = c.formationEnthalpy[i];
  }
  for (std::size_t r = 0; r < c.reactant.size(); ++r) {
    DeviceReaction& reaction = out.reactions[r];
    for (std::size_t i = 0; i < c.names.size(); ++i) {
      reaction.reactant[i] = c.reactant[r][i];
      reaction.product[i] = c.product[r][i];
      reaction.net[i] = c.product[r][i] - c.reactant[r][i];
    }
    reaction.reversible = c.reversible[r];
    out.forward[r] = DeviceArrhenius{c.preExponential[r], c.exponent[r], c.activationEnergy[r]};
    if (c.reversible[r]) {
      out.reverse[r] =
          DeviceArrhenius{c.reversePreExponential[r], c.exponent[r], c.activationEnergy[r]};
    }
  }
  return out;
}

// P18.7's validated A -> B case, with the dyadic weights that make the mass balance exact.
CaseScalars abCase() {
  CaseScalars c;
  c.names = {"A", "B"};
  c.molecularWeight = {0.0625, 0.0625};
  c.specificHeat = {1000.0, 1000.0};
  c.formationEnthalpy = {0.0, -2000000.0};
  c.reactant = {{1.0, 0.0}};
  c.product = {{0.0, 1.0}};
  c.reversible = {false};
  c.preExponential = {5.0};
  c.exponent = {0.0};
  c.activationEnergy = {3.0e4};
  c.reversePreExponential = {0.0};
  return c;
}

// A three-species, two-reaction case with DELIBERATELY DISTINCT molecular weights, specific heats,
// formation enthalpies and kinetics -- section 34's requirement, so a species permutation that
// preserved total mass but mapped chemistry to the wrong species cannot pass.
CaseScalars chainCase() {
  CaseScalars c;
  c.names = {"A", "B", "C"};
  c.molecularWeight = {0.0625, 0.125, 0.1875};
  c.specificHeat = {900.0, 1300.0, 1700.0};
  c.formationEnthalpy = {0.0, -1500000.0, 2500000.0};
  //  A + B -> C   (mass balances exactly: 0.0625 + 0.125 = 0.1875)
  c.reactant.push_back({1.0, 1.0, 0.0});
  c.product.push_back({0.0, 0.0, 1.0});
  c.reversible.push_back(false);
  c.preExponential.push_back(3.0);
  c.exponent.push_back(0.5);
  c.activationEnergy.push_back(4.0e4);
  c.reversePreExponential.push_back(0.0);
  //  C <-> A + B   the reverse direction, declared reversible
  c.reactant.push_back({0.0, 0.0, 1.0});
  c.product.push_back({1.0, 1.0, 0.0});
  c.reversible.push_back(true);
  c.preExponential.push_back(1.5);
  c.exponent.push_back(-0.25);
  c.activationEnergy.push_back(6.0e4);
  c.reversePreExponential.push_back(0.75);
  return c;
}

const DeviceSpeciesControls kProduction{};  // all false

// A composition matrix: single-species, binary, ternary, near-pure, and spatially varying.
std::vector<std::vector<Real>> compositionMatrix(Index speciesCount) {
  std::vector<std::vector<Real>> out;
  if (speciesCount == 2) {
    out.push_back({1.0, 0.0});
    out.push_back({0.0, 1.0});
    out.push_back({0.5, 0.5});
    out.push_back({0.25, 0.75});
    out.push_back({1.0 - 1.0e-12, 1.0e-12});  // near-pure
    out.push_back({1.0e-12, 1.0 - 1.0e-12});
    out.push_back({0.3, 0.7});
  } else {
    out.push_back({1.0, 0.0, 0.0});
    out.push_back({0.0, 1.0, 0.0});
    out.push_back({0.0, 0.0, 1.0});
    out.push_back({0.5, 0.5, 0.0});
    out.push_back({0.25, 0.25, 0.5});
    out.push_back({1.0 / 3.0, 1.0 / 3.0, 1.0 - (1.0 / 3.0) - (1.0 / 3.0)});
    out.push_back({1.0 - 2.0e-12, 1.0e-12, 1.0e-12});  // near-pure
    out.push_back({0.1, 0.2, 0.7});
  }
  return out;
}

const std::vector<Real>& temperatureMatrix() {
  static const std::vector<Real> t{300.0, 800.0, 1000.0, 1500.0, 2500.0};
  return t;
}

}  // namespace

// ===================================================================
// E0 -- the shared constants cannot drift
// ===================================================================

TEST(P188RestatementTest, TheUniversalGasConstantIsTheSameLiteral) {
  // The device header restates R_u rather than including the species headers, so the two could
  // drift. This asserts they have not -- bitwise.
  EXPECT_EQ(kDeviceUniversalGasConstant, kUniversalGasConstant);
}

TEST(P188RestatementTest, TheTestMechanismsFitTheDeviceCapacity) {
  // The device PODs are flat and fixed-size. If a mechanism did not fit, the comparison would be
  // of a truncated mechanism -- which would still "pass". Asserted rather than assumed.
  EXPECT_LE(abCase().names.size(), static_cast<std::size_t>(cfd::gpu::kMaxDeviceSpecies));
  EXPECT_LE(chainCase().names.size(), static_cast<std::size_t>(cfd::gpu::kMaxDeviceSpecies));
  EXPECT_LE(chainCase().reactant.size(), static_cast<std::size_t>(cfd::gpu::kMaxDeviceReactions));
}

// ===================================================================
// E1 -- mixture properties (§9)
// ===================================================================

TEST(P188MixturePropertyTest, EveryMixturePropertyIsBitwise) {
  int bitwiseRows = 0;
  int totalRows = 0;
  Real worstAbsolute = 0.0;

  for (const CaseScalars& c : {abCase(), chainCase()}) {
    const auto mixture = mixtureOf(c);
    const auto device = deviceOf(c);
    for (const auto& composition : compositionMatrix(device.speciesCount)) {
      for (const Real t : temperatureMatrix()) {
        ++totalRows;
        bool rowBitwise = true;

        const Real cpuM = mixture.molecularWeight(composition);
        const Real cpuR = mixture.gasConstant(composition);
        const Real cpuCp = mixture.specificHeatPressure(composition, t);
        const auto cpuProperties = mixture.properties(composition, t);
        const Real cpuCv = cpuProperties.specificHeatVolume();
        const Real cpuGamma = cpuProperties.specificHeatRatio();

        DeviceSpeciesStatus status = DeviceSpeciesStatus::Ok;
        const Real gpuM =
            cfd::gpu::deviceMolecularWeight(device, composition.data(), kProduction, &status);
        const Real gpuR = cfd::gpu::deviceGasConstant(device, composition.data(), kProduction);
        const Real gpuCp =
            cfd::gpu::deviceSpecificHeatPressure(device, composition.data(), kProduction);
        const Real gpuCv = cfd::gpu::deviceSpecificHeatVolume(gpuCp, gpuR);
        const Real gpuGamma = cfd::gpu::deviceSpecificHeatRatio(gpuCp, gpuCv);

        EXPECT_EQ(gpuM, cpuM) << "M_mix, T = " << t;
        EXPECT_EQ(gpuR, cpuR) << "R_mix, T = " << t;
        EXPECT_EQ(gpuCp, cpuCp) << "cp_mix, T = " << t;
        EXPECT_EQ(gpuCv, cpuCv) << "cv_mix, T = " << t;
        EXPECT_EQ(gpuGamma, cpuGamma) << "gamma_mix, T = " << t;
        rowBitwise = (gpuM == cpuM) && (gpuR == cpuR) && (gpuCp == cpuCp) && (gpuCv == cpuCv) &&
                     (gpuGamma == cpuGamma);
        worstAbsolute = std::max(worstAbsolute, std::abs(gpuM - cpuM));
        worstAbsolute = std::max(worstAbsolute, std::abs(gpuR - cpuR));
        if (rowBitwise) {
          ++bitwiseRows;
        }
      }
    }
  }
  EXPECT_EQ(bitwiseRows, totalRows) << bitwiseRows << " of " << totalRows << " rows bitwise";
  EXPECT_EQ(worstAbsolute, 0.0);
  EXPECT_GT(totalRows, 50) << "the matrix must actually be a matrix";
}

TEST(P188MixturePropertyTest, N1_N2_PermutingSpeciesOrCollapsingWeightsIsDetected) {
  // CONTROLS N1 and N2, as FLAGS on the device path rather than source edits.
  const CaseScalars c = chainCase();  // distinct weights, so a permutation is observable
  const auto mixture = mixtureOf(c);
  const auto device = deviceOf(c);
  const std::vector<Real> composition{0.1, 0.2, 0.7};

  DeviceSpeciesStatus status = DeviceSpeciesStatus::Ok;
  const Real correct =
      cfd::gpu::deviceMolecularWeight(device, composition.data(), kProduction, &status);
  EXPECT_EQ(correct, mixture.molecularWeight(composition));

  DeviceSpeciesControls permuted;
  permuted.permuteSpeciesIndex = true;
  const Real withPermutation =
      cfd::gpu::deviceMolecularWeight(device, composition.data(), permuted, &status);
  EXPECT_NE(withPermutation, correct) << "a species permutation must change M_mix";

  DeviceSpeciesControls collapsed;
  collapsed.oneWeightForAllSpecies = true;
  const Real withOneWeight =
      cfd::gpu::deviceMolecularWeight(device, composition.data(), collapsed, &status);
  EXPECT_NE(withOneWeight, correct) << "one weight for all species must change M_mix";

  // and the margins are large, not round-off
  EXPECT_GT(std::abs(withPermutation - correct) / correct, 0.1);
  EXPECT_GT(std::abs(withOneWeight - correct) / correct, 0.1);
}

// ===================================================================
// E2 -- EOS (§10)
// ===================================================================

TEST(P188EquationOfStateTest, DensityPressureAndTemperatureAreBitwise) {
  for (const CaseScalars& c : {abCase(), chainCase()}) {
    const auto mixture = mixtureOf(c);
    const auto device = deviceOf(c);
    for (const auto& composition : compositionMatrix(device.speciesCount)) {
      for (const Real t : temperatureMatrix()) {
        for (const Real p : {50000.0, 101325.0, 1000000.0}) {
          const Real gpuR = cfd::gpu::deviceGasConstant(device, composition.data(), kProduction);
          const Real cpuRho = mixture.density(composition, p, t);
          const Real gpuRho = cfd::gpu::deviceDensityFromPressure(p, gpuR, t);
          EXPECT_EQ(gpuRho, cpuRho) << "rho at p = " << p << " T = " << t;

          // the reverse directions, through the SAME per-cell R, reached via the EOS the mixture
          // builds -- so the comparison is against production, not against a restatement.
          const auto properties = mixture.properties(composition, t);
          const auto& eos = properties.equationOfState();
          EXPECT_EQ(cfd::gpu::devicePressure(cpuRho, gpuR, t), eos.pressure(cpuRho, t));
          EXPECT_EQ(cfd::gpu::deviceTemperatureFromPressure(p, cpuRho, gpuR),
                    eos.temperature(p, cpuRho));
        }
      }
    }
  }
}

// ===================================================================
// E3 -- Arrhenius, value AND branch (§11)
// ===================================================================

TEST(P188ArrheniusTest, RateCoefficientAndBranchAreBitwise) {
  struct Parameters {
    Real a;
    Real n;
    Real ea;
    const char* what;
  };
  const std::vector<Parameters> set{
      {5.0, 0.0, 3.0e4, "the production case"},
      {0.0, 0.5, 3.0e4, "A = 0, the exact branch"},
      {1.5e8, 0.5, 0.0, "Ea = 0"},
      {1.5e8, 0.0, 1.25e5, "n = 0"},
      {1.5e8, -3.5, 1.25e5, "negative n"},
      {1.5e8, 0.5, 5.0e5, "high Ea, so a deep underflow at low T"},
      {1.0e300, 40.0, 3.0e6, "A T^n overflows -- the log-space FALLBACK"},
  };

  int bitwise = 0;
  int total = 0;
  for (const Parameters& p : set) {
    const ArrheniusParameters cpu(p.a, p.n, p.ea);
    const DeviceArrhenius gpu{p.a, p.n, p.ea};
    for (const Real t : {300.0, 800.0, 1000.0, 1500.0, 2500.0, 3000.0}) {
      ++total;
      int branch = -1;
      DeviceSpeciesStatus status = DeviceSpeciesStatus::Ok;
      const Real gpuK = cfd::gpu::deviceRateCoefficient(gpu, t, kProduction, &status, &branch);
      const Real gpuX = cfd::gpu::deviceExponentialArgument(gpu, t, kProduction);

      Real cpuK = 0.0;
      bool cpuThrew = false;
      try {
        cpuK = cpu.rateCoefficient(t);
      } catch (const NumericalError&) {
        cpuThrew = true;
      }
      EXPECT_EQ(gpuX, cpu.exponentialArgument(t)) << p.what << " at T = " << t;

      if (cpuThrew) {
        // the CPU refused; the device must have flagged the same thing
        EXPECT_EQ(status, DeviceSpeciesStatus::NonFiniteRate)
            << p.what << " at T = " << t << ": CPU threw, device status must say so";
      } else {
        EXPECT_EQ(gpuK, cpuK) << p.what << " at T = " << t;
        if (gpuK == cpuK) {
          ++bitwise;
        }
        // THE BRANCH IS COMPARED, not only the value -- G9 requires the control-flow decisions.
        const int expectedBranch = (p.a == 0.0) ? 0 : std::isfinite(p.a * std::pow(t, p.n)) ? 1 : 2;
        EXPECT_EQ(branch, expectedBranch) << p.what << " at T = " << t;
      }
    }
  }
  EXPECT_GT(bitwise, 30) << bitwise << " of " << total << " rows bitwise";
  // the FALLBACK branch must actually have been exercised, or the branch comparison is vacuous
  int fallbackSeen = 0;
  for (const Real t : {1000.0, 1500.0}) {
    int branch = -1;
    DeviceSpeciesStatus status = DeviceSpeciesStatus::Ok;
    const DeviceArrhenius overflowing{1.0e300, 40.0, 3.0e6};
    static_cast<void>(
        cfd::gpu::deviceRateCoefficient(overflowing, t, kProduction, &status, &branch));
    if (branch == 2) {
      ++fallbackSeen;
    }
  }
  EXPECT_GT(fallbackSeen, 0) << "the log-space fallback branch was never taken";
}

TEST(P188ArrheniusTest, N4_FlippingTheExponentSignIsDetected) {
  const DeviceArrhenius gpu{5.0, 0.0, 3.0e4};
  DeviceSpeciesControls flipped;
  flipped.flipArrheniusExponentSign = true;
  DeviceSpeciesStatus status = DeviceSpeciesStatus::Ok;
  int branch = -1;
  const Real correct = cfd::gpu::deviceRateCoefficient(gpu, 1000.0, kProduction, &status, &branch);
  const Real wrong = cfd::gpu::deviceRateCoefficient(gpu, 1000.0, flipped, &status, &branch);
  EXPECT_NE(wrong, correct);
  EXPECT_GT(wrong / correct, 1.0e3) << "flipping the sign of x must change k by orders";
}

// ===================================================================
// E4, E5 -- progress rates and sources (§12, §13, §14)
// ===================================================================

TEST(P188ReactionSourceTest, ConcentrationsProgressAndOmegaAreBitwise) {
  for (const CaseScalars& c : {abCase(), chainCase()}) {
    const auto mixture = mixtureOf(c);
    const auto mechanism = mechanismOf(c);
    const auto kinetics = kineticsOf(c, mechanism);
    const auto device = deviceOf(c);
    const Index species = device.speciesCount;
    const Index reactions = device.reactionCount;

    for (const auto& composition : compositionMatrix(species)) {
      for (const Real t : {800.0, 1000.0, 1500.0}) {
        for (const Real rho : {0.5, 1.0, 7.25}) {
          const Index cells = 1;
          const MassFractionState state(registryOf(c), cells, composition);
          const ScalarField density(cells, rho);
          const ScalarField temperature(cells, t);

          // --- concentrations
          const auto cpuConcentrations = molarConcentrations(mixture, state, density);
          std::vector<Real> gpuConcentrations(static_cast<std::size_t>(species));
          for (Index i = 0; i < species; ++i) {
            gpuConcentrations[i] = cfd::gpu::deviceConcentration(device, i, rho, composition[i]);
            EXPECT_EQ(gpuConcentrations[i], cpuConcentrations[i][0])
                << "C_" << i << " at T = " << t << " rho = " << rho;
          }

          // --- progress rates, forward / reverse / net
          const auto cpuRates =
              arrheniusProgressRates(mechanism, kinetics, mixture, state, density, temperature);
          std::vector<Real> gpuRates(static_cast<std::size_t>(reactions));
          DeviceSpeciesStatus status = DeviceSpeciesStatus::Ok;
          for (Index r = 0; r < reactions; ++r) {
            gpuRates[r] = cfd::gpu::deviceProgressRate(device, r, gpuConcentrations.data(), t,
                                                       kProduction, &status);
            EXPECT_EQ(gpuRates[r], cpuRates.perReaction[r][0])
                << "q_" << r << " at T = " << t << " rho = " << rho;
          }

          // --- omega_i
          const auto cpuSources = reactionSpeciesSources(mechanism, mixture, cpuRates, cells);
          std::vector<Real> gpuOmega(static_cast<std::size_t>(species));
          cfd::gpu::deviceSpeciesSources(device, gpuRates.data(), gpuOmega.data(), kProduction);
          for (Index i = 0; i < species; ++i) {
            EXPECT_EQ(gpuOmega[i], cpuSources.perSpecies[i].Su[0])
                << "omega_" << i << " at T = " << t;
          }

          // --- SECTION 14: mass conservation must hold INDEPENDENTLY on each path, not merely
          // agree. "A GPU result that matches CPU but violates mass conservation is not
          // acceptable."
          Real gpuSum = 0.0;
          Real cpuSum = 0.0;
          Real scale = 0.0;
          for (Index i = 0; i < species; ++i) {
            gpuSum += gpuOmega[i];
            cpuSum += cpuSources.perSpecies[i].Su[0];
            scale = std::max(scale, std::abs(gpuOmega[i]));
          }
          const Real bound = 32.0 * std::numeric_limits<Real>::epsilon() * scale;
          EXPECT_LE(std::abs(gpuSum), bound) << "GPU sum omega = " << gpuSum;
          EXPECT_LE(std::abs(cpuSum), bound) << "CPU sum omega = " << cpuSum;
        }
      }
    }
  }
}

TEST(P188ReactionSourceTest, N5_N8_OmittingTheWeightOrAReactionIsDetected) {
  const CaseScalars c = chainCase();
  const auto device = deviceOf(c);
  const std::vector<Real> rates{2.0, 0.5};
  std::vector<Real> correct(3);
  std::vector<Real> noWeight(3);
  std::vector<Real> oneLess(3);

  cfd::gpu::deviceSpeciesSources(device, rates.data(), correct.data(), kProduction);
  DeviceSpeciesControls omitWeight;
  omitWeight.omitMolecularWeightInOmega = true;
  cfd::gpu::deviceSpeciesSources(device, rates.data(), noWeight.data(), omitWeight);
  DeviceSpeciesControls omitReaction;
  omitReaction.omitLastReaction = true;
  cfd::gpu::deviceSpeciesSources(device, rates.data(), oneLess.data(), omitReaction);

  bool weightDiffers = false;
  bool reactionDiffers = false;
  for (Index i = 0; i < 3; ++i) {
    weightDiffers = weightDiffers || (noWeight[i] != correct[i]);
    reactionDiffers = reactionDiffers || (oneLess[i] != correct[i]);
  }
  EXPECT_TRUE(weightDiffers) << "omitting M_i must change omega";
  EXPECT_TRUE(reactionDiffers) << "omitting a reaction must change omega";

  // AND OMITTING THE WEIGHT BREAKS CONSERVATION, which is the sharper detection: the weights are
  // what make sum_i M_i nu_i vanish.
  Real sumNoWeight = 0.0;
  Real sumCorrect = 0.0;
  for (Index i = 0; i < 3; ++i) {
    sumNoWeight += noWeight[i];
    sumCorrect += correct[i];
  }
  EXPECT_LE(std::abs(sumCorrect), 1.0e-12);
  EXPECT_GT(std::abs(sumNoWeight), 1.0) << "sum omega without M_i = " << sumNoWeight;
}

// ===================================================================
// E6 -- heat release (§17)
// ===================================================================

TEST(P188HeatReleaseTest, ReactionEnthalpyAndSourceAreBitwiseWithSignEquivalence) {
  for (const CaseScalars& c : {abCase(), chainCase()}) {
    const auto mixture = mixtureOf(c);
    const auto mechanism = mechanismOf(c);
    const auto formation = formationOf(c);
    const auto device = deviceOf(c);
    const Index reactions = device.reactionCount;

    for (Index r = 0; r < reactions; ++r) {
      const Real cpuDeltaH = standardReactionEnthalpy(mechanism.reaction(r), mixture, formation);
      const Real gpuDeltaH = cfd::gpu::deviceStandardReactionEnthalpy(device, r);
      EXPECT_EQ(gpuDeltaH, cpuDeltaH) << "Delta H_r for reaction " << r;
      // SIGN EQUIVALENCE IS A HARD PASS/FAIL (section 17)
      EXPECT_EQ(cpuDeltaH < 0.0, gpuDeltaH < 0.0) << "the sign of Delta H_r must agree";
    }

    for (const Real q : {0.0, 1.0, 2.0, 7.5, -1.5}) {
      ReactionProgressRates cpuRates;
      std::vector<Real> gpuRates(static_cast<std::size_t>(reactions));
      for (Index r = 0; r < reactions; ++r) {
        cpuRates.perReaction.push_back(ScalarField(1, q));
        gpuRates[r] = q;
      }
      const Real cpuSource = reactionHeatRelease(mechanism, mixture, formation, cpuRates, 1)[0];
      DeviceSpeciesStatus status = DeviceSpeciesStatus::Ok;
      const Real gpuSource =
          cfd::gpu::deviceHeatRelease(device, gpuRates.data(), kProduction, &status);
      EXPECT_EQ(gpuSource, cpuSource) << "S_E at q = " << q;
      EXPECT_EQ(cpuSource > 0.0, gpuSource > 0.0) << "the sign of S_E must agree";
    }
  }
}

TEST(P188HeatReleaseTest, N6_FlippingTheHeatReleaseSignIsDetected) {
  const CaseScalars c = abCase();
  const auto device = deviceOf(c);
  const std::vector<Real> rates{2.0};
  DeviceSpeciesStatus status = DeviceSpeciesStatus::Ok;
  const Real correct = cfd::gpu::deviceHeatRelease(device, rates.data(), kProduction, &status);
  DeviceSpeciesControls flipped;
  flipped.flipHeatReleaseSign = true;
  const Real wrong = cfd::gpu::deviceHeatRelease(device, rates.data(), flipped, &status);
  EXPECT_EQ(wrong, -correct);
  EXPECT_LT(wrong * correct, 0.0) << "the sign must actually invert";
  EXPECT_GT(correct, 0.0) << "and the production case must be exothermic, or this proves nothing";
}

// ===================================================================
// E7 -- energy and temperature recovery (§18)
// ===================================================================

TEST(P188EnergyTest, EnergyPoolsAndTemperatureRecoveryAreBitwise) {
  for (const CaseScalars& c : {abCase(), chainCase()}) {
    const auto mixture = mixtureOf(c);
    const auto formation = formationOf(c);
    const auto device = deviceOf(c);
    for (const auto& composition : compositionMatrix(device.speciesCount)) {
      for (const Real t : {300.0, 1000.0, 2500.0}) {
        for (const Real rho : {0.5, 1.0, 7.25}) {
          const auto cpu = energyDensity(mixture, formation, composition, rho, t);
          const Real gpuSensible = cfd::gpu::deviceSensibleEnergyDensity(device, composition.data(),
                                                                         rho, t, kProduction);
          const Real gpuChemical =
              cfd::gpu::deviceChemicalEnergyDensity(device, composition.data(), rho);
          EXPECT_EQ(gpuSensible, cpu.sensible) << "sensible at T = " << t;
          EXPECT_EQ(gpuChemical, cpu.chemical) << "chemical at T = " << t;
          EXPECT_EQ(gpuSensible + gpuChemical, cpu.total);

          // temperature recovery, against the CPU's own bisection
          const Real cpuBack =
              temperatureFromSensibleEnergy(mixture, composition, rho, cpu.sensible);
          DeviceSpeciesStatus status = DeviceSpeciesStatus::Ok;
          const Real gpuBack = cfd::gpu::deviceTemperatureFromSensibleEnergy(
              device, composition.data(), rho, cpu.sensible, 1.0e-6, 1.0e5, kProduction, &status);
          EXPECT_EQ(status, DeviceSpeciesStatus::Ok);
          EXPECT_EQ(gpuBack, cpuBack) << "T recovery at T = " << t << " rho = " << rho;
        }
      }
    }
  }
}

// ===================================================================
// E8, E9 -- the coupled step and the batch-reactor trajectory (§21, §22, §23)
// ===================================================================

TEST(P188CoupledStepTest, EveryIntermediateOfOneStepIsBitwise) {
  // SECTION 21's major gate: one step through mixture properties, kinetics, sources, species
  // update, heat release, energy update and temperature recovery, with EVERY intermediate
  // compared -- not only the final state.
  const CaseScalars c = abCase();
  const auto mixture = mixtureOf(c);
  const auto mechanism = mechanismOf(c);
  const auto kinetics = kineticsOf(c, mechanism);
  const auto formation = formationOf(c);
  const auto device = deviceOf(c);
  const Index cells = 1;
  const Real rho = 1.0;
  const Real t0 = 1000.0;
  const Real dt = 1.0e-3;
  const std::vector<Real> composition{1.0, 0.0};

  // --- CPU, through production
  cfd::species::ReactorState state{MassFractionState(registryOf(c), cells, composition),
                                   ScalarField(cells, t0), ScalarField(cells, rho)};
  const auto cpuRates = arrheniusProgressRates(mechanism, kinetics, mixture, state.composition,
                                               state.density, state.temperature);
  const auto cpuSources = reactionSpeciesSources(mechanism, mixture, cpuRates, cells);
  const Real cpuHeat = reactionHeatRelease(mechanism, mixture, formation, cpuRates, cells)[0];
  const auto cpuBefore = energyDensity(mixture, formation, composition, rho, t0);
  const auto cpuNext =
      cfd::species::advanceClosedReactor(mechanism, mixture, formation, cpuRates, state, dt);

  // --- the device path, same inputs
  DeviceReactorStep step;
  cfd::gpu::deviceAdvanceReactorCell(device, composition.data(), rho, t0, dt, 1.0e-6, 1.0e5,
                                     kProduction, false, false, 0.0, &step);

  EXPECT_EQ(step.status, DeviceSpeciesStatus::Ok);
  // mixture properties
  EXPECT_EQ(step.molecularWeight, mixture.molecularWeight(composition));
  EXPECT_EQ(step.gasConstant, mixture.gasConstant(composition));
  EXPECT_EQ(step.specificHeatP, mixture.specificHeatPressure(composition, t0));
  // kinetics and sources
  EXPECT_EQ(step.progressRate[0], cpuRates.perReaction[0][0]);
  EXPECT_EQ(step.omega[0], cpuSources.perSpecies[0].Su[0]);
  EXPECT_EQ(step.omega[1], cpuSources.perSpecies[1].Su[0]);
  // heat release, BOTH routes
  EXPECT_EQ(step.heatRelease, cpuHeat);
  EXPECT_LE(std::abs(step.heatReleaseFromSources - step.heatRelease) / std::abs(step.heatRelease),
            8.0 * std::numeric_limits<Real>::epsilon());
  // energy pools
  EXPECT_EQ(step.sensibleBefore, cpuBefore.sensible);
  EXPECT_EQ(step.chemicalBefore, cpuBefore.chemical);
  // the updated state
  EXPECT_EQ(step.compositionAfter[0], cpuNext.composition.massFraction(0, 0));
  EXPECT_EQ(step.compositionAfter[1], cpuNext.composition.massFraction(1, 0));
  EXPECT_EQ(step.temperatureAfter, cpuNext.temperature[0]);
  // and the step did something
  EXPECT_NE(step.temperatureAfter, t0);
  EXPECT_LT(step.compositionAfter[0], 1.0);
}

TEST(P188CoupledStepTest, TheBatchReactorTrajectoryIsBitwiseAndDoesNotDiverge) {
  // SECTIONS 22, 23. P18.7's validated batch reactor, marched on both paths with IDENTICAL
  // initial state, dt and step count, comparing EVERY step -- section 22 forbids judging only
  // step 1.
  const CaseScalars c = abCase();
  const auto mixture = mixtureOf(c);
  const auto mechanism = mechanismOf(c);
  const auto kinetics = kineticsOf(c, mechanism);
  const auto formation = formationOf(c);
  const auto device = deviceOf(c);
  const Index cells = 1;
  const Real rho = 1.0;
  const Real dt = 1.0e-3;
  const int steps = 500;

  cfd::species::ReactorState state{MassFractionState(registryOf(c), cells, {1.0, 0.0}),
                                   ScalarField(cells, 1000.0), ScalarField(cells, rho)};
  std::vector<Real> gpuComposition{1.0, 0.0};
  Real gpuTemperature = 1000.0;

  int bitwiseSteps = 0;
  Real worstTemperature = 0.0;
  Real worstComposition = 0.0;
  for (int step = 0; step < steps; ++step) {
    const auto rates = arrheniusProgressRates(mechanism, kinetics, mixture, state.composition,
                                              state.density, state.temperature);
    state = cfd::species::advanceClosedReactor(mechanism, mixture, formation, rates, state, dt);

    DeviceReactorStep deviceStep;
    cfd::gpu::deviceAdvanceReactorCell(device, gpuComposition.data(), rho, gpuTemperature, dt,
                                       1.0e-6, 1.0e5, kProduction, false, false, 0.0, &deviceStep);
    ASSERT_EQ(deviceStep.status, DeviceSpeciesStatus::Ok) << "step " << step;
    gpuComposition[0] = deviceStep.compositionAfter[0];
    gpuComposition[1] = deviceStep.compositionAfter[1];
    gpuTemperature = deviceStep.temperatureAfter;

    const bool stepBitwise = (gpuComposition[0] == state.composition.massFraction(0, 0)) &&
                             (gpuComposition[1] == state.composition.massFraction(1, 0)) &&
                             (gpuTemperature == state.temperature[0]);
    if (stepBitwise) {
      ++bitwiseSteps;
    }
    worstTemperature = std::max(worstTemperature, std::abs(gpuTemperature - state.temperature[0]));
    worstComposition = std::max(worstComposition,
                                std::abs(gpuComposition[0] - state.composition.massFraction(0, 0)));
  }

  EXPECT_EQ(bitwiseSteps, steps) << bitwiseSteps << " of " << steps << " steps bitwise";
  // THE DIVERGENCE MUST NOT GROW -- measured, not assumed (section 22)
  EXPECT_EQ(worstTemperature, 0.0);
  EXPECT_EQ(worstComposition, 0.0);
  // and the trajectory actually went somewhere
  EXPECT_LT(state.composition.massFraction(0, 0), 0.95);
  EXPECT_GT(state.temperature[0], 1000.0 + 100.0);
}

TEST(P188CoupledStepTest, N7_N9_StaleTemperatureOrCompositionIsDetected) {
  // CONTROLS N7 and N9, as flags.
  const CaseScalars c = chainCase();  // unequal cp, so a stale composition is observable
  const auto device = deviceOf(c);
  const std::vector<Real> composition{0.4, 0.4, 0.2};
  const Real rho = 1.0;
  const Real t0 = 1200.0;
  const Real dt = 1.0e-4;

  DeviceReactorStep correct;
  cfd::gpu::deviceAdvanceReactorCell(device, composition.data(), rho, t0, dt, 1.0e-6, 1.0e5,
                                     kProduction, false, false, 0.0, &correct);
  ASSERT_EQ(correct.status, DeviceSpeciesStatus::Ok);

  DeviceReactorStep staleT;
  cfd::gpu::deviceAdvanceReactorCell(device, composition.data(), rho, t0, dt, 1.0e-6, 1.0e5,
                                     kProduction, true, false, 900.0, &staleT);
  EXPECT_NE(staleT.progressRate[0], correct.progressRate[0])
      << "a stale temperature must change the rate";

  DeviceReactorStep staleY;
  cfd::gpu::deviceAdvanceReactorCell(device, composition.data(), rho, t0, dt, 1.0e-6, 1.0e5,
                                     kProduction, false, true, 0.0, &staleY);
  EXPECT_NE(staleY.temperatureAfter, correct.temperatureAfter)
      << "a stale composition must change the recovered T";
  // and cp_mix really did move, or N9 would be vacuous
  const Real cpBefore =
      cfd::gpu::deviceSpecificHeatPressure(device, composition.data(), kProduction);
  const Real cpAfter =
      cfd::gpu::deviceSpecificHeatPressure(device, correct.compositionAfter, kProduction);
  EXPECT_NE(cpBefore, cpAfter) << "without this, N9 would be vacuous";
}

// ===================================================================
// E11 -- invalid-state parity (§20, §48)
// ===================================================================

TEST(P188InvalidStateTest, TheDeviceRefusesWhatTheCpuRefusesAndSaysWhy) {
  // Section 48: compare the DECISION and its CLASSIFICATION, not merely that something failed.
  // "If CPU rejects a state, GPU must not silently accept it."
  const CaseScalars c = abCase();
  const auto mixture = mixtureOf(c);
  const auto device = deviceOf(c);
  const std::vector<Real> good{1.0, 0.0};
  const Real nan = std::numeric_limits<Real>::quiet_NaN();

  struct Row {
    Real temperature;
    Real density;
    DeviceSpeciesStatus expected;
    const char* what;
  };
  const std::vector<Row> rows{
      {-5.0, 1.0, DeviceSpeciesStatus::NonPositiveTemperature, "T < 0"},
      {0.0, 1.0, DeviceSpeciesStatus::NonPositiveTemperature, "T = 0"},
      {nan, 1.0, DeviceSpeciesStatus::NonFiniteTemperature, "T = NaN"},
      {1000.0, -1.0, DeviceSpeciesStatus::NonPositiveDensity, "rho < 0"},
      {1000.0, 0.0, DeviceSpeciesStatus::NonPositiveDensity, "rho = 0"},
      {1000.0, nan, DeviceSpeciesStatus::NonPositiveDensity, "rho = NaN"},
  };

  for (const Row& row : rows) {
    DeviceReactorStep step;
    cfd::gpu::deviceAdvanceReactorCell(device, good.data(), row.density, row.temperature, 1.0e-3,
                                       1.0e-6, 1.0e5, kProduction, false, false, 0.0, &step);
    EXPECT_EQ(step.status, row.expected) << row.what;

    // and the CPU refuses the same state
    const Index cells = 1;
    const MassFractionState state(registryOf(c), cells, good);
    bool cpuRefused = false;
    try {
      static_cast<void>(energyDensity(mixture, formationOf(c), good, row.density, row.temperature));
    } catch (const InvalidArgumentError&) {
      cpuRefused = true;
    }
    EXPECT_TRUE(cpuRefused) << row.what << ": the CPU must refuse it too";
  }

  // THE INVALID COMPOSITIONS ARE REFUSED BY P18.1 ON THE HOST, which is the documented split
  // (audit.md section 6): a device cannot throw, so composition validity is a host invariant.
  EXPECT_THROW(MassFractionState(registryOf(c), 1, {0.6, 0.6}), InvalidArgumentError);
  EXPECT_THROW(MassFractionState(registryOf(c), 1, {-0.1, 1.1}), InvalidArgumentError);
  EXPECT_THROW(MassFractionState(registryOf(c), 1, {nan, 0.0}), InvalidArgumentError);
}

TEST(P188InvalidStateTest, N11_SilentlyAcceptingAnInvalidStateIsDetected) {
  // CONTROL N11. The flag makes the device continue past a state it should refuse; the detection
  // is that the production path does NOT.
  const CaseScalars c = abCase();
  const auto device = deviceOf(c);
  const std::vector<Real> good{1.0, 0.0};

  DeviceReactorStep refused;
  cfd::gpu::deviceAdvanceReactorCell(device, good.data(), 1.0, -5.0, 1.0e-3, 1.0e-6, 1.0e5,
                                     kProduction, false, false, 0.0, &refused);
  EXPECT_EQ(refused.status, DeviceSpeciesStatus::NonPositiveTemperature);
  EXPECT_EQ(refused.temperatureAfter, 0.0) << "the production path stops and writes nothing";

  DeviceSpeciesControls accept;
  accept.acceptInvalidState = true;
  DeviceReactorStep accepted;
  cfd::gpu::deviceAdvanceReactorCell(device, good.data(), 1.0, -5.0, 1.0e-3, 1.0e-6, 1.0e5, accept,
                                     false, false, 0.0, &accepted);
  // it still RECORDS the status -- the control makes it continue, not lie
  EXPECT_EQ(accepted.status, DeviceSpeciesStatus::NonPositiveTemperature);
  // but it produced a different outcome, which is the detection
  EXPECT_NE(accepted.progressRate[0], refused.progressRate[0]);
}
