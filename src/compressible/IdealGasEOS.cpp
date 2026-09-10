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

}  // namespace cfd::compressible
