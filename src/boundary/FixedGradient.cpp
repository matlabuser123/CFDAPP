#include "cfd/boundary/FixedGradient.hpp"

#include <cmath>

#include "cfd/core/Exception.hpp"

namespace cfd::boundary {

FixedGradient::FixedGradient(Real gradient) : gradient_(gradient) {
  if (!std::isfinite(gradient_)) {
    throw InvalidArgumentError("FixedGradient: gradient must be finite");
  }
}

Real FixedGradient::gradient() const noexcept { return gradient_; }

BoundaryConditionType FixedGradient::type() const noexcept {
  return BoundaryConditionType::FixedGradient;
}
std::string_view FixedGradient::name() const noexcept { return "FixedGradient"; }

Real FixedGradient::boundaryValue(Real ownerValue, Real normalDistance) const {
  if (!std::isfinite(normalDistance) || !(normalDistance > 0.0)) {
    throw InvalidArgumentError("FixedGradient: normalDistance must be finite and positive");
  }
  return ownerValue + (gradient_ * normalDistance);
}

}  // namespace cfd::boundary
