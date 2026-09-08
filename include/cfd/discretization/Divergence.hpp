#pragma once

#include "cfd/boundary/BoundaryCondition.hpp"
#include "cfd/fields/ScalarField.hpp"
#include "cfd/fields/VectorField.hpp"
#include "cfd/mesh/Mesh.hpp"

namespace cfd::discretization {

// Finite-volume Gauss divergence of a cell-centered vector field:
//   div(U)_P = (1/V_P) * sum_f (Uf . Sf_cell)
// Internal-face contributions cancel exactly across the whole mesh (each
// face's Uf is computed once and used with opposite sign by its two
// cells), leaving only the boundary flux -- the divergence theorem check
// in the tests relies on this.
[[nodiscard]] cfd::fields::ScalarField divergence(
    const cfd::mesh::Mesh& mesh, const cfd::fields::VectorField& field,
    const cfd::boundary::BoundaryConditionSet& boundaries);

}  // namespace cfd::discretization
