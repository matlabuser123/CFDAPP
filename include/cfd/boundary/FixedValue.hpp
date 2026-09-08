#pragma once

#include "cfd/boundary/BoundaryCondition.hpp"

namespace cfd::boundary {

// Dirichlet condition: phi_boundary = a fixed configured value,
// independent of the owner cell's value. Unit-agnostic -- the field
// determines whether this is Pa, m/s, K, etc.
class FixedValue final : public ScalarBoundaryCondition {
 public:
  explicit FixedValue(Real value);

  [[nodiscard]] Real value() const noexcept;

  [[nodiscard]] BoundaryConditionType type() const noexcept override;
  [[nodiscard]] std::string_view name() const noexcept override;

  [[nodiscard]] Real boundaryValue(Real ownerValue, Real normalDistance) const override;

 private:
  Real value_{};
};

}  // namespace cfd::boundary
