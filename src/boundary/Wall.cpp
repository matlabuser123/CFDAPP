#include "cfd/boundary/Wall.hpp"

namespace cfd::boundary {

BoundaryConditionType Wall::type() const noexcept { return BoundaryConditionType::Wall; }
std::string_view Wall::name() const noexcept { return "Wall"; }

Vector2 Wall::boundaryValue(const Vector2& /*ownerValue*/, Real /*normalDistance*/,
                            const Vector2& /*unitNormal*/) const {
  return Vector2{0.0, 0.0};
}

}  // namespace cfd::boundary
