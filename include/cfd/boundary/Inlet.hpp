#pragma once

#include "cfd/boundary/BoundaryCondition.hpp"

namespace cfd::boundary {

// Prescribed-velocity inlet. Construction only rejects non-finite
// components -- whether the prescribed velocity actually points into the
// domain (U . Sf < 0, since the boundary area vector points outward) is
// a useful diagnostic once associated with a patch, not a hard
// constructor error here (the BC does not yet know the patch normal).
class Inlet final : public VectorBoundaryCondition {
 public:
  explicit Inlet(Vector2 velocity);

  [[nodiscard]] const Vector2& velocity() const noexcept;

  [[nodiscard]] BoundaryConditionType type() const noexcept override;
  [[nodiscard]] std::string_view name() const noexcept override;

  [[nodiscard]] Vector2 boundaryValue(const Vector2& ownerValue, Real normalDistance,
                                      const Vector2& unitNormal) const override;

 private:
  Vector2 velocity_{};
};

}  // namespace cfd::boundary
