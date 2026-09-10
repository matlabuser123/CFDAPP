#include "cfd/boundary/FixedTemperature.hpp"

#include <cmath>

#include "cfd/core/Exception.hpp"

namespace cfd::boundary {

FixedTemperature::FixedTemperature(Real temperature) : temperature_(temperature) {
  if (!std::isfinite(temperature_)) {
    throw InvalidArgumentError("FixedTemperature: temperature must be finite");
  }
}

Real FixedTemperature::temperature() const noexcept { return temperature_; }

BoundaryConditionType FixedTemperature::type() const noexcept {
  return BoundaryConditionType::FixedTemperature;
}
std::string_view FixedTemperature::name() const noexcept { return "FixedTemperature"; }

Real FixedTemperature::boundaryValue(Real /*ownerValue*/, Real /*normalDistance*/) const {
  return temperature_;
}

}  // namespace cfd::boundary
