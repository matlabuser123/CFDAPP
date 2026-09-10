#include "cfd/multiphase/PhaseProperties.hpp"

#include <cmath>
#include <string>
#include <utility>

#include "cfd/core/Exception.hpp"

namespace cfd::multiphase {

namespace {

void validatePositiveFinite(Real value, const char* fieldName) {
  if (!std::isfinite(value) || !(value > 0.0)) {
    throw InvalidArgumentError(std::string("PhaseProperties: ") + fieldName +
                               " must be finite and > 0");
  }
}

}  // namespace

PhaseProperties::PhaseProperties(std::string name, Real density, Real viscosity)
    : name_(std::move(name)), density_(density), viscosity_(viscosity) {
  if (name_.empty()) {
    throw InvalidArgumentError("PhaseProperties: name must not be empty");
  }
  validatePositiveFinite(density_, "density");
  validatePositiveFinite(viscosity_, "viscosity");
}

const std::string& PhaseProperties::name() const noexcept { return name_; }
Real PhaseProperties::density() const noexcept { return density_; }
Real PhaseProperties::viscosity() const noexcept { return viscosity_; }

}  // namespace cfd::multiphase
