#pragma once

#include "cfd/boundary/BoundaryCondition.hpp"
#include "cfd/fields/ScalarField.hpp"
#include "cfd/fields/VectorField.hpp"
#include "cfd/mesh/Mesh.hpp"

namespace cfd::discretization {

// Finite-volume Gauss gradient at cell centers:
//   grad(phi)_P = (1/V_P) * sum_f phi_f * Sf_cell
// where Sf_cell is the face area vector oriented outward from the
// current cell (Sf for the owner, -Sf for the neighbor -- see
// PROJECT_STRUCTURE.md's owner/neighbor convention). Face values come
// from Interpolation (linear interior, boundary-condition-derived at
// boundaries).
[[nodiscard]] cfd::fields::VectorField gradient(
    const cfd::mesh::Mesh& mesh, const cfd::fields::ScalarField& field,
    const cfd::boundary::BoundaryConditionSet& boundaries);

}  // namespace cfd::discretization
