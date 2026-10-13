#include "cfd/compressible/IdealGasEOS.hpp"

#include <cmath>
#include <string>

#include "cfd/core/Exception.hpp"

namespace cfd::compressible {

namespace {

void requirePositiveFinite(Real value, const char* fieldName) {
  if (!std::isfinite(value) || !(value > 0.0)) {
    throw InvalidArgumentError(std::string("IdealGasEOS: ") + fieldName +
                               " must be finite and > 0");
  }
}

}  // namespace

IdealGasEOS::IdealGasEOS(Real gasConstant) : gasConstant_(gasConstant) {
  requirePositiveFinite(gasConstant_, "gasConstant");
}

Real IdealGasEOS::gasConstant() const noexcept { return gasConstant_; }

Real IdealGasEOS::density(Real pressure, Real temperature) const {
  requirePositiveFinite(pressure, "pressure");
  requirePositiveFinite(temperature, "temperature");
  const Real rho = pressure / (gasConstant_ * temperature);
  requirePositiveFinite(rho, "density");
  return rho;
}

Real IdealGasEOS::dDensityDPressure(Real pressure, Real temperature) const {
  requirePositiveFinite(pressure, "pressure");
  requirePositiveFinite(temperature, "temperature");
  return 1.0 / (gasConstant_ * temperature);
}

Real IdealGasEOS::dDensityDTemperature(Real pressure, Real temperature) const {
  requirePositiveFinite(pressure, "pressure");
  requirePositiveFinite(temperature, "temperature");
  return -pressure / (gasConstant_ * temperature * temperature);
}

Real IdealGasEOS::temperature(Real pressure, Real density) const {
  // Same validation, same order, same messages as density() and pressure(). The third
  // direction has no reason to be laxer than the two it completes.
  requirePositiveFinite(pressure, "pressure");
  requirePositiveFinite(density, "density");
  const Real T = pressure / (density * gasConstant_);
  // Checked rather than assumed: rho and R can each be finite and positive while their
  // product underflows to zero, making the quotient infinite -- the mirror of the
  // overflow case pressure() guards.
  requirePositiveFinite(T, "temperature");
  return T;
}

Real IdealGasEOS::pressure(Real density, Real temperature) const {
  // Same validation, same order, same messages as density() -- the inverse has no
  // reason to be laxer than the forward direction, and a caller that has learned
  // what density() rejects should not have to learn a second set of rules.
  requirePositiveFinite(density, "density");
  requirePositiveFinite(temperature, "temperature");
  const Real p = density * gasConstant_ * temperature;
  // Checked rather than assumed: rho and T can each be finite and positive while
  // their product overflows to inf.
  requirePositiveFinite(p, "pressure");
  return p;
}

}  // namespace cfd::compressible
