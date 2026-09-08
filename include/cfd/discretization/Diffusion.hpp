#pragma once

#include "cfd/boundary/BoundaryCondition.hpp"
#include "cfd/fields/ScalarField.hpp"
#include "cfd/mesh/Mesh.hpp"

namespace cfd::discretization {

// Finite-volume divergence of (diffusivity * grad(phi)). For constant
// diffusivity, div(Gamma*grad(phi)) = Gamma * laplacian(phi) (see
// Laplacian.hpp). Internal faces contribute
// Gamma*Af*(phiN-phiP)/dPN evaluated once, added to the owner and
// subtracted from the neighbor -- so internal contributions cancel
// exactly across the whole mesh (face-once conservation, see
// PROJECT_STRUCTURE.md).
[[nodiscard]] cfd::fields::ScalarField diffusion(
    const cfd::mesh::Mesh& mesh, const cfd::fields::ScalarField& field, Real diffusivity,
    const cfd::boundary::BoundaryConditionSet& boundaries);

}  // namespace cfd::discretization
