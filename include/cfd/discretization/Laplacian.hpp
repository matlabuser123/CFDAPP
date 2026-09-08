#pragma once

#include "cfd/boundary/BoundaryCondition.hpp"
#include "cfd/fields/ScalarField.hpp"
#include "cfd/mesh/Mesh.hpp"

namespace cfd::discretization {

// laplacian(phi) = div(grad(phi)) -- exactly diffusion() with a constant
// unit diffusivity. Kept as its own named operator since "Laplacian" and
// "diffusion" mean the same thing only while Gamma is constant (see
// Diffusion.hpp); once variable diffusivity arrives they diverge.
[[nodiscard]] cfd::fields::ScalarField laplacian(
    const cfd::mesh::Mesh& mesh, const cfd::fields::ScalarField& field,
    const cfd::boundary::BoundaryConditionSet& boundaries);

}  // namespace cfd::discretization
