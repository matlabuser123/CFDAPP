#pragma once

#include "cfd/core/Types.hpp"
#include "cfd/thermal/TemperatureDependentProperty.hpp"

// P15 -- sensible enthalpy, and the inversion back to temperature.
//
// WHY THIS EXISTS
// ---------------
// The existing energy equation transports cp*T: `assembleThermalConvectionContribution`
// scales the mass flux by cp because, in its own header's words, "the transported
// quantity here is cp*T rather than T itself". For CONSTANT cp that is identical
// to transporting enthalpy. For cp(T) it is not, and the difference is not small:
//
//   h(T) = h_ref + integral_{T_ref}^{T} cp(theta) dtheta       correct
//   h    = cp * T                                              wrong when cp varies
//
// With cp(T) = b0 + b1 T, the true integral carries b1/2 (T^2 - T_ref^2); the
// naive product carries b1 T^2. Those differ by a factor of two in the
// quadratic term, so a solver that used the product would conserve something,
// just not energy.
//
// THE CLOSED FORM
// ---------------
// For cp(T) = sum_i b_i T^i the integral is exact term by term:
//
//   h(T) - h_ref = sum_i  b_i / (i+1)  * ( T^{i+1} - T_ref^{i+1} )
//
// so no quadrature is needed and no quadrature error is introduced. This is
// implemented rather than a numerical integral precisely so the enthalpy is
// exact for the property model actually configured.
//
// THE INVERSION
// -------------
// T(h) has no closed form for a general polynomial, so it is solved. h(T) is
// strictly increasing wherever cp > 0 -- which the property model guarantees by
// refusing a non-positive polynomial over its range -- so the inverse exists
// and is unique, and bisection on the bracket [Tmin, Tmax] cannot fail to
// converge. Bisection is chosen over Newton deliberately: it needs no
// derivative, cannot overshoot outside the validity range, and its iteration
// count is a fixed function of the bracket width and tolerance rather than of
// how well an initial guess was chosen.
//
// An enthalpy outside [h(Tmin), h(Tmax)] is an ERROR, not a clamp. Clamping
// would return a temperature the energy does not correspond to and let a
// diverging solve look converged.

namespace cfd::thermal {

// Sensible enthalpy relative to a reference state.
class SensibleEnthalpy {
 public:
  // Throws InvalidArgumentError if referenceTemperature is outside the
  // property's validity range or not finite.
  SensibleEnthalpy(TemperatureDependentProperty specificHeat, Real referenceTemperature,
                   Real referenceEnthalpy = 0.0);

  // h(T) = h_ref + integral_{T_ref}^{T} cp dtheta, evaluated in closed form.
  // Throws if T is outside the validity range.
  [[nodiscard]] Real enthalpy(Real temperature) const;

  // T(h) by bisection. Throws InvalidArgumentError if `h` lies outside the
  // enthalpy range implied by the property's temperature range -- never
  // clamped, never extrapolated.
  [[nodiscard]] Real temperature(Real enthalpy) const;

  // cp at T, for callers assembling with the field overloads.
  [[nodiscard]] Real specificHeat(Real temperature) const {
    return specificHeat_.evaluate(temperature);
  }

  [[nodiscard]] Real referenceTemperature() const noexcept { return referenceTemperature_; }
  [[nodiscard]] Real referenceEnthalpy() const noexcept { return referenceEnthalpy_; }
  [[nodiscard]] const TemperatureDependentProperty& specificHeatModel() const noexcept {
    return specificHeat_;
  }

  // The absolute tolerance the inversion converges to, in kelvin. Exposed so a
  // test can assert the round trip against the documented bound rather than a
  // number it discovered.
  static constexpr Real kInversionToleranceKelvin = 1e-12;

 private:
  TemperatureDependentProperty specificHeat_;
  Real referenceTemperature_;
  Real referenceEnthalpy_;
};

}  // namespace cfd::thermal
