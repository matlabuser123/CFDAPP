#include "cfd/species/MixtureThermodynamics.hpp"

#include <cmath>
#include <limits>
#include <sstream>
#include <string>
#include <utility>

#include "cfd/core/Exception.hpp"

namespace cfd::species {
namespace {

std::string exact(Real value) {
  std::ostringstream out;
  out.precision(std::numeric_limits<Real>::max_digits10);
  out << value;
  return out.str();
}

void requireFinitePositive(Real value, const std::string& what, const std::string& where) {
  if (!std::isfinite(value) || !(value > 0.0)) {
    throw InvalidArgumentError(where + ": " + what + " is " + exact(value) +
                               "; must be finite and > 0");
  }
}

}  // namespace

// ===================================================================
// SpeciesThermodynamics
// ===================================================================

SpeciesThermodynamics::SpeciesThermodynamics(Real molecularWeight,
                                             cfd::thermal::SensibleEnthalpy enthalpy)
    : molecularWeight_(molecularWeight),
      // R_i = R_u / M_i, once. Never stored alongside M_i as an independent
      // datum, so the two cannot disagree.
      gasConstant_(kUniversalGasConstant / molecularWeight),
      enthalpy_(std::move(enthalpy)) {
  requireFinitePositive(molecularWeight_, "molecular weight", "SpeciesThermodynamics");
  // Recomputed after validation: the initialiser above ran before the check, so a
  // zero or non-finite M would have produced an inf or NaN R that must not escape.
  gasConstant_ = kUniversalGasConstant / molecularWeight_;
}

Real SpeciesThermodynamics::specificHeatPressure(Real temperature) const {
  const Real cp = enthalpy_.specificHeat(temperature);
  requireFinitePositive(cp, "cp at T = " + exact(temperature), "SpeciesThermodynamics");
  return cp;
}

Real SpeciesThermodynamics::specificHeatVolume(Real temperature) const {
  const Real cv = specificHeatPressure(temperature) - gasConstant_;
  if (!std::isfinite(cv) || !(cv > 0.0)) {
    // cv <= 0 is gamma <= 1, i.e. not a physically admissible ideal gas. Reported
    // as cv, because that is the quantity the caller can inspect.
    throw InvalidArgumentError(
        "SpeciesThermodynamics: cv = cp - R = " + exact(cv) + " at T = " + exact(temperature) +
        "; must be > 0 (equivalently gamma > 1). cp = " + exact(specificHeatPressure(temperature)) +
        ", R = " + exact(gasConstant_));
  }
  return cv;
}

Real SpeciesThermodynamics::specificHeatRatio(Real temperature) const {
  return specificHeatPressure(temperature) / specificHeatVolume(temperature);
}

Real SpeciesThermodynamics::enthalpy(Real temperature) const {
  return enthalpy_.enthalpy(temperature);
}

// ===================================================================
// MixtureThermodynamics
// ===================================================================

MixtureThermodynamics::MixtureThermodynamics(SpeciesRegistry registry,
                                             std::vector<SpeciesThermodynamics> species)
    : registry_(std::move(registry)), species_(std::move(species)) {
  if (species_.size() != registry_.size()) {
    throw InvalidArgumentError("MixtureThermodynamics: " + std::to_string(species_.size()) +
                               " thermodynamic entries for " + std::to_string(registry_.size()) +
                               " species; one per species is required");
  }
}

const SpeciesThermodynamics& MixtureThermodynamics::species(Index index) const {
  if (index >= species_.size()) {
    throw InvalidArgumentError("MixtureThermodynamics: species index " + std::to_string(index) +
                               " is out of range; the mixture holds " +
                               std::to_string(species_.size()) + " species");
  }
  return species_[index];
}

void MixtureThermodynamics::requireComposition(const std::vector<Real>& composition) const {
  if (composition.size() != registry_.size()) {
    throw InvalidArgumentError("MixtureThermodynamics: composition has " +
                               std::to_string(composition.size()) + " entries; expected " +
                               std::to_string(registry_.size()));
  }
  // NOTHING ELSE IS CHECKED, deliberately. A composition comes from a P18.1
  // MassFractionState, which has already enforced finiteness, 0 <= Y <= 1 and
  // |sum Y - 1| <= 4 eps N at construction. Re-checking here would duplicate
  // that logic (authorization section 20) and would invite the two copies to
  // disagree. Nothing is renormalised.
}

Real MixtureThermodynamics::molecularWeight(const std::vector<Real>& composition) const {
  requireComposition(composition);
  // 1/M_mix = sum_i Y_i / M_i, in FIXED ascending species order.
  //
  // THE MASS-FRACTION RELATION. sum_i Y_i M_i is the mole-fraction formula
  // misapplied and gives a different -- wrong -- answer whenever the molecular
  // weights differ; control N1 detects exactly that substitution.
  Real reciprocal = 0.0;
  for (Index i = 0; i < composition.size(); ++i) {
    reciprocal += composition[i] / species_[i].molecularWeight();
  }
  if (!std::isfinite(reciprocal) || !(reciprocal > 0.0)) {
    throw InvalidArgumentError(
        "MixtureThermodynamics: sum_i Y_i / M_i = " + exact(reciprocal) +
        "; must be finite and > 0. A composition of all zeros cannot define a molecular weight.");
  }
  return 1.0 / reciprocal;
}

Real MixtureThermodynamics::gasConstant(const std::vector<Real>& composition) const {
  requireComposition(composition);
  // R_mix = sum_i Y_i R_i, in FIXED ascending species order.
  //
  // THIS FORM AND NOT R_u * sum(Y_i/M_i), which is algebraically identical but
  // NOT bitwise equal to R_1 in the single-species limit -- measured to fail for
  // oxygen (M = 0.032). Here, at Y_1 = 1, the sum is 1.0 * R_1 = R_1 exactly,
  // because multiplication by exactly 1.0 is exact. That is what makes G5's
  // bitwise single-species criterion reachable at all.
  Real total = 0.0;
  for (Index i = 0; i < composition.size(); ++i) {
    total += composition[i] * species_[i].gasConstant();
  }
  return total;
}

Real MixtureThermodynamics::gasConstantFromMolecularWeight(
    const std::vector<Real>& composition) const {
  // The equivalent form, exposed only so a test can cross-check it against
  // gasConstant() rather than assume the two agree. Nothing else calls it.
  return kUniversalGasConstant / molecularWeight(composition);
}

Real MixtureThermodynamics::specificHeatPressure(const std::vector<Real>& composition,
                                                 Real temperature) const {
  requireComposition(composition);
  // cp_mix = sum_i Y_i cp_i(T), MASS-weighted because cp_i is mass-based, in
  // FIXED ascending species order. Mole-fraction weighting here would be a basis
  // error; control N3 detects it. At Y_1 = 1 this is 1.0 * cp_1 = cp_1 bitwise.
  Real total = 0.0;
  for (Index i = 0; i < composition.size(); ++i) {
    total += composition[i] * species_[i].specificHeatPressure(temperature);
  }
  return total;
}

Real MixtureThermodynamics::enthalpy(const std::vector<Real>& composition, Real temperature) const {
  requireComposition(composition);
  // h_mix = sum_i Y_i h_i(T), each h_i the INTEGRATED sensible enthalpy P15
  // already provides -- not cp_i(T) * T, which P15 measures as 26-103% wrong.
  Real total = 0.0;
  for (Index i = 0; i < composition.size(); ++i) {
    total += composition[i] * species_[i].enthalpy(temperature);
  }
  return total;
}

Real MixtureThermodynamics::internalEnergy(const std::vector<Real>& composition,
                                           Real temperature) const {
  // e = h - R T, the same relation TotalEnergyState::specificInternalEnergy uses,
  // with R_mix substituted for the single-species R.
  return enthalpy(composition, temperature) - (gasConstant(composition) * temperature);
}

cfd::compressible::ThermodynamicProperties MixtureThermodynamics::properties(
    const std::vector<Real>& composition, Real temperature) const {
  const Real rMix = gasConstant(composition);
  const Real cpMix = specificHeatPressure(composition, temperature);
  const Real cvMix = cpMix - rMix;
  if (!std::isfinite(cvMix) || !(cvMix > 0.0)) {
    // Checked here, before ThermodynamicProperties sees it, so the message can
    // name the composition and temperature that produced it rather than only the
    // offending number.
    throw InvalidArgumentError("MixtureThermodynamics: cv_mix = cp_mix - R_mix = " + exact(cvMix) +
                               " at T = " + exact(temperature) +
                               "; must be > 0 (equivalently gamma_mix > 1). cp_mix = " +
                               exact(cpMix) + ", R_mix = " + exact(rMix));
  }
  // The EXISTING P17 type. It derives cv = cp - R and gamma = cp/cv itself and
  // owns the IdealGasEOS, so the closure and the EOS are inherited rather than
  // reimplemented -- and with R_mix and cp_mix bitwise equal to a single
  // species' values, this object is bit-identical to the one P17 builds.
  return cfd::compressible::ThermodynamicProperties(rMix, cpMix);
}

Real MixtureThermodynamics::density(const std::vector<Real>& composition, Real pressure,
                                    Real temperature) const {
  return properties(composition, temperature).density(pressure, temperature);
}

// ===================================================================
// Per-cell fields -- the P18.2 -> P18.3 join
// ===================================================================

MixtureFields evaluateMixtureFields(const MixtureThermodynamics& mixture,
                                    const MassFractionState& state,
                                    const cfd::fields::ScalarField& pressure,
                                    const cfd::fields::ScalarField& temperature) {
  if (state.speciesCount() != mixture.speciesCount()) {
    throw InvalidArgumentError(
        "evaluateMixtureFields: the state holds " + std::to_string(state.speciesCount()) +
        " species; the mixture holds " + std::to_string(mixture.speciesCount()));
  }
  if (state.registry() != mixture.registry()) {
    // Ordering is part of a registry's value (P18.1), so a mismatch here would
    // silently read every composition against the wrong species.
    throw InvalidArgumentError(
        "evaluateMixtureFields: the state's species registry differs from the mixture's; a "
        "composition read against a different ordering would be silently wrong");
  }
  const Index n = state.cellCount();
  if (pressure.size() != n || temperature.size() != n) {
    throw InvalidArgumentError("evaluateMixtureFields: pressure and temperature must hold " +
                               std::to_string(n) + " cells");
  }

  MixtureFields out{cfd::fields::ScalarField(n), cfd::fields::ScalarField(n),
                    cfd::fields::ScalarField(n), cfd::fields::ScalarField(n),
                    cfd::fields::ScalarField(n), cfd::fields::ScalarField(n)};

  for (Index cell = 0; cell < n; ++cell) {
    // EACH CELL FROM ITS OWN LOCAL COMPOSITION -- which is the whole point of
    // section 25. A spatially varying Y_i therefore produces spatially varying
    // M_mix, R_mix, cp_mix and rho.
    const std::vector<Real> composition = state.cellComposition(cell);
    const auto properties = mixture.properties(composition, temperature[cell]);
    out.molecularWeight[cell] = mixture.molecularWeight(composition);
    out.gasConstant[cell] = properties.gasConstant();
    out.specificHeatPressure[cell] = properties.specificHeatPressure();
    out.specificHeatVolume[cell] = properties.specificHeatVolume();
    out.specificHeatRatio[cell] = properties.specificHeatRatio();
    out.density[cell] = properties.density(pressure[cell], temperature[cell]);
  }
  return out;
}

}  // namespace cfd::species
