#pragma once

#include "cfd/boundary/BoundaryCondition.hpp"

namespace cfd::boundary {

// Prescribed wall/inlet temperature: T = Twall, independent of the owner
// cell's value. Mathematically identical to FixedValue (Dirichlet), but
// given its own type/name so case configuration and
// thermal::EnergyEquation can use a physically-named thermal condition
// instead of the generic scalar primitive -- same precedent as Outlet
// being a distinct, physically-named type from a bare FixedGradient(0.0)
// (see Outlet.hpp). Deliberately placed alongside every other domain-
// specific boundary condition (Wall, MovingWall, Inlet, Outlet,
// Symmetry) under cfd::boundary rather than cfd::thermal -- this
// codebase keeps its whole BoundaryCondition hierarchy, generic and
// domain-named alike, in one place (P2-THERMAL-003).
class FixedTemperature final : public ScalarBoundaryCondition {
 public:
  // Throws InvalidArgumentError if temperature is not finite.
  explicit FixedTemperature(Real temperature);

  [[nodiscard]] Real temperature() const noexcept;

  [[nodiscard]] BoundaryConditionType type() const noexcept override;
  [[nodiscard]] std::string_view name() const noexcept override;

  [[nodiscard]] Real boundaryValue(Real ownerValue, Real normalDistance) const override;

 private:
  Real temperature_{};
};

}  // namespace cfd::boundary
