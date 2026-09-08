#pragma once

#include "cfd/boundary/BoundaryCondition.hpp"

namespace cfd::boundary {

// A wall with a prescribed velocity, e.g. the lid-driven cavity's moving
// top wall. Not automatically constrained to be tangential -- callers
// choose a physically sound velocity for the patch; construction only
// rejects non-finite components.
class MovingWall final : public VectorBoundaryCondition {
 public:
  explicit MovingWall(Vector2 velocity);

  [[nodiscard]] const Vector2& velocity() const noexcept;

  [[nodiscard]] BoundaryConditionType type() const noexcept override;
  [[nodiscard]] std::string_view name() const noexcept override;

  [[nodiscard]] Vector2 boundaryValue(const Vector2& ownerValue, Real normalDistance,
                                      const Vector2& unitNormal) const override;

 private:
  Vector2 velocity_{};
};

}  // namespace cfd::boundary
