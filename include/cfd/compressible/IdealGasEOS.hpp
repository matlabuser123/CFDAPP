#pragma once

#include "cfd/compressible/EquationOfState.hpp"

namespace cfd::compressible {

// P3-PHYS-006: the calorically-perfect ideal-gas equation of state --
// rho = p/(R*T) -- the mandatory first (and, for this task, only)
// EquationOfState implementation (this task's own section 5/57: real-gas
// EOS is explicitly out of scope).
//
// Analytical derivatives (section 15):
//   d(rho)/dp |_T = 1/(R*T)
//   d(rho)/dT |_p = -p/(R*T^2) = -rho/T
class IdealGasEOS final : public EquationOfState {
 public:
  // Throws InvalidArgumentError if gasConstant is not finite or <= 0.
  explicit IdealGasEOS(Real gasConstant);

  [[nodiscard]] Real gasConstant() const noexcept;

  // Throws InvalidArgumentError if pressure or temperature is not finite
  // and strictly positive, or if the resulting density is not finite and
  // strictly positive (unreachable for finite positive p,T with a
  // positive R, but checked anyway -- same "validate the output, not
  // just the input" convention this codebase already applies elsewhere,
  // e.g. evaluatePropertyField).
  [[nodiscard]] Real density(Real pressure, Real temperature) const override;
  [[nodiscard]] Real dDensityDPressure(Real pressure, Real temperature) const override;
  [[nodiscard]] Real dDensityDTemperature(Real pressure, Real temperature) const override;

 private:
  Real gasConstant_{};
};

}  // namespace cfd::compressible
