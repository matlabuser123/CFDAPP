#include "cfd/discretization/Laplacian.hpp"

#include "cfd/discretization/Diffusion.hpp"

namespace cfd::discretization {

cfd::fields::ScalarField laplacian(const cfd::mesh::Mesh& mesh,
                                   const cfd::fields::ScalarField& field,
                                   const cfd::boundary::BoundaryConditionSet& boundaries) {
  return diffusion(mesh, field, 1.0, boundaries);
}

}  // namespace cfd::discretization
