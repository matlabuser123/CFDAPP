#pragma once

#include <string_view>
#include <vector>

#include "cfd/boundary/BoundaryCondition.hpp"
#include "cfd/core/Vector2.hpp"
#include "cfd/fields/ScalarField.hpp"
#include "cfd/fields/VectorField.hpp"
#include "cfd/mesh/Mesh.hpp"

namespace cfd::discretization {

// P12-NUM-002: which cell-gradient reconstruction gradient() (and any
// caller that opts in) uses. GreenGauss is the default -- exactly the
// only scheme that existed before this enum, so every pre-P12-NUM-002
// caller (which never passes one) sees byte-identical behavior.
enum class GradientScheme {
  GreenGauss,
  LeastSquares,
};

// Parses "green_gauss" / "least_squares" (case-sensitive, matching the
// case-file vocabulary exactly -- mirrors
// cfd::discretization::parseConvectionScheme's own convention). Throws
// InvalidArgumentError for anything else.
[[nodiscard]] GradientScheme parseGradientScheme(std::string_view name);

// Inverse of parseGradientScheme -- for diagnostics/round-tripping.
[[nodiscard]] std::string_view gradientSchemeName(GradientScheme scheme) noexcept;

// Finite-volume Gauss gradient at cell centers:
//   grad(phi)_P = (1/V_P) * sum_f phi_f * Sf_cell
// where Sf_cell is the face area vector oriented outward from the
// current cell (Sf for the owner, -Sf for the neighbor -- see
// PROJECT_STRUCTURE.md's owner/neighbor convention). Face values come
// from Interpolation (linear interior, boundary-condition-derived at
// boundaries).
//
// `scheme` (default GreenGauss, exactly today's only behavior -- see
// GradientOverloadDefaultsToGreenGauss in test_gradient.cpp) selects the
// reconstruction: GreenGauss is this formula with P12-NUM-003's skewness-
// corrected internal-face values (greenGaussGradient below, with
// kGreenGaussSkewCorrectionSweeps) -- unchanged, bit-for-bit, on any mesh
// without skewed faces.
// LeastSquares dispatches to leastSquaresGradient() below (own header
// comment has the full formulation).
[[nodiscard]] cfd::fields::VectorField gradient(
    const cfd::mesh::Mesh& mesh, const cfd::fields::ScalarField& field,
    const cfd::boundary::BoundaryConditionSet& boundaries,
    GradientScheme scheme = GradientScheme::GreenGauss);

// P12-NUM-003 -- skewness-corrected Green-Gauss. On a skewed face the
// distance-weighted face value is the value at the point f' where the
// owner-neighbor line crosses the face, not at the face centroid the
// Green-Gauss sum needs (MeshGeometry::ownerNeighborCrossing). This
// computes the plain Green-Gauss gradient, then `skewCorrectionSweeps`
// times: re-evaluates every skewed internal face's value with
// interpolateInternalFaceSkewCorrected (phi_f' + grad_f' . (x_f - x_f'),
// grad from the previous sweep) and re-sums. A fixed-point iteration whose
// fixed point is exact for linear fields (on a mesh whose boundary values
// are exact); each sweep contracts the error by roughly the face skewness.
// Faces with an exactly-zero skew vector (every face of an orthogonal
// Cartesian mesh) are never touched, so on such a mesh the result is the
// pre-P12-NUM-003 Green-Gauss gradient bit-for-bit for any sweep count.
// Boundary faces keep their boundary-condition value (already at the face
// centroid). Throws InvalidArgumentError on a field-size mismatch.
[[nodiscard]] cfd::fields::VectorField greenGaussGradient(
    const cfd::mesh::Mesh& mesh, const cfd::fields::ScalarField& field,
    const cfd::boundary::BoundaryConditionSet& boundaries, Index skewCorrectionSweeps);

// Sweeps gradient(GreenGauss) uses. EMPIRICAL, chosen from the measured
// sweep study (results/p12-num-003/summary.md): on the distorted
// verification meshes (0.10h-0.45h, 10x10 and 20x20) each sweep contracts
// the linear-field gradient error ~100-1000x (8.8e-2 -> 4.1e-4 -> 6.3e-6 ->
// 4.8e-8 -> 1.1e-9 at 0.45h, 10x10), while the smooth-field error stops
// improving after 1-2 sweeps (truncation-dominated). 4 keeps the worst
// measured linear-field error <= ~1e-9 -- close to, but NOT, exact: only
// GradientScheme::LeastSquares is exact for linear fields on a skewed mesh.
inline constexpr Index kGreenGaussSkewCorrectionSweeps = 4;

// P12-NUM-002 -- weighted least-squares gradient reconstruction.
//
// Result of one cell's local least-squares solve: `gradient` is only
// meaningful when `wellConditioned` is true. A singular/ill-conditioned
// local system (too few independent neighbor directions -- e.g. every
// neighbor colinear with the cell center) NEVER silently returns a
// bogus value; `wellConditioned = false` (with `gradient` left as a
// harmless {0,0,0} placeholder, never NaN/Inf) signals the caller must
// use its own documented fallback -- see leastSquaresGradient's own
// policy below.
struct LeastSquaresGradientResult {
  Vector2 gradient{0.0, 0.0};
  bool wellConditioned = true;
};

// Pure primitive (no Mesh/Field dependency, independently unit-tested):
// solves the small 2x2 weighted-least-squares normal-equations system
// for the gradient g that best satisfies, for every neighbor i,
//   displacements[i] . g ~= valueDifferences[i]
// (i.e. phi_i - phi_P ~= grad(phi)_P . (r_i - r_P)). Weight w_i =
// 1/|displacements[i]|^2 (inverse-distance-squared -- a standard,
// documented choice: closer neighbors, whose finite-difference estimate
// of the local slope is more locally accurate, are trusted more). The
// normal equations are:
//   [Sxx Sxy] [gx]   [bx]      Sxx = sum w_i dx_i^2,  Sxy = sum w_i dx_i dy_i
//   [Sxy Syy] [gy] = [by]      Syy = sum w_i dy_i^2
//                              bx  = sum w_i dx_i dphi_i,  by = sum w_i dy_i dphi_i
// solved directly (Cramer's rule -- a plain 2x2 solve, no external
// linear-algebra dependency, matching this solver's current 2D scope).
// `wellConditioned` is false whenever `det = Sxx*Syy - Sxy^2` is small
// relative to `(Sxx+Syy)^2` (a scale-invariant threshold) -- the
// geometric signature of every displacement lying (near-)colinear, so
// the two-direction gradient is not well-determined by this stencil.
//
// P12-MESH-005: if any displacement has a non-zero z component (a 3D
// cell), the same weighted normal equations are solved in three
// dimensions -- the symmetric 3 x 3 system with Sxz, Syz, Szz, bz added,
// by the adjugate formula -- and `wellConditioned` is false when det(S) <
// 1e-10 (trace S)^3 (coplanar or colinear displacements). Planar
// displacements (every 2D cell) take the 2 x 2 path above unchanged.
// Throws InvalidArgumentError if the two input vectors' sizes differ.
[[nodiscard]] LeastSquaresGradientResult solveLeastSquaresGradient(
    const std::vector<Vector2>& displacements, const std::vector<Real>& valueDifferences);

// Mesh-level least-squares gradient: for each cell, gathers one
// displacement/value-difference pair per face -- from the neighbor
// cell's centroid for an internal face, or from the assigned
// ScalarBoundaryCondition evaluated exactly at the boundary face's own
// centroid for a boundary face (the same boundaryValue(ownerValue,
// distance) convention Interpolation.hpp/the GreenGauss path above
// already use) -- and solves solveLeastSquaresGradient() per cell.
//
// Fallback policy (deterministic, documented, tested): any cell whose
// local system is NOT well-conditioned uses that same cell's GreenGauss
// value instead (computed once for the whole mesh up front) -- never a
// silently-wrong or non-finite least-squares result. This codebase's
// only mesh generator (Cartesian, or the test-only distorted-quad
// generator) gives every cell contributions spanning both the x and y
// directions (every cell has exactly 2 x-facing and 2 y-facing faces,
// each either internal or a boundary "virtual neighbor"), so genuine
// degeneracy is not reachable from an actual produced mesh in this
// codebase -- confirmed by inspection and tested directly at the
// solveLeastSquaresGradient primitive level instead (see
// test_gradient.cpp and results/p12-num-002/summary.md). (P12-MESH-005:
// likewise every hexahedron of createCartesian3D has 2 faces per axis, so
// its 3 x 3 system is always well-conditioned.)
[[nodiscard]] cfd::fields::VectorField leastSquaresGradient(
    const cfd::mesh::Mesh& mesh, const cfd::fields::ScalarField& field,
    const cfd::boundary::BoundaryConditionSet& boundaries);

}  // namespace cfd::discretization
