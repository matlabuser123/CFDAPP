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
// inflow. Inflow does NOT simply return the boundary condition's value:
// every other upwind face (interior, or outflow-boundary) feeds the
// scheme a value one full owner-to-neighbor spacing away, but the raw
// boundary value is known at zero offset (right at the face) -- using it
// directly breaks that pattern and leaves an O(1) truncation error at
// inflow-boundary-adjacent cells that does not shrink under refinement.
// Instead this mirrors the owner value through the exactly-known
// boundary value to produce a "ghost" value the same distance past the
// boundary as the owner cell is on this side (boundaryValue = (owner +
// ghost) / 2, so ghost = 2*boundaryValue - owner), restoring the same
// full-spacing offset every other upwind face already has. See
// Convection.cpp for the full derivation and
// GridRefinementTest.UpwindConvectionConvergesAtFirstOrder.
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
