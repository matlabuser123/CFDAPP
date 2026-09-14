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

// P12-NUM-003 -- skewness-corrected internal-face interpolation:
//   phi_f = phi_f' + grad(phi)_f' . (x_f - x_f')
//   phi_f' = phi_P + t*(phi_N - phi_P),  grad_f' = grad_P + t*(grad_N - grad_P)
// where f' is the point where the owner-neighbor line crosses the face
// and t its weight (MeshGeometry::ownerNeighborCrossing). The first term
// is exact along the P-N line for a linear field; the second transports
// that value from f' to the true face centroid x_f -- so the result is
// exact for any linear field on any (non-degenerate) skewed face, which
// plain distance-weighted interpolateInternalFace above is NOT once the
// face is skewed. `gradient` is a caller-supplied reconstructed cell
// gradient of `field` (e.g. cfd::discretization::gradient, either
// GradientScheme) -- no gradient is computed here. On an unskewed face
// (skew vector exactly zero) the correction term vanishes. Falls back to
// plain interpolateInternalFace when the crossing is undefined
// (degenerate d . Sf, see MeshGeometry::decomposeFaceArea) -- never
// NaN/Inf. On an unskewed face (skew vector exactly zero, e.g. every face
// of createCartesian2D) the result IS interpolateInternalFace, bit-for-bit.
// Production use (P12-NUM-003): the Green-Gauss gradients
// (cfd::discretization::gradient / computeVelocityGradient) evaluate their
// internal-face values with it -- see Gradient.hpp. Throws
// InvalidArgumentError for a boundary face or if either field size does
// not match mesh.numberOfCells().
[[nodiscard]] Real interpolateInternalFaceSkewCorrected(const cfd::mesh::Mesh& mesh,
                                                        const cfd::mesh::Face& face,
                                                        const cfd::fields::ScalarField& field,
                                                        const cfd::fields::VectorField& gradient);

// Vector-field counterpart (each component transported with its own
// gradient: gradientX = grad(field.x), gradientY = grad(field.y)) -- the
// same formula, used by computeVelocityGradient's Green-Gauss path.
[[nodiscard]] Vector2 interpolateInternalFaceSkewCorrected(
    const cfd::mesh::Mesh& mesh, const cfd::mesh::Face& face, const cfd::fields::VectorField& field,
    const cfd::fields::VectorField& gradientX, const cfd::fields::VectorField& gradientY);

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
