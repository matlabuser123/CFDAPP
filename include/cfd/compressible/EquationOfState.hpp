#pragma once

#include "cfd/core/Types.hpp"

namespace cfd::compressible {

// P3-PHYS-006: equation-of-state abstraction -- every compressible
// density calculation in this module goes through one of these (this
// task's own section 5: "Do not hard-code ideal-gas calculations
// throughout momentum, continuity, and energy code"). Mirrors the
// "solver consumes a value, does not know how it was produced"
// separation already established for
// cfd::physics::TemperatureProperty (P3-PHYS-003) and
// cfd::boundary::BoundaryCondition.
class EquationOfState {
 public:
  virtual ~EquationOfState() = default;

  // rho(p, T). p is *absolute* thermodynamic pressure (section 23 --
  // never a gauge/reference-relative pressure), T is absolute
  // temperature. Throws InvalidArgumentError if p or T is not finite and
  // strictly positive, or if the resulting density is not finite and
  // strictly positive.
  [[nodiscard]] virtual Real density(Real pressure, Real temperature) const = 0;

  // d(rho)/dp at constant T -- needed by the pressure-density correction
  // relation (section 15/34). Analytical, not a finite difference
  // (section 15's own "do not approximate... using arbitrary finite
  // differences if analytical derivatives are available").
  [[nodiscard]] virtual Real dDensityDPressure(Real pressure, Real temperature) const = 0;

  // d(rho)/dT at constant p.
  [[nodiscard]] virtual Real dDensityDTemperature(Real pressure, Real temperature) const = 0;
};

}  // namespace cfd::compressible
