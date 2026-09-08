#include "cfd/boundary/Inlet.hpp"

#include <cmath>

#include "cfd/core/Exception.hpp"

namespace cfd::boundary {

Inlet::Inlet(Vector2 velocity) : velocity_(velocity) {
  if (!std::isfinite(velocity_.x) || !std::isfinite(velocity_.y)) {
    throw InvalidArgumentError("Inlet: velocity must be finite");
  }
}

const Vector2& Inlet::velocity() const noexcept { return velocity_; }

BoundaryConditionType Inlet::type() const noexcept { return BoundaryConditionType::Inlet; }
std::string_view Inlet::name() const noexcept { return "Inlet"; }

Vector2 Inlet::boundaryValue(const Vector2& /*ownerValue*/, Real /*normalDistance*/,
                             const Vector2& /*unitNormal*/) const {
  return velocity_;
}

}  // namespace cfd::boundary
