#pragma once

#include <vector>

#include "cfd/compressible/ThermodynamicProperties.hpp"
#include "cfd/core/Types.hpp"
#include "cfd/species/MassFractionState.hpp"
#include "cfd/species/SpeciesRegistry.hpp"
#include "cfd/thermal/Enthalpy.hpp"

// P18.3 -- composition-dependent ideal-gas mixture thermodynamics.
//
// WHAT THIS IS NOT: a second EOS, a second enthalpy stack, or a second
// thermodynamic closure. Only TWO quantities are computed here -- R_mix and
// cp_mix -- and everything else is obtained by constructing the EXISTING
// cfd::compressible::ThermodynamicProperties from them. That type already
// derives cv = cp - R and gamma = cp/cv at construction and already owns an
// IdealGasEOS, so:
//
//   * cv_mix, gamma_mix and the EOS come from already-qualified P17 code;
//   * cp_mix, cv_mix and R_mix CANNOT drift apart, because only two of them are
//     ever stored and the third is derived from those two;
//   * gamma_mix EMERGES from cp and cv and is never interpolated from gamma_i;
//   * the single-species limit is bitwise for cv, gamma and density AUTOMATICALLY
//     once it is bitwise for R and cp -- the constructed object is bit-identical
//     to the one P17 builds.
//
// cp_i(T) and h_i(T) come from P15's SensibleEnthalpy, which integrates cp(T)
// properly rather than evaluating cp(T)*T -- a substitution P15's own header
// measures as 26-103% wrong. P15's reference conventions are preserved per
// species and never re-based.
//
// ===================================================================
// THE ONE FORMULATION CHOICE THAT IS NOT OBVIOUS, and it is forced by
// measurement rather than taste:
//
//     R_mix = sum_i Y_i R_i        with R_i = R_u / M_i evaluated ONCE
//
// and NOT the algebraically identical R_u * sum_i(Y_i / M_i). G5 requires the
// single-species limit to be BITWISE, and:
//
//     sum_i Y_i R_i  at Y_1 = 1  is  1.0 * R_1 = R_1   EXACTLY
//     R_u * (1/M)  ==  R_u / M   is FALSE for M = 0.032 kg/mol -- OXYGEN
//     1 / (1 / x)  ==  x         is FALSE for 17.2% of random doubles
//
// Both forms are still cross-checked against each other, because they must agree
// to round-off and a larger disagreement would mean one is wrong.
// results/p18/mixture_properties/audit.md section 3.
// ===================================================================
//
// NO CHEMISTRY. No reaction, no stoichiometry, no Arrhenius, no heat release, and
// no formation enthalpy -- P15's referenceEnthalpy is a SENSIBLE offset and is
// deliberately not repurposed as an h_f. Those are P18.4-P18.6.
namespace cfd::species {

// CODATA 2018; exact by the 2019 SI redefinition. [J / (mol K)]
//
// Kept here rather than in cfd/core/Constants.hpp, which deliberately holds no
// tunable physical constants -- putting a substance-independent physical
// constant there would change what that header means.
inline constexpr Real kUniversalGasConstant = 8.31446261815324;

// One species' thermodynamic data: a molecular weight plus P15's cp(T)/h(T)
// model. R_i is DERIVED from M_i, never stored alongside it, so the two cannot
// disagree.
class SpeciesThermodynamics {
 public:
  // Throws InvalidArgumentError if molecularWeight is not finite or not > 0.
  //
  // cp and cv are validated lazily, at each temperature they are evaluated at,
  // rather than once at construction: cp_i(T) is a polynomial with a validity
  // range, so "is cp positive" is a question about a temperature, not about the
  // object. cv_i(T) <= 0 (equivalently gamma_i <= 1) is rejected where it is
  // evaluated, with the species named.
  SpeciesThermodynamics(Real molecularWeight, cfd::thermal::SensibleEnthalpy enthalpy);

  [[nodiscard]] Real molecularWeight() const noexcept { return molecularWeight_; }

  // R_i = R_u / M_i, computed once at construction.
  [[nodiscard]] Real gasConstant() const noexcept { return gasConstant_; }

  [[nodiscard]] Real specificHeatPressure(Real temperature) const;  // cp_i(T)
  [[nodiscard]] Real specificHeatVolume(Real temperature) const;    // cv_i = cp_i(T) - R_i
  [[nodiscard]] Real specificHeatRatio(Real temperature) const;     // gamma_i = cp_i/cv_i
  [[nodiscard]] Real enthalpy(Real temperature) const;              // h_i(T), integrated by P15

  [[nodiscard]] const cfd::thermal::SensibleEnthalpy& enthalpyModel() const noexcept {
    return enthalpy_;
  }

 private:
  Real molecularWeight_{};
  Real gasConstant_{};
  cfd::thermal::SensibleEnthalpy enthalpy_;
};

// The mixture. Holds the species identity (so a composition can never be read
// against the wrong ordering) and one SpeciesThermodynamics per species.
class MixtureThermodynamics {
 public:
  // Throws InvalidArgumentError if the counts disagree.
  MixtureThermodynamics(SpeciesRegistry registry, std::vector<SpeciesThermodynamics> species);

  [[nodiscard]] const SpeciesRegistry& registry() const noexcept { return registry_; }
  [[nodiscard]] Index speciesCount() const noexcept { return registry_.size(); }
  [[nodiscard]] const SpeciesThermodynamics& species(Index index) const;

  // Every accessor below takes a COMPOSITION, which is one cell's mass fractions
  // in species-index order. Compositions are obtained from a P18.1
  // MassFractionState -- which has already enforced finiteness, 0 <= Y <= 1 and
  // sum(Y) = 1 -- so NO validation is duplicated here and NOTHING is
  // renormalised. The only check is that the length matches the registry.

  // 1 / M_mix = sum_i (Y_i / M_i).  THE MASS-FRACTION relation; the arithmetic
  // form sum_i Y_i M_i is the mole-fraction formula misapplied and is wrong.
  [[nodiscard]] Real molecularWeight(const std::vector<Real>& composition) const;

  // R_mix = sum_i Y_i R_i -- see the header note on why this form and not the
  // other. Bitwise R_1 when one species is at Y = 1.
  [[nodiscard]] Real gasConstant(const std::vector<Real>& composition) const;

  // R_u / M_mix: the equivalent form, exposed SO IT CAN BE CROSS-CHECKED against
  // gasConstant() rather than assumed equal to it. Not used by anything else.
  [[nodiscard]] Real gasConstantFromMolecularWeight(const std::vector<Real>& composition) const;

  // cp_mix = sum_i Y_i cp_i(T). MASS-weighted, because cp_i is mass-based.
  [[nodiscard]] Real specificHeatPressure(const std::vector<Real>& composition,
                                          Real temperature) const;

  // h_mix = sum_i Y_i h_i(T), each h_i integrated by P15.
  [[nodiscard]] Real enthalpy(const std::vector<Real>& composition, Real temperature) const;

  // e_mix = h_mix - R_mix T, the same relation TotalEnergyState already uses with
  // R_mix substituted for R.
  [[nodiscard]] Real internalEnergy(const std::vector<Real>& composition, Real temperature) const;

  // THE KEY ENTRY POINT. Returns the EXISTING P17 type built from (R_mix, cp_mix),
  // so cv_mix, gamma_mix, density, pressure, temperature and speed of sound all
  // come from qualified code and are consistent by construction.
  //
  // Throws InvalidArgumentError if cv_mix <= 0 (equivalently gamma_mix <= 1),
  // which ThermodynamicProperties itself also refuses -- the check is named here
  // so the message can say which composition and temperature produced it.
  [[nodiscard]] cfd::compressible::ThermodynamicProperties properties(
      const std::vector<Real>& composition, Real temperature) const;

  // rho = p / (R_mix T), through the EOS above. Provided because it is the
  // quantity §18 requires to change with composition, and a caller should not
  // have to know that it comes via ThermodynamicProperties to get it right.
  [[nodiscard]] Real density(const std::vector<Real>& composition, Real pressure,
                             Real temperature) const;

 private:
  void requireComposition(const std::vector<Real>& composition) const;

  SpeciesRegistry registry_;
  std::vector<SpeciesThermodynamics> species_;
};

// Per-cell mixture fields from a transported composition. This is the P18.2 ->
// P18.3 join (§25): each cell's properties come from ITS OWN local composition,
// so a spatially varying Y_i produces spatially varying R_mix, cp_mix and rho.
struct MixtureFields {
  cfd::fields::ScalarField molecularWeight;
  cfd::fields::ScalarField gasConstant;
  cfd::fields::ScalarField specificHeatPressure;
  cfd::fields::ScalarField specificHeatVolume;
  cfd::fields::ScalarField specificHeatRatio;
  cfd::fields::ScalarField density;
};

// Evaluates the mixture properties cell by cell. `pressure` and `temperature`
// are per-cell fields; the state supplies the per-cell composition.
[[nodiscard]] MixtureFields evaluateMixtureFields(const MixtureThermodynamics& mixture,
                                                  const MassFractionState& state,
                                                  const cfd::fields::ScalarField& pressure,
                                                  const cfd::fields::ScalarField& temperature);

}  // namespace cfd::species
