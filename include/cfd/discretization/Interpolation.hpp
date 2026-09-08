#pragma once

#include "cfd/boundary/BoundaryCondition.hpp"
#include "cfd/fields/ScalarField.hpp"
#include "cfd/fields/SurfaceField.hpp"
#include "cfd/fields/VectorField.hpp"
#include "cfd/mesh/Mesh.hpp"

namespace cfd::discretization {

// Converts cell-centered values to face-centered values. Distance-weighted
// linear interpolation: phi_f = (dNf*phiP + dPf*phiN) / (dPf + dNf),
// which reduces to phi_f = 0.5*(phiP+phiN) on a uniform grid but stays
// correct if cell spacing is ever non-uniform.
[[nodiscard]] Real interpolateInternalFace(const cfd::mesh::Mesh& mesh, const cfd::mesh::Face& face,
                                           const cfd::fields::ScalarField& field);
[[nodiscard]] Vector2 interpolateInternalFace(const cfd::mesh::Mesh& mesh,
                                              const cfd::mesh::Face& face,
                                              const cfd::fields::VectorField& field);

// Boundary face value from the assigned boundary condition -- never just
// the owner cell's value (that would make Dirichlet boundaries wrong).
[[nodiscard]] Real interpolateBoundaryFace(const cfd::mesh::Mesh& mesh, const cfd::mesh::Face& face,
                                           const cfd::fields::ScalarField& field,
                                           const cfd::boundary::ScalarBoundaryCondition& bc);
[[nodiscard]] Vector2 interpolateBoundaryFace(const cfd::mesh::Mesh& mesh,
                                              const cfd::mesh::Face& face,
                                              const cfd::fields::VectorField& field,
                                              const cfd::boundary::VectorBoundaryCondition& bc);

// Dispatches to the internal/boundary overload above based on
// face.isBoundary(), resolving the boundary condition from `boundaries`.
// Throws InvalidArgumentError if a boundary face's assigned condition is
// not the expected (scalar/vector) kind.
[[nodiscard]] Real interpolateFace(const cfd::mesh::Mesh& mesh, const cfd::mesh::Face& face,
                                   const cfd::fields::ScalarField& field,
                                   const cfd::boundary::BoundaryConditionSet& boundaries);
[[nodiscard]] Vector2 interpolateFace(const cfd::mesh::Mesh& mesh, const cfd::mesh::Face& face,
                                      const cfd::fields::VectorField& field,
                                      const cfd::boundary::BoundaryConditionSet& boundaries);

// Whole-field convenience: interpolates every face into a SurfaceField.
[[nodiscard]] cfd::fields::SurfaceField interpolate(
    const cfd::mesh::Mesh& mesh, const cfd::fields::ScalarField& field,
    const cfd::boundary::BoundaryConditionSet& boundaries);

}  // namespace cfd::discretization
