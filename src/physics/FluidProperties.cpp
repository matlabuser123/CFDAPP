#include "cfd/physics/FluidProperties.hpp"

#include <cmath>
#include <string>

#include "cfd/core/Exception.hpp"

namespace cfd::physics {

namespace {

void validatePositiveFinite(Real value, const char* fieldName) {
  if (!std::isfinite(value) || !(value > 0.0)) {
    throw InvalidArgumentError(std::string("FluidProperties: ") + fieldName +
                               " must be finite and > 0");
  }
}

}  // namespace

FluidProperties::FluidProperties(Real density, Real dynamicViscosity)
    : density_(density), dynamicViscosity_(dynamicViscosity) {
  validatePositiveFinite(density_, "density");
  validatePositiveFinite(dynamicViscosity_, "dynamicViscosity");
}

Real FluidProperties::density() const noexcept { return density_; }
Real FluidProperties::dynamicViscosity() const noexcept { return dynamicViscosity_; }
Real FluidProperties::kinematicViscosity() const noexcept { return dynamicViscosity_ / density_; }

}  // namespace cfd::physics
