#pragma once

#include "cfd/boundary/BoundaryCondition.hpp"

namespace cfd::boundary {

// Perfectly insulated wall: zero heat flux (q''=0), equivalently
// dT/dn=0. Mathematically identical to HeatFlux(0.0, k) for any positive
// k (the conductivity cancels: dT/dn = -0/k = 0) and to
// FixedGradient(0.0), but implemented directly in terms of the same
// underlying Neumann machinery (T_boundary = T_owner, the zero-gradient
// case) rather than composing a HeatFlux member -- same precedent as
// Outlet, which reimplements FixedGradient(0)'s math directly instead of
// holding a FixedGradient (see Outlet.cpp). Needs no conductivity at
// all, unlike HeatFlux: zero flux divided by any positive k is always
// zero, so there is nothing for a stored conductivity to do here.
class Adiabatic final : public ScalarBoundaryCondition {
 public:
  Adiabatic() = default;

  [[nodiscard]] BoundaryConditionType type() const noexcept override;
  [[nodiscard]] std::string_view name() const noexcept override;

  // Throws InvalidArgumentError if normalDistance is non-finite or <= 0
  // (same FixedGradient::boundaryValue contract), even though the
  // returned value never depends on normalDistance's magnitude -- a
  // non-positive distance normally indicates broken mesh geometry, and
  // this class should not silently mask that the way an unchecked
  // pass-through would.
  [[nodiscard]] Real boundaryValue(Real ownerValue, Real normalDistance) const override;
};

}  // namespace cfd::boundary
