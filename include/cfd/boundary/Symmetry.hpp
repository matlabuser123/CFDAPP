#pragma once

#include "cfd/boundary/BoundaryCondition.hpp"

namespace cfd::boundary {

// Symmetry/slip plane: removes the velocity component normal to the
// boundary while preserving the tangential component --
// Usym = U - (U . n) n, where n is the boundary's outward unit normal.
// A zero-gradient scalar symmetry condition (dphi/dn = 0) is exactly
// FixedGradient(0.0) -- no separate scalar Symmetry class is needed.
class Symmetry final : public VectorBoundaryCondition {
 public:
  Symmetry() = default;

  [[nodiscard]] BoundaryConditionType type() const noexcept override;
  [[nodiscard]] std::string_view name() const noexcept override;

  // Throws InvalidArgumentError if unitNormal is not (approximately) of
  // unit length -- a real bug is far more likely than a legitimate
  // non-unit "normal" reaching this call.
  [[nodiscard]] Vector2 boundaryValue(const Vector2& ownerValue, Real normalDistance,
                                      const Vector2& unitNormal) const override;
};

}  // namespace cfd::boundary
