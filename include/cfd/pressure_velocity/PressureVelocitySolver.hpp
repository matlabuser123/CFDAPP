#pragma once

#include "cfd/boundary/BoundaryCondition.hpp"
#include "cfd/fields/ScalarField.hpp"
#include "cfd/fields/VectorField.hpp"
#include "cfd/mesh/Mesh.hpp"
#include "cfd/physics/FluidProperties.hpp"
#include "cfd/pressure_velocity/SIMPLEResult.hpp"

namespace cfd::pressure_velocity {

// Common interface for pressure-velocity coupling algorithms (SIMPLE
// now; SIMPLEC/SIMPLER/PISO/PIMPLE later share this same shape --
// TODO.md P0 -- SIMPLE section 3). Deliberately independent of CLI/GUI.
class PressureVelocitySolver {
 public:
  virtual ~PressureVelocitySolver() = default;

  [[nodiscard]] virtual SIMPLEResult solve(
      const cfd::mesh::Mesh& mesh, const cfd::physics::FluidProperties& fluid,
      const cfd::boundary::BoundaryConditionSet& velocityBoundaries,
      const cfd::boundary::BoundaryConditionSet& pressureBoundaries,
      cfd::fields::VectorField initialVelocity, cfd::fields::ScalarField initialPressure) const = 0;
};

}  // namespace cfd::pressure_velocity
