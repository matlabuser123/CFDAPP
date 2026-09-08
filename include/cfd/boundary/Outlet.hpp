#pragma once

#include "cfd/boundary/BoundaryCondition.hpp"

namespace cfd::boundary {

// Zero-gradient velocity outlet: dU/dn = 0, so the boundary value equals
// the owner cell's value (mathematically FixedGradient(0), expressed
// directly for velocity so it can be assigned wherever a
// VectorBoundaryCondition is needed). A zero-gradient outlet for a
// *scalar* field (e.g. pressure) is exactly FixedGradient(0.0) -- no
// separate scalar Outlet class is needed.
class Outlet final : public VectorBoundaryCondition {
 public:
  Outlet() = default;

  [[nodiscard]] BoundaryConditionType type() const noexcept override;
  [[nodiscard]] std::string_view name() const noexcept override;

  [[nodiscard]] Vector2 boundaryValue(const Vector2& ownerValue, Real normalDistance,
                                      const Vector2& unitNormal) const override;
};

}  // namespace cfd::boundary
