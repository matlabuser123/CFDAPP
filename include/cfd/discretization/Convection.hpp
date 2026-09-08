#pragma once

#include "cfd/boundary/BoundaryCondition.hpp"
#include "cfd/fields/ScalarField.hpp"
#include "cfd/fields/SurfaceField.hpp"
#include "cfd/mesh/Mesh.hpp"

namespace cfd::discretization {

// First-order upwind face value for an internal face, given the face's
// mass flux Ff oriented along the stored area vector Sf (owner ->
// neighbor). Ff >= 0 -> owner value (flow along Sf); Ff < 0 -> neighbor
// value. Ff == 0 deterministically resolves to the owner value.
[[nodiscard]] Real upwindInternalFaceValue(const cfd::mesh::Face& face,
                                           const cfd::fields::ScalarField& field, Real faceFlux);

// First-order upwind face value for a boundary face. Sf points outward,
// so Ff >= 0 is outflow (carries the owner/interior value) and Ff < 0 is
// inflow (carries the prescribed boundary value).
[[nodiscard]] Real upwindBoundaryFaceValue(const cfd::mesh::Mesh& mesh, const cfd::mesh::Face& face,
                                           const cfd::fields::ScalarField& field, Real faceFlux,
                                           const cfd::boundary::ScalarBoundaryCondition& bc);

// Conservative first-order upwind convection operator:
//   conv(phi)_P = (1/V_P) * sum_f Ff * phi_upwind,f
// Each face's Ff*phi_upwind is evaluated once (from the owner's
// orientation) and applied with opposite sign to its two adjacent cells,
// so internal contributions cancel exactly across the whole mesh.
[[nodiscard]] cfd::fields::ScalarField convection(
    const cfd::mesh::Mesh& mesh, const cfd::fields::ScalarField& field,
    const cfd::fields::SurfaceField& faceMassFlux,
    const cfd::boundary::BoundaryConditionSet& boundaries);

}  // namespace cfd::discretization
