#include "cfd/boundary/FixedValue.hpp"

#include <cmath>

#include "cfd/core/Exception.hpp"

namespace cfd::boundary {

FixedValue::FixedValue(Real value) : value_(value) {
  if (!std::isfinite(value_)) {
    throw InvalidArgumentError("FixedValue: value must be finite");
  }
}

Real FixedValue::value() const noexcept { return value_; }

BoundaryConditionType FixedValue::type() const noexcept {
  return BoundaryConditionType::FixedValue;
}
std::string_view FixedValue::name() const noexcept { return "FixedValue"; }

Real FixedValue::boundaryValue(Real /*ownerValue*/, Real /*normalDistance*/) const {
  return value_;
}

}  // namespace cfd::boundary
