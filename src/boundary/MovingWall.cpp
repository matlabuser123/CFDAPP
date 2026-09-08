#include "cfd/boundary/MovingWall.hpp"

#include <cmath>

#include "cfd/core/Exception.hpp"

namespace cfd::boundary {

MovingWall::MovingWall(Vector2 velocity) : velocity_(velocity) {
  if (!std::isfinite(velocity_.x) || !std::isfinite(velocity_.y)) {
    throw InvalidArgumentError("MovingWall: velocity must be finite");
  }
}

const Vector2& MovingWall::velocity() const noexcept { return velocity_; }

BoundaryConditionType MovingWall::type() const noexcept {
  return BoundaryConditionType::MovingWall;
}
std::string_view MovingWall::name() const noexcept { return "MovingWall"; }

Vector2 MovingWall::boundaryValue(const Vector2& /*ownerValue*/, Real /*normalDistance*/,
                                  const Vector2& /*unitNormal*/) const {
  return velocity_;
}

}  // namespace cfd::boundary
