#pragma once

#include "cfd/core/Types.hpp"
#include "cfd/pressure_velocity/PressureVelocitySolver.hpp"
#include "cfd/pressure_velocity/SIMPLESettings.hpp"

namespace cfd::pressure_velocity {

// Steady, incompressible, constant-property SIMPLE (TODO.md P0 --
// SIMPLE). Orchestrates the already-verified physics/algebra/
// discretization layers (momentum assembly + relaxation, pressure
// correction, velocity/flux correction) rather than reimplementing any
// of them -- see this phase's other headers for the individual pieces
// and their own derivations/invariants.
class SIMPLE final : public PressureVelocitySolver {
 public:
  // referenceCell is the pressure-correction null-space gauge (TODO.md
  // section 23-24): fixed at construction, so it never moves between
  // iterations or between repeated solves of the same problem
  // (determinism -- section 69-70).
  explicit SIMPLE(SIMPLESettings settings, Index referenceCell = 0);

  [[nodiscard]] SIMPLEResult solve(const cfd::mesh::Mesh& mesh,
                                   const cfd::physics::FluidProperties& fluid,
                                   const cfd::boundary::BoundaryConditionSet& velocityBoundaries,
                                   const cfd::boundary::BoundaryConditionSet& pressureBoundaries,
                                   cfd::fields::VectorField initialVelocity,
                                   cfd::fields::ScalarField initialPressure) const override;

  [[nodiscard]] const SIMPLESettings& settings() const noexcept;
  [[nodiscard]] Index referenceCell() const noexcept;

 private:
  SIMPLESettings settings_;
  Index referenceCell_;
};

}  // namespace cfd::pressure_velocity
