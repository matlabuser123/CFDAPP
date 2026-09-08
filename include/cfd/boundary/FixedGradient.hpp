#pragma once

#include "cfd/boundary/BoundaryCondition.hpp"

namespace cfd::boundary {

// Neumann condition: dphi/dn = a fixed configured gradient. The boundary
// face value is reconstructed as phi_boundary = phi_owner + gradient *
// normalDistance. gradient = 0 is the common "zero-gradient" case
// (phi_boundary = phi_owner); Outlet/Symmetry's scalar behavior is
// exactly FixedGradient(0.0).
class FixedGradient final : public ScalarBoundaryCondition {
 public:
  explicit FixedGradient(Real gradient);

  [[nodiscard]] Real gradient() const noexcept;

  [[nodiscard]] BoundaryConditionType type() const noexcept override;
  [[nodiscard]] std::string_view name() const noexcept override;

  // Throws InvalidArgumentError if normalDistance is non-finite or <= 0
  // -- a non-positive distance normally indicates broken mesh geometry.
  [[nodiscard]] Real boundaryValue(Real ownerValue, Real normalDistance) const override;

 private:
  Real gradient_{};
};

}  // namespace cfd::boundary
