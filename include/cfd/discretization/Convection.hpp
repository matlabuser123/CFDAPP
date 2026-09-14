#pragma once

#include <optional>
#include <string_view>

#include "cfd/boundary/BoundaryCondition.hpp"
#include "cfd/core/Vector2.hpp"
#include "cfd/fields/ScalarField.hpp"
#include "cfd/fields/SurfaceField.hpp"
#include "cfd/mesh/Mesh.hpp"

namespace cfd::discretization {

// P12-NUM-001: which face-interpolation scheme convection() (and, in the
// production path, cfd::physics::assembleConvectionContribution and any
// caller that opts in) uses for INTERNAL faces. Boundary faces always use
// upwindBoundaryFaceValue's own ghost-value treatment regardless of this
// choice -- see this file's own module comment further down for why
// (reproducing each scheme's boundary-consistent treatment is a separate,
// harder problem, explicitly out of scope here; see
// results/p12-num-001/summary.md).
enum class ConvectionScheme {
  Upwind,
  Central,
  LinearUpwind,
  QUICK,
};

// Parses one of "upwind" / "central" / "linear_upwind" / "quick"
// (case-sensitive, matching the case-file vocabulary exactly). Throws
// InvalidArgumentError for anything else -- this is the one place a
// case-file/GUI string becomes a ConvectionScheme, so an invalid value
// fails loudly here rather than silently defaulting.
[[nodiscard]] ConvectionScheme parseConvectionScheme(std::string_view name);

// Inverse of parseConvectionScheme -- for diagnostics/round-tripping.
[[nodiscard]] std::string_view convectionSchemeName(ConvectionScheme scheme) noexcept;

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

// P12-NUM-001 -- Higher-order convection, internal-face primitives.
//
// These are pure, physics-agnostic building blocks -- they take already-
// computed values/gradients/distances, never a Mesh or a Field, so any
// physics module (momentum, thermal, species, ...) can reuse them without
// depending on this module's own scalar BoundaryConditionSet-based
// gradient/interpolation machinery. Each is deliberately as small as its
// own unit tests (test_convection.cpp) can verify directly.

// General 3-point upstream-biased (QUICK-family, Leonard 1979) quadratic
// face interpolation: the unique quadratic through (phiC, phiU, phiD) at
// their true positions, evaluated at the face. hCU is the distance from
// the far-upstream cell C to the upstream cell U; hUf is the distance
// from U to the face; hfD is the distance from the face to the
// downstream cell D. All three must be finite and > 0. Reduces to the
// classical QUICK coefficients (6/8, 3/8, -1/8) when hCU == 2*hUf ==
// 2*hfD (a uniform grid, where the face sits midway between U and D and
// C is one further full cell-spacing upstream of U) -- see Convection.cpp
// for the derivation and QuickFaceValueMatchesClassicalCoefficients.
[[nodiscard]] Real quickFaceValue(Real phiC, Real hCU, Real phiU, Real hUf, Real phiD, Real hfD);

// Second-order upwind-biased face value from a linear (Taylor) expansion
// about the upwind cell: phiUpwind + gradPhiUpwind . (faceCentroid -
// upwindCentroid). `gradPhiUpwind` must already be the gradient AT the
// upwind cell -- from cfd::discretization::gradient for a plain scalar
// field, or cfd::discretization::computeVelocityGradient's gradU/gradV
// for a velocity component -- this function does not compute a gradient
// itself (P12-NUM-001 requirement: never a second gradient
// implementation living inside this file).
[[nodiscard]] Real linearUpwindFaceValue(Real phiUpwind, const Vector2& gradPhiUpwind,
                                         const Vector2& upwindCentroid,
                                         const Vector2& faceCentroid);

// P12-NUM-001 boundedness -- Sweby (1984) TVD flux-limiter framework.
//
// A first attempt at boundedness here simply clipped the raw high-order
// face value into [min(phiUpwind, phiDownwind), max(...)]. That clip is
// NOT sufficient: two independently-clipped faces of the same cell can
// still combine into an out-of-bounds cell update (confirmed by
// StepFunctionStaysWithinInitialBoundsAfterOneExplicitStep failing under
// it) -- clipping one face's value in isolation says nothing about the
// other face's contribution to the same cell. The textbook fix (Sweby
// 1984) blends toward the high-order value only as far as the LOCAL
// smoothness allows:
//   phi_face = phiUpwind + psi(r) * (phiHighOrder - phiUpwind)
// where r is the ratio of the upstream gradient to the local (across-
// this-face) gradient, and psi(r) -- the limiter function -- is 0 at any
// local extremum (r <= 0, where ANY high-order correction would create a
// new one) and smoothly blends in the correction as the data gets
// smoother/more linear (larger r), never exceeding what a proven TVD
// limiter allows.
//
// For the Central scheme specifically (whose phiHighOrder - phiUpwind
// reduces, on a uniform grid, to exactly 0.5*(phiDownwind - phiUpwind)),
// this reproduces the classical, PUBLISHED, PROVEN-TVD "limited linear/
// central" scheme (equivalent to van Leer's MUSCL). For LinearUpwind and
// QUICK -- whose own high-order correction magnitude does not generally
// match that exact classical form -- applying the same r/psi blend is a
// common, practical technique (in the spirit of NVD-based limiters like
// SMART/ULTRA-QUICK) but is NOT a formally proven discrete maximum
// principle for those two schemes; it is empirically verified instead
// (see test_convection.cpp's step-function tests and
// results/p12-num-001/summary.md, which states plainly which of the two
// kinds of guarantee applies to which scheme).

// The classical Sweby smoothness ratio: how the upstream (far-upstream-
// to-upwind) gradient compares to the local (upwind-to-downwind, across
// this face) gradient. std::nullopt when the local gradient is exactly
// zero (a locally uniform field -- no limiting question even arises,
// any face value already equals both neighbors) rather than a computed
// NaN/Inf from a division by zero.
[[nodiscard]] std::optional<Real> smoothnessRatio(Real phiFarUpstream, Real phiUpwind,
                                                  Real phiDownwind) noexcept;

// Van Leer's (1974) TVD limiter function: psi(r) = (r + |r|) / (1 + |r|)
// for r > 0 (full value at r=1, asymptoting toward 2 for large r, always
// <= the Sweby TVD region's upper bound of min(2r, 2)); 0 for r <= 0 (a
// local extremum or non-monotone upstream data -- MUST fall back to
// pure upwind there, the defining TVD requirement) or for `r` ==
// std::nullopt (no far-upstream cell available -- e.g. QUICK's own
// documented degradation case -- the same safe "degrade toward upwind"
// default this file already uses elsewhere).
[[nodiscard]] Real vanLeerLimiter(std::optional<Real> r) noexcept;

// Conservative convection operator:
//   conv(phi)_P = (1/V_P) * sum_f Ff * phi_f
// Each face's Ff*phi_f is evaluated once (from the owner's orientation)
// and applied with opposite sign to its two adjacent cells, so internal
// contributions cancel exactly across the whole mesh.
//
// `scheme` (default Upwind, exactly today's behavior -- see
// ConvectionOverloadDefaultsToUpwind in test_convection.cpp) selects
// phi_f at INTERNAL faces only: Upwind uses upwindInternalFaceValue
// unchanged; Central/LinearUpwind/QUICK compute a higher-order phi_f
// (reusing cfd::discretization::gradient/interpolateInternalFace and
// MeshGeometry::oppositeInteriorFace -- no new gradient/interpolation
// logic here), then blend it with the plain-upwind value via
// vanLeerLimiter(smoothnessRatio(...)) (see that pair's own header
// comment) rather than using it directly. Boundary faces always use
// upwindBoundaryFaceValue, regardless of scheme (P12-NUM-001 explicit
// scope limit -- see results/p12-num-001/summary.md). Any internal face
// whose upwind cell has no further-upstream interior neighbor
// (MeshGeometry::oppositeInteriorFace returns std::nullopt) degrades
// deterministically toward Upwind (QUICK: no other value to blend at
// all; Central/LinearUpwind: the limiter itself sees r=std::nullopt and
// returns 0) -- documented, not a silent approximation.
[[nodiscard]] cfd::fields::ScalarField convection(
    const cfd::mesh::Mesh& mesh, const cfd::fields::ScalarField& field,
    const cfd::fields::SurfaceField& faceMassFlux,
    const cfd::boundary::BoundaryConditionSet& boundaries,
    ConvectionScheme scheme = ConvectionScheme::Upwind);

}  // namespace cfd::discretization
