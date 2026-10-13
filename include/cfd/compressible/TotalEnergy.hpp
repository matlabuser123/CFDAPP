#pragma once

#include "cfd/compressible/ThermodynamicProperties.hpp"
#include "cfd/core/Types.hpp"
#include "cfd/fields/ScalarField.hpp"
#include "cfd/fields/VectorField.hpp"
#include "cfd/mesh/Mesh.hpp"
#include "cfd/thermal/Enthalpy.hpp"

// P17.5 -- the total-energy thermodynamic state.
//
// NAMING, fixed to avoid the usual collision
// ------------------------------------------
// TODO.md writes "E = rho e_t", while much of the literature uses E for the SPECIFIC total
// energy. A bare `E` is therefore never used here:
//
//   e        specific internal energy           [J/kg]
//   ke       specific kinetic energy            [J/kg]
//   eTotal   specific total energy              [J/kg]
//   rhoE     conserved total-energy density     [J/m^3]
//   h        specific static enthalpy           [J/kg]
//   hTotal   specific total enthalpy            [J/kg]
//
// THE DEFINING CHOICE: e(T) = h(T) - R T
// --------------------------------------
// h(T) comes from P15's SensibleEnthalpy, which integrates cp in CLOSED FORM from its own
// reference state. Internal energy is then *defined* as h(T) - R T rather than as cv*T, and that
// one decision carries the whole contract:
//
//   * h - e = R T is EXACT BY CONSTRUCTION, for any reference state. Measured: worst
//     |(h-e) - R T| = 0 at T_ref = 0 AND at T_ref = 298.15 (results/p17/total_energy,
//     logs/01_dryrun.log section C). Exactly zero, because the subtraction is algebraic.
//   * de/dT = cp - R = cv follows automatically, so nothing has to assert it separately.
//   * P15's reference convention is PRESERVED, not redefined: e inherits its zero point from h,
//     e(T_ref) = h_ref - R T_ref.
//   * h = e + p/rho follows, because p/rho = R T for an ideal gas -- the enthalpy identity and
//     the EOS become the same statement instead of two that must be reconciled.
//
// and then, with no further assumption,
//
//   rho hTotal = rho (h + ke) = rho (e + R T + ke) = rhoE + p
//
// which is why the convective total-energy flux may be written either as (rhoE + p) U or as
// m_dot_f hTotal_f.
//
// WHY e = cv T AND THE GAMMA PRESSURE FORM ARE NOT USED
// ----------------------------------------------------
// Both are exact only under the ZERO reference (T_ref = 0, h_ref = 0). Measured on the unchanged
// tree, with cp = 1005, R = 287.05:
//
//   quantity                          T_ref = 0        T_ref = 298.15
//   |e - cv T|                        0                299640.75  = cp T_ref
//   p(gamma) - rho R T, relative      0.76 eps         0.83, i.e. 83 PERCENT WRONG
//
// So pressure is recovered as e -> T -> p = rho R T through P17.2's EOS, and NEVER as
// p = (gamma-1)(rhoE - 0.5 rho |U|^2). Had the gamma form been used as the pressure path -- which
// is how it is usually written -- a case carrying a nonzero reference would have been 83% wrong
// while every internal consistency check still passed, because they would all have shared the same
// wrong p. The gamma form is kept only as a VERIFICATION identity, asserted where
// hasZeroReference() makes it exact.
//
// THE DUAL-cp HAZARD, CLOSED HERE RATHER THAN MOVED
// ------------------------------------------------
// physics.thermal.specific_heat (a cp(T) model, used by P15 to produce temperature) and
// physics.compressible.specific_heat_pressure (one constant, used by P17.2 to consume it) are
// parsed independently and NOTHING cross-validated them: a thermal-coupled compressible case could
// declare two different gases and run silently. This class owns both and refuses to construct when
// they disagree, so the inconsistency becomes a diagnostic at the only place that can see both.
//
// A VARIABLE cp IS REJECTED, and that is a scope boundary rather than a limitation of the maths:
// ThermodynamicProperties stores a single cp and derives cv and gamma from it, so a cp(T) enthalpy
// model cannot be made consistent with the EOS block at all. Rejecting it with a named error is
// the opposite of silently using h = cp(T) T, which P15's own Enthalpy.hpp measures as 26-103%
// wrong. Supporting cp(T) needs a variable-gamma EOS and is follow-on work.

namespace cfd::compressible {

// The thermodynamic energy contract: one object owning the EOS (for R) and the enthalpy model
// (for cp and h(T)), with their consistency enforced at construction.
class TotalEnergyState {
 public:
  // Throws InvalidArgumentError if the enthalpy model's specific heat is not CONSTANT (the EOS
  // block cannot represent cp(T)), or if that constant disagrees with
  // thermodynamics.specificHeatPressure() by more than kSpecificHeatRelativeTolerance.
  TotalEnergyState(ThermodynamicProperties thermodynamics, cfd::thermal::SensibleEnthalpy enthalpy);

  // The agreement required between the two cp sources. Derived, not guessed: the two values
  // travel through JSON parsing and a polynomial evaluation, so they can differ by a few
  // round-offs of their own magnitude; anything larger is two different gases rather than two
  // roundings of one.
  static constexpr Real kSpecificHeatRelativeTolerance = 1e-12;

  // --- the state, per the naming above -----------------------------------
  [[nodiscard]] Real specificStaticEnthalpy(Real temperature) const;  // h(T), straight from P15
  [[nodiscard]] Real specificInternalEnergy(Real temperature) const;  // e = h(T) - R T
  [[nodiscard]] static Real specificKineticEnergy(Vector2 velocity);  // ke = 0.5 |U|^2
  [[nodiscard]] Real specificTotalEnergy(Real temperature, Vector2 velocity) const;    // e + ke
  [[nodiscard]] Real specificTotalEnthalpy(Real temperature, Vector2 velocity) const;  // h + ke
  // rhoE = rho (e + ke). Throws if density is not finite and > 0.
  [[nodiscard]] Real conservedTotalEnergy(Real density, Real temperature, Vector2 velocity) const;

  // --- conserved -> primitive --------------------------------------------
  struct RecoveredState {
    Vector2 velocity;               // (rho U) / rho
    Real specificKineticEnergy{};   // 0.5 |U|^2
    Real specificInternalEnergy{};  // rhoE/rho - ke
    Real temperature{};             // the exact inverse of e(T)
    Real pressure{};                // ABSOLUTE, from P17.2's EOS -- never from the gamma form
  };

  // Throws InvalidArgumentError if density is not finite and > 0, if rhoE or the momentum density
  // is not finite, if the recovered internal energy is not a valid thermodynamic state (kinetic
  // energy exceeding the available total energy), or if the recovered temperature falls outside
  // the property model's validity range. NOTHING is clamped and no energy floor is applied: a
  // clamped state corresponds to an energy the solver does not have and would let a diverging
  // solve look converged -- the same policy P15's enthalpy inversion already enforces.
  [[nodiscard]] RecoveredState recover(Real density, Vector2 momentumDensity, Real rhoE) const;

  // T from e, by inverting e(T) = h(T) - R T. For the calorically perfect gas this class enforces,
  // the inverse is CLOSED FORM and exact:
  //     e = h_ref + cp (T - T_ref) - R T = (h_ref - cp T_ref) + cv T
  //  => T = (e - h_ref + cp T_ref) / cv
  // so no root find is introduced and no iteration tolerance is spent. P15's bisection is not
  // duplicated; it remains the authority for the FORWARD direction, and
  // TotalEnergyStateTest.InternalEnergyInverseAgreesWithTheForwardModel checks the two against
  // each other rather than trusting either alone.
  [[nodiscard]] Real temperatureFromInternalEnergy(Real specificInternalEnergy) const;

  // True when T_ref == 0 and h_ref == 0, i.e. when e = cv T, h = cp T and
  // p = (gamma-1)(rhoE - 0.5 rho |U|^2) are exact. The calorically perfect shortcuts are asserted
  // only where this holds.
  [[nodiscard]] bool hasZeroReference() const noexcept;

  [[nodiscard]] const ThermodynamicProperties& thermodynamics() const noexcept { return thermo_; }
  [[nodiscard]] const cfd::thermal::SensibleEnthalpy& enthalpyModel() const noexcept {
    return enthalpy_;
  }

 private:
  ThermodynamicProperties thermo_;
  cfd::thermal::SensibleEnthalpy enthalpy_;
};

// --- field-level conversions ---------------------------------------------
// Each validates sizes against the mesh and names the offending cell on failure, the same
// convention evaluateDensityField and evaluatePropertyField already use.

[[nodiscard]] cfd::fields::ScalarField conservedTotalEnergyField(
    const cfd::mesh::Mesh& mesh, const cfd::fields::ScalarField& density,
    const cfd::fields::ScalarField& temperature, const cfd::fields::VectorField& velocity,
    const TotalEnergyState& state);

[[nodiscard]] cfd::fields::ScalarField specificTotalEnthalpyField(
    const cfd::mesh::Mesh& mesh, const cfd::fields::ScalarField& temperature,
    const cfd::fields::VectorField& velocity, const TotalEnergyState& state);

// The per-cell recovered primitive state. `momentumDensity` is rho*U, not U.
struct RecoveredFields {
  cfd::fields::VectorField velocity;
  cfd::fields::ScalarField temperature;
  cfd::fields::ScalarField pressure;  // absolute
  cfd::fields::ScalarField specificInternalEnergy;
};

[[nodiscard]] RecoveredFields recoverPrimitiveFields(
    const cfd::mesh::Mesh& mesh, const cfd::fields::ScalarField& density,
    const cfd::fields::VectorField& momentumDensity, const cfd::fields::ScalarField& rhoE,
    const TotalEnergyState& state);

}  // namespace cfd::compressible
