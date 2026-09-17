#pragma once

#include "cfd/algebra/LinearSystem.hpp"
#include "cfd/algebra/SparseMatrix.hpp"
#include "cfd/algebra/Vector.hpp"
#include "cfd/boundary/BoundaryCondition.hpp"
#include "cfd/discretization/Convection.hpp"
#include "cfd/discretization/Gradient.hpp"
#include "cfd/fields/ScalarField.hpp"
#include "cfd/fields/SurfaceField.hpp"
#include "cfd/fields/VectorField.hpp"
#include "cfd/mesh/Mesh.hpp"
#include "cfd/physics/BoussinesqBuoyancy.hpp"
#include "cfd/physics/FluidProperties.hpp"

namespace cfd::physics {

// u and v are assembled as separate scalar systems (Au u = bu, Av v =
// bv), not one coupled 2N x 2N block system -- see TODO.md P0 --
// Incompressible Physics section 36. This selects which scalar component
// a contribution assembler is building for; velocity boundary conditions
// are inherently vector-valued (a Wall gives one (0,0), not two
// independent scalar conditions), so callers pass the full vector
// BoundaryConditionSet and this says which component to extract.
//
// P12-MESH-006: W is the third component. Every contribution assembler below
// is dimension-independent (face loops, 3D area vectors, 3D gradients), so the
// same code assembles U, V on a 2D mesh and U, V, W on a 3D mesh -- W is not a
// special-case equation. On a 2D mesh only U and V are assembled, exactly as
// before. (assembleMomentum, the two-component convenience wrapper, remains
// 2D-only.)
enum class VelocityComponent { U, V, W };

// The component of `v` that `component` selects: x for U, y for V, z for W.
[[nodiscard]] inline Real velocityComponentValue(const Vector2& v,
                                                 VelocityComponent component) noexcept {
  switch (component) {
    case VelocityComponent::U:
      return v.x;
    case VelocityComponent::V:
      return v.y;
    case VelocityComponent::W:
      return v.z;
  }
  return v.x;
}

// Result of assembling one scalar momentum component. The diagonal
// (aP per cell) is exposed directly rather than requiring callers to
// rescan the matrix -- SIMPLE will need 1/aP-like quantities for
// pressure correction (TODO.md section 39).
struct MomentumAssembly {
  cfd::algebra::LinearSystem system;
  cfd::algebra::Vector diagonal;
};

struct MomentumSystems {
  MomentumAssembly u;
  MomentumAssembly v;
};

// Each assembleXxxContribution below adds its term directly into a
// shared, caller-owned SparseMatrixBuilder + RHS Vector rather than
// returning its own standalone system -- this keeps convection,
// diffusion, and the pressure source independently testable (TODO.md
// section 9/31-32: contribution-level tests, without needing to disable
// a term by passing invalid physics like mu=0) while still letting
// assembleMomentum combine them into one equation. `velocity` is the
// current/lagged velocity iterate, needed only to evaluate boundary
// conditions whose value depends on the owner cell's state (Outlet,
// Symmetry) -- Wall/MovingWall/Inlet ignore it. This mirrors how the
// discretization layer's interpolateFace/diffusion/convection already
// evaluate boundary conditions against a current field value.

// mu * Af / d diffusion contribution, symmetric internal-face
// contribution (equal/opposite to owner and neighbor rows -- TODO.md
// section 10/45), Dirichlet-style boundary contribution added to the RHS.
//
// P12-NUM-003: `applyNonOrthogonalCorrection` (default false, exactly
// today's only behavior for every pre-P12-NUM-003 call site) replaces
// each internal face's implicit coefficient basis `face.area()` with
// `|S_orth|` (MeshGeometry::decomposeFaceArea's over-relaxed orthogonal
// part -- identical to `face.area()` on an orthogonal face, see that
// function's own header comment) and adds the corresponding explicit
// deferred correction `mu * S_nonorth . grad(component)_f` to the RHS,
// face-once and equal/opposite between the owner and neighbor rows (same
// sign derivation as this file's own boundary-flux comment: the
// assembled row represents -[physical flux into the owner cell], so a
// known correction term moves to the RHS with a flipped sign). The
// component gradient is reconstructed via
// cfd::discretization::computeVelocityGradient (VectorGradient.hpp) with
// `correctionGradientScheme` -- reused directly, not a second gradient
// implementation (velocity boundary conditions are vector-valued, so
// Gradient.hpp's scalar gradient() does not apply; computeVelocityGradient
// is its vector-BC counterpart, and its LeastSquares option shares
// Gradient.hpp's own solveLeastSquaresGradient primitive). Production
// (RelaxedMomentum/SIMPLE) passes the case's `gradient_scheme`. Measured on
// a distorted mesh (linear velocity): LeastSquares makes the corrected
// operator exact to round-off; the skewness-corrected GreenGauss to
// ~3e-13 (plain GreenGauss: 1.3e-4) -- see results/p12-num-003/summary.md.
// A face whose geometry is too
// degenerate for the decomposition (`valid = false`) falls back to the
// plain `face.area()`-based coefficient for that one face, never NaN/Inf.
// Boundary faces: a Dirichlet-type velocity face (Wall/MovingWall/Inlet)
// gets the same split against d = x_face - x_owner (MeshGeometry::
// decomposeBoundaryFaceArea): coefficient mu*|S_orth,b|/|d| and explicit
// mu * S_nonorth,b . grad(component)_P on the RHS; Outlet/Symmetry faces
// are never corrected -- same policy, and the same reason (a half-
// corrected boundary cell does not converge), as
// cfd::discretization::diffusion. Every decomposition is exactly
// {Sf, 0} on an orthogonal mesh, so the assembled system is then
// bit-identical to the uncorrected one.
//
// Throws InvalidArgumentError if velocity.size() != mesh.numberOfCells().
void assembleDiffusionContribution(const cfd::mesh::Mesh& mesh, Real dynamicViscosity,
                                   const cfd::fields::VectorField& velocity,
                                   const cfd::boundary::BoundaryConditionSet& velocityBoundaries,
                                   VelocityComponent component,
                                   cfd::algebra::SparseMatrixBuilder& builder,
                                   cfd::algebra::Vector& rhs,
                                   bool applyNonOrthogonalCorrection = false,
                                   cfd::discretization::GradientScheme correctionGradientScheme =
                                       cfd::discretization::GradientScheme::GreenGauss);

// P2-TURB-003: same physics as the constant-viscosity overload above, but
// with a per-cell effective viscosity field (mu_eff = mu + mu_t, from
// cfd::turbulence::TurbulenceModel::effectiveViscosity()) instead of one
// scalar -- this is the site that generalizes to support a turbulence
// model without any RANS-specific formula living here. Internal faces use
// the project's already-established distance-weighted linear face
// interpolation (cfd::discretization::interpolateInternalFace) to get
// mu_eff at the face, the same convention every other cell-centered field
// is already interpolated to a face with -- no new averaging scheme is
// invented for this. A boundary face has no neighbor cell to interpolate
// against, so it uses the owner cell's own effective viscosity directly,
// matching how the scalar overload already applies one value uniformly
// including at boundaries.
//
// The scalar overload above is intentionally left as a separate,
// untouched code path (not reimplemented in terms of this one) -- callers
// that only ever pass a single constant viscosity keep their existing,
// exact floating-point behavior. `applyNonOrthogonalCorrection` -- see the
// scalar overload's own header comment above for the full formulation;
// identical here, just using the per-cell effective viscosity at the face
// (already the pre-existing muFace interpolation this overload uses for
// its orthogonal coefficient too). `correctionVelocity` (default null ->
// `velocity` itself) optionally supplies a DIFFERENT field to reconstruct
// the explicit correction's component gradient from -- used only by
// SIMPLE's own N-pass non-orthogonal correction loop (passes 2..N
// re-evaluate the correction from the latest predictor velocity while
// every other lagged term, e.g. boundary values, still uses `velocity`;
// see SIMPLE.cpp). Ignored when applyNonOrthogonalCorrection is false.
// Throws InvalidArgumentError if velocity.size(), effectiveViscosity.size()
// or (when given) correctionVelocity->size() != mesh.numberOfCells().
void assembleDiffusionContribution(const cfd::mesh::Mesh& mesh,
                                   const cfd::fields::ScalarField& effectiveViscosity,
                                   const cfd::fields::VectorField& velocity,
                                   const cfd::boundary::BoundaryConditionSet& velocityBoundaries,
                                   VelocityComponent component,
                                   cfd::algebra::SparseMatrixBuilder& builder,
                                   cfd::algebra::Vector& rhs,
                                   bool applyNonOrthogonalCorrection = false,
                                   cfd::discretization::GradientScheme correctionGradientScheme =
                                       cfd::discretization::GradientScheme::GreenGauss,
                                   const cfd::fields::VectorField* correctionVelocity = nullptr);

// Convection contribution using the already-computed face mass flux (see
// MassFlux.hpp) -- TODO.md section 11/25/53. `scheme` (default Upwind,
// exactly today's behavior for every pre-P12-NUM-001 call site, which
// never passes one) selects the INTERNAL-face treatment -- see
// cfd::discretization::ConvectionScheme's own header comment. The
// implicit matrix coefficients are always the first-order upwind ones
// regardless of scheme (deferred correction, P12-NUM-001 requirement 7):
// a higher-order scheme instead adds an EXPLICIT correction
// (highOrderFace - upwindFace, boundedness-limited, evaluated from the
// current/lagged `velocity`) to the RHS, face-once and equal/opposite
// between the owner and neighbor rows, the same convention every other
// contribution in this file already uses. Boundary faces are completely
// unaffected by `scheme` (always the pre-existing upwind treatment) --
// see results/p12-num-001/summary.md for why. Throws
// InvalidArgumentError if velocity.size() != mesh.numberOfCells() or
// massFlux.size() != mesh.numberOfFaces().
void assembleConvectionContribution(
    const cfd::mesh::Mesh& mesh, const cfd::fields::SurfaceField& massFlux,
    const cfd::fields::VectorField& velocity,
    const cfd::boundary::BoundaryConditionSet& velocityBoundaries, VelocityComponent component,
    cfd::algebra::SparseMatrixBuilder& builder, cfd::algebra::Vector& rhs,
    cfd::discretization::ConvectionScheme scheme = cfd::discretization::ConvectionScheme::Upwind);

// -V_P * (dp/dx or dp/dy)_P, using the verified gradient operator
// (TODO.md section 26-29). Purely a source: adds to rhs only, no matrix
// contribution (pressure is not an unknown of this equation). `scheme`
// (default GreenGauss, exactly today's only behavior for every
// pre-P12-NUM-002 call site, which never passes one) selects
// cfd::discretization::gradient's own reconstruction -- see that
// function's header comment. Throws InvalidArgumentError if
// pressure.size() != mesh.numberOfCells().
void assemblePressureSourceContribution(
    const cfd::mesh::Mesh& mesh, const cfd::fields::ScalarField& pressure,
    const cfd::boundary::BoundaryConditionSet& pressureBoundaries, VelocityComponent component,
    cfd::algebra::Vector& rhs,
    cfd::discretization::GradientScheme scheme = cfd::discretization::GradientScheme::GreenGauss);

// P3-PHYS-001: Boussinesq buoyancy body-force contribution -- a pure
// source, same shape as assemblePressureSourceContribution above (adds
// to rhs only, no matrix/diagonal contribution -- temperature is not an
// unknown of this equation). For each cell, `buoyancy.source(T)` (force
// per unit volume -- see BoussinesqBuoyancy.hpp's own header comment for
// the full derivation and sign convention) is integrated over the cell's
// volume, the same "per-volume source times cellVolume" convention
// assemblePressureSourceContribution already establishes for
// -V_P*grad(p) (verified directly from that existing contribution's own
// implementation, not assumed -- P3-PHYS-001 Phase 3's own explicit
// instruction). Throws InvalidArgumentError if temperature.size() !=
// mesh.numberOfCells().
void assembleBuoyancySourceContribution(const cfd::mesh::Mesh& mesh,
                                        const cfd::fields::ScalarField& temperature,
                                        const BoussinesqBuoyancy& buoyancy,
                                        VelocityComponent component, cfd::algebra::Vector& rhs);

// P12-NUM-006: a generic, prescribed volumetric momentum source (a body
// force per unit volume, one Vector2 per cell -- e.g. a driving pressure
// gradient, or a manufactured-solution forcing term). Pure source, same
// "per-volume source times cellVolume" convention as the pressure and
// buoyancy contributions above: rhs[P] += V_P * f_P (component). The value
// is the source at the cell centroid (midpoint-rule volume integral, second
// order); the caller owns what the field means -- nothing here is specific
// to any physics or to verification. Throws InvalidArgumentError if
// sourcePerUnitVolume.size() != mesh.numberOfCells() or any value is
// non-finite.
void assembleMomentumSourceContribution(const cfd::mesh::Mesh& mesh,
                                        const cfd::fields::VectorField& sourcePerUnitVolume,
                                        VelocityComponent component, cfd::algebra::Vector& rhs);

// Combines the three contributions above into the full u- and v-momentum
// systems for steady, incompressible, constant-property, laminar flow:
//   rho (U.grad)u - mu grad^2 u = -dp/dx
//   rho (U.grad)v - mu grad^2 v = -dp/dy
// massFlux must already carry rho (see calculateMassFlux) -- fluid is
// used here only for dynamicViscosity. No SIMPLE, no under-relaxation,
// no pressure-correction equation -- see TODO.md section 40-41.
//
// Throws InvalidArgumentError if velocity/pressure size does not match
// mesh.numberOfCells(), or massFlux size does not match
// mesh.numberOfFaces(). Throws NumericalError if the assembled system
// (matrix or RHS) contains a non-finite value.
[[nodiscard]] MomentumSystems assembleMomentum(
    const cfd::mesh::Mesh& mesh, const cfd::fields::VectorField& velocity,
    const cfd::fields::ScalarField& pressure, const cfd::fields::SurfaceField& massFlux,
    const FluidProperties& fluid, const cfd::boundary::BoundaryConditionSet& velocityBoundaries,
    const cfd::boundary::BoundaryConditionSet& pressureBoundaries);

}  // namespace cfd::physics
