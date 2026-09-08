#pragma once

#include "cfd/boundary/BoundaryCondition.hpp"

namespace cfd::boundary {

// Stationary no-slip wall: velocity = (0,0) regardless of the owner
// value. Pressure is deliberately not addressed here -- pressure
// treatment at walls belongs to the pressure equation/discretization,
// not to a generic Wall imposing an arbitrary pressure value.
class Wall final : public VectorBoundaryCondition {
 public:
  Wall() = default;

  [[nodiscard]] BoundaryConditionType type() const noexcept override;
  [[nodiscard]] std::string_view name() const noexcept override;

  [[nodiscard]] Vector2 boundaryValue(const Vector2& ownerValue, Real normalDistance,
                                      const Vector2& unitNormal) const override;
};

}  // namespace cfd::boundary
