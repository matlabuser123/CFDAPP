// P18.3 -- INSTRUMENTATION, not production and not a test.
//
// Section 35 asks for measured values rather than PASS/FAIL. This sweeps a matrix
// of compositions, temperatures and pressures and reports the worst error in each
// identity G5 names, so the summary quotes numbers that came out of the code.
#include <cmath>
#include <cstdio>
#include <limits>
#include <string>
#include <vector>

#include "cfd/compressible/ThermodynamicProperties.hpp"
#include "cfd/species/MixtureThermodynamics.hpp"
#include "cfd/species/SpeciesRegistry.hpp"
#include "cfd/thermal/Enthalpy.hpp"
#include "cfd/thermal/TemperatureDependentProperty.hpp"

using namespace cfd;
using cfd::compressible::ThermodynamicProperties;
using namespace cfd::species;
using cfd::thermal::SensibleEnthalpy;
using cfd::thermal::TemperatureDependentProperty;

namespace {
constexpr Real kEps = std::numeric_limits<Real>::epsilon();

SensibleEnthalpy constantCp(Real cp, const std::string& name) {
  return SensibleEnthalpy(TemperatureDependentProperty::constant(cp, name), 298.15, 0.0);
}

struct Worst {
  Real value{};
  std::string where;
  void offer(Real candidate, const std::string& context) {
    if (candidate > value) {
      value = candidate;
      where = context;
    }
  }
};
}  // namespace

int main() {
  // H2 / CO2 / N2 -- molecular weights spanning 21.8x and cp spanning 17.0x.
  const Real mH2 = 0.002016;
  const Real mCO2 = 0.04401;
  const Real mN2 = 0.0280134;
  std::vector<SpeciesThermodynamics> species;
  species.push_back(SpeciesThermodynamics(mH2, constantCp(14307.0, "cp_H2")));
  species.push_back(SpeciesThermodynamics(mCO2, constantCp(844.0, "cp_CO2")));
  species.push_back(SpeciesThermodynamics(mN2, constantCp(1040.0, "cp_N2")));
  const std::vector<Real> molecularWeights{mH2, mCO2, mN2};
  const MixtureThermodynamics mixture(SpeciesRegistry({"H2", "CO2", "N2"}), std::move(species));

  std::printf("=== P18.3 quantitative closure (section 35) ===\n");
  std::printf("eps = %.17g\n", kEps);
  std::printf("all errors are RELATIVE to the magnitude of the quantity; the G5 allowance is 8 eps"
              " = %.17g\n\n", 8.0 * kEps);

  Worst cpCvR, gammaConsistency, rClosure, mRecovery, eosRho, eosP, eosT, cvCrossCheck;

  const std::vector<std::vector<Real>> compositions{
      {1.0, 0.0, 0.0},        {0.0, 1.0, 0.0},        {0.0, 0.0, 1.0},
      {0.5, 0.5, 0.0},        {0.25, 0.75, 0.0},      {0.2, 0.3, 0.5},
      {1.0 / 3.0, 1.0 / 3.0, 1.0 / 3.0},
      {1.0 - 2.0e-9, 1.0e-9, 1.0e-9},                 {0.1, 0.1, 0.8},
  };
  const std::vector<Real> temperatures{200.0, 300.0, 700.0, 1500.0, 3000.0};
  const std::vector<Real> pressures{1000.0, 101325.0, 5.0e6};

  for (const auto& composition : compositions) {
    for (const Real temperature : temperatures) {
      const auto properties = mixture.properties(composition, temperature);
      const Real cp = properties.specificHeatPressure();
      const Real cv = properties.specificHeatVolume();
      const Real r = properties.gasConstant();
      const Real gamma = properties.specificHeatRatio();
      const std::string context =
          "Y={" + std::to_string(composition[0]) + ",...} T=" + std::to_string(temperature);

      cpCvR.offer(std::abs((cp - cv) - r) / std::abs(r), context);
      gammaConsistency.offer(std::abs(gamma - (cp / cv)) / std::abs(gamma), context);

      // the two R_mix formulations
      const Real rA = mixture.gasConstant(composition);
      const Real rB = mixture.gasConstantFromMolecularWeight(composition);
      rClosure.offer(std::abs(rA - rB) / std::abs(rA), context);

      // cv from the closure vs the species sum -- algebraically identical
      Real cvSum = 0.0;
      for (Index i = 0; i < composition.size(); ++i) {
        cvSum += composition[i] * mixture.species(i).specificHeatVolume(temperature);
      }
      cvCrossCheck.offer(std::abs(cv - cvSum) / std::abs(cv), context);

      for (const Real pressure : pressures) {
        const Real rho = properties.density(pressure, temperature);
        const Real pBack = properties.equationOfState().pressure(rho, temperature);
        const Real tBack = properties.equationOfState().temperature(pressure, rho);
        eosRho.offer(std::abs(rho - (pressure / (r * temperature))) / rho, context);
        eosP.offer(std::abs(pBack - pressure) / pressure, context);
        eosT.offer(std::abs(tBack - temperature) / temperature, context);
      }
    }
  }

  // single-species molecular-weight recovery: measured, not claimed bitwise
  for (Index i = 0; i < 3; ++i) {
    std::vector<Real> composition(3, 0.0);
    composition[i] = 1.0;
    const Real recovered = mixture.molecularWeight(composition);
    mRecovery.offer(std::abs(recovered - molecularWeights[i]) / molecularWeights[i],
                    "species " + std::to_string(i));
  }

  std::printf("max |cp - cv - R| / R                  %.6e   at %s\n", cpCvR.value,
              cpCvR.where.c_str());
  std::printf("max |gamma - cp/cv| / gamma            %.6e   at %s\n", gammaConsistency.value,
              gammaConsistency.where.c_str());
  std::printf("max R_mix closure error (two forms)    %.6e   at %s\n", rClosure.value,
              rClosure.where.c_str());
  std::printf("max cv discrepancy (closure vs sum)    %.6e   at %s\n", cvCrossCheck.value,
              cvCrossCheck.where.c_str());
  std::printf("max single-species M recovery error    %.6e   at %s\n", mRecovery.value,
              mRecovery.where.c_str());
  std::printf("max EOS rho error vs p/(R T)           %.6e\n", eosRho.value);
  std::printf("max EOS p round-trip error             %.6e\n", eosP.value);
  std::printf("max EOS T round-trip error             %.6e\n", eosT.value);
  std::printf("\n");

  // ---- single-species compatibility against a directly built P17 object ----
  std::printf("=== single-species compatibility with P17 (G5's BITWISE clause) ===\n");
  std::vector<SpeciesThermodynamics> one;
  one.push_back(SpeciesThermodynamics(mH2, constantCp(14307.0, "cp_H2")));
  const MixtureThermodynamics pure(SpeciesRegistry({"H2"}), std::move(one));
  const ThermodynamicProperties p17(kUniversalGasConstant / mH2, 14307.0);
  int mismatches = 0;
  for (const Real temperature : temperatures) {
    const auto mixed = pure.properties({1.0}, temperature);
    if (mixed.gasConstant() != p17.gasConstant()) ++mismatches;
    if (mixed.specificHeatPressure() != p17.specificHeatPressure()) ++mismatches;
    if (mixed.specificHeatVolume() != p17.specificHeatVolume()) ++mismatches;
    if (mixed.specificHeatRatio() != p17.specificHeatRatio()) ++mismatches;
    for (const Real pressure : pressures) {
      if (mixed.density(pressure, temperature) != p17.density(pressure, temperature)) ++mismatches;
    }
  }
  std::printf("  bitwise mismatches over %zu T x %zu p, 5 quantities : %d   (must be 0)\n",
              temperatures.size(), pressures.size(), mismatches);
  std::printf("\n");

  // ---- the wrong-formula margins, so the controls are quantified ----
  std::printf("=== control margins ===\n");
  const std::vector<Real> half{0.5, 0.5, 0.0};
  const Real correctM = mixture.molecularWeight(half);
  const Real wrongM = (0.5 * mH2) + (0.5 * mCO2);
  std::printf("  N1 arithmetic M / harmonic M           %.4f x   (correct %.6e, wrong %.6e)\n",
              wrongM / correctM, correctM, wrongM);
  const Real correctCp = mixture.specificHeatPressure({0.25, 0.75, 0.0}, 300.0);
  const Real wrongCp = (14307.0 + 844.0) / 2.0;
  std::printf("  N3 simple mean cp / mass-weighted cp   %.4f x   (correct %.6e, wrong %.6e)\n",
              wrongCp / correctCp, correctCp, wrongCp);
  const Real correctGamma = mixture.properties(half, 300.0).specificHeatRatio();
  const Real wrongGamma = (0.5 * mixture.species(0).specificHeatRatio(300.0)) +
                          (0.5 * mixture.species(1).specificHeatRatio(300.0));
  std::printf("  N5 averaged gamma vs derived gamma     %.6f vs %.6f  (%.2f%% error)\n", wrongGamma,
              correctGamma, 100.0 * std::abs(wrongGamma - correctGamma) / correctGamma);
  const Real rhoH2 = mixture.density({1.0, 0.0, 0.0}, 101325.0, 300.0);
  const Real rhoCO2 = mixture.density({0.0, 1.0, 0.0}, 101325.0, 300.0);
  std::printf("  N2/N6 rho(pure CO2) / rho(pure H2)     %.4f x   at identical p and T\n",
              rhoCO2 / rhoH2);
  std::printf("\n");

  // ---- determinism ----
  std::printf("=== determinism (section 22) ===\n");
  const std::vector<Real> ternary{0.2, 0.3, 0.5};
  const Real m0 = mixture.molecularWeight(ternary);
  const auto pr0 = mixture.properties(ternary, 700.0);
  const Real rho0 = mixture.density(ternary, 101325.0, 700.0);
  int differences = 0;
  for (int repeat = 0; repeat < 1000; ++repeat) {
    if (mixture.molecularWeight(ternary) != m0) ++differences;
    const auto pr = mixture.properties(ternary, 700.0);
    if (pr.gasConstant() != pr0.gasConstant()) ++differences;
    if (pr.specificHeatPressure() != pr0.specificHeatPressure()) ++differences;
    if (pr.specificHeatVolume() != pr0.specificHeatVolume()) ++differences;
    if (pr.specificHeatRatio() != pr0.specificHeatRatio()) ++differences;
    if (mixture.density(ternary, 101325.0, 700.0) != rho0) ++differences;
  }
  std::printf("  repeats 1000, 6 quantities, bitwise differences : %d   (must be 0)\n",
              differences);
  return 0;
}
