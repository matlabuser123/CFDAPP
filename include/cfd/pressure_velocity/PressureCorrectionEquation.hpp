#pragma once

#include "cfd/algebra/LinearSystem.hpp"
#include "cfd/algebra/Vector.hpp"
#include "cfd/boundary/BoundaryCondition.hpp"
#include "cfd/core/Types.hpp"
#include "cfd/core/Vector2.hpp"
#include "cfd/discretization/Gradient.hpp"
#include "cfd/fields/ScalarField.hpp"
#include "cfd/fields/SurfaceField.hpp"
#include "cfd/fields/VectorField.hpp"
#include "cfd/mesh/Face.hpp"
#include "cfd/mesh/Mesh.hpp"

namespace cfd::pressure_velocity {

// Boundary treatment for the pressure-correction equation depends on
// each patch's *pressure* boundary condition type:
//
//  - Neumann-like (FixedGradient, or any non-FixedValue condition --
//    Wall/MovingWall/Inlet/Outlet velocity patches always pair with a
//    Neumann pressure condition in this codebase): ZERO pressure-
//    correction coupling, the predictor flux at that boundary face is
//    left exactly as computed, uncorrected. Standard and correct for
//    walls (impermeable, F_f=0 always) and prescribed-velocity inlets
//    (F_f already fixed).
//  - Dirichlet (FixedValue, a genuinely fixed-pressure boundary such as
//    a channel outlet): real coupling, as if p'=0 at that boundary --
//    the true pressure there is already fixed by the BC, so no further
//    correction is needed at the boundary itself, but the interior cell
//    it borders gets a real D_f*(p'_P - 0) term, exactly like an
//    internal face whose "neighbor" pressure correction is pinned to
//    zero. This is what gives an open (inlet/outlet) domain a degree of
//    freedom to absorb a global mass-flow mismatch -- a fully-Neumann
//    system has none (a classical compatibility condition: solvable
//    only when net source already matches net boundary flux exactly).
//
// A fully closed domain (every boundary Wall/MovingWall, e.g. the
// mandatory P0 gate target, lid-driven cavity) has no FixedValue
// pressure patch, so this reduces to the original zero-everywhere
// treatment and `referenceCell` is still forced to p'=0 to remove the
// resulting null space -- byte-identical to this function's behavior
// before open boundaries were supported. Once at least one FixedValue
// pressure patch exists, it alone removes the null space physically, so
// `referenceCell` forcing is skipped entirely (forcing it too would
// over-constrain an already well-posed system to two independent, and
// generally inconsistent, pins).
//
// Not attempted: full Rhie-Chow face-flux coupling (see MassFlux.hpp) or
// a convective (zero-gradient, non-reflecting) outlet -- both remain
// deferred past this gate (TODO.md section 65: prove convergence/
// continuity/determinism first, physical accuracy afterward). Only a
// prescribed-pressure (Dirichlet) outlet is supported.

// d_P = V_P / aP, the momentum-response coefficient from TODO.md section
// 13/18 -- how much a cell's velocity component responds to a unit
// pressure-correction gradient. `momentumDiagonal` is a MomentumAssembly
// diagonal (already positive, finite, and nonzero per
// MomentumEquation.hpp's own guarantees).
[[nodiscard]] cfd::fields::ScalarField computeMomentumResponseCoefficient(
    const cfd::mesh::Mesh& mesh, const cfd::algebra::Vector& momentumDiagonal);

struct PressureCorrectionAssembly {
  cfd::algebra::LinearSystem system;
  // D_f per face: 0 for every non-Dirichlet boundary face (see the
  // boundary-treatment comment above), the geometric coupling coefficient
  // (pressureCorrectionFaceCoupling below) otherwise -- exposed so
  // correctFaceMassFlux can reuse the *exact* coefficient the matrix was
  // built with (TODO.md section 31's consistency invariant).
  cfd::fields::SurfaceField faceCoefficient;
  // P12-NUM-003: the explicit (non-orthogonal) part of each face's
  // pressure-correction flux, owner-oriented: -rho_f * T_f . grad(p'_prev)_f
  // (see PressureCorrectionOptions). Already accounted for in system's RHS;
  // correctFaceMassFlux must add the SAME values so the corrected flux
  // satisfies the discrete continuity equation the matrix encodes. Exactly
  // 0 on every face unless a previous pass's p' was supplied.
  cfd::fields::SurfaceField explicitFaceFlux;
};

// P12-NUM-003 -- geometric pressure-correction face coupling (the ONE
// authoritative formula; the incompressible and compressible equations
// both use it through assembleGeometricPressureCorrection below).
//
// Derivation: SIMPLE's velocity correction is u' = -D grad(p') with the
// (anisotropic) momentum response D = diag(d_u, d_v), d = V/aP
// (computeMomentumResponseCoefficient). The face mass-flux correction is
// therefore F'_f = -rho_f (D_f grad p'_f) . Sf = -rho_f grad p'_f . S_D with
// the response vector S_D = D_f Sf = (d_u,f Sf.x, d_v,f Sf.y) (d_u,f, d_v,f
// distance-weighted to the face). S_D is split against d = x_N - x_P by
// the SAME over-relaxed decomposition the diffusion correction uses
// (MeshGeometry::decomposeAreaVector; Moukalled et al., ch. 15):
//   S_D = E + T,  E = (S_D.S_D / d.S_D) d
//   F'_f = coefficient * (p'_P - p'_N) - rho_f T . grad(p')_f,
//   coefficient = rho_f |E| / |d|  (> 0: E points along d)
// With `nonOrthogonal` false the coupling is the two-point one,
// coefficient = rho_f |S_D| / |d|, T = 0 (the "uncorrected" analogue of
// the diffusion operator's |Sf|/|d|). On an axis-aligned face whose d is
// exactly parallel to Sf (every face of createCartesian2D) S_D = d_comp Sf
// exactly, T = 0, and the coefficient is evaluated as
// rho_f*|Sf|*d_comp/|d| -- the pre-P12-NUM-003 expression in its original
// operand order, so Cartesian results are bit-identical.
// A boundary face (only called for Dirichlet-pressure faces) uses the
// owner's d_u, d_v and d = x_face - x_owner. A degenerate split (d . S_D not
// safely positive) falls back to the two-point coupling for that face; a
// coupling that is non-finite (e.g. |d| = 0) throws NumericalError, the
// category SIMPLE/CompressibleSIMPLE report as NonFiniteState.
//
// P12-MESH-006, 3D: D = diag(d_u, d_v, d_w) and S_D = (d_u Sx, d_v Sy, d_w Sz);
// on an axis-aligned face d_comp is the response of the face's NORMAL
// component (d_u for an x-face, d_v for a y-face, d_w for a z-face).
// `wResponseCoefficient` must be given for a 3D mesh (InvalidArgumentError
// otherwise) and is not used for a 2D one, whose results are unchanged.
struct PressureFaceCoupling {
  Real coefficient{};
  // T scaled by rho_f: the explicit flux is -dot(nonOrthogonal, grad p'_f).
  Vector2 nonOrthogonal{0.0, 0.0};
};
[[nodiscard]] PressureFaceCoupling pressureCorrectionFaceCoupling(
    const cfd::mesh::Mesh& mesh, const cfd::mesh::Face& face, Real faceDensity,
    const cfd::fields::ScalarField& uResponseCoefficient,
    const cfd::fields::ScalarField& vResponseCoefficient, bool nonOrthogonal,
    const cfd::fields::ScalarField* wResponseCoefficient = nullptr);

// P12-NUM-003: controls the non-orthogonal part of the pressure-correction
// equation. Default: two-point coupling, no explicit term (on Cartesian
// meshes identical to the pre-P12-NUM-003 equation).
struct PressureCorrectionOptions {
  // Over-relaxed implicit coefficient, and (with previousPressureCorrection)
  // the explicit -rho_f T . grad(p') term.
  bool nonOrthogonal{false};
  // Gradient of the previous p' for the explicit term (same p' boundary
  // conditions as correctVelocity: 0 at FixedValue pressure patches,
  // zero-gradient elsewhere).
  cfd::discretization::GradientScheme gradientScheme{
      cfd::discretization::GradientScheme::GreenGauss};
  // p' from the previous non-orthogonal correction pass, or null for the
  // first pass (in the incremental p' formulation p' starts every outer
  // iteration at zero, so pass 1 has no explicit term -- see SIMPLE.cpp).
  const cfd::fields::ScalarField* previousPressureCorrection{nullptr};
};

// P12-NUM-003: the shared pressure-correction assembly (incompressible
// assemblePressureCorrection passes a uniform faceDensity and no extra
// diagonal; the compressible equation passes its per-face density and its
// V/dt * d(rho)/dp compressibility diagonal). Assembles
//   sum_f s_f [coefficient_f (p'_P - p'_N)] = -R*_P - sum_f s_f explicitFaceFlux_f
// (s_f = +1 for the owner, -1 for the neighbor), with the boundary
// treatment, reference-cell pinning and argument validation of
// assemblePressureCorrection (header comment above). `additionalDiagonal`
// (nullable, per cell) is added after the face terms, skipping a pinned
// reference cell.
[[nodiscard]] PressureCorrectionAssembly assembleGeometricPressureCorrection(
    const cfd::mesh::Mesh& mesh, const cfd::fields::SurfaceField& predictorMassFlux,
    const cfd::fields::SurfaceField& faceDensity,
    const cfd::fields::ScalarField& uResponseCoefficient,
    const cfd::fields::ScalarField& vResponseCoefficient, Index referenceCell,
    const cfd::boundary::BoundaryConditionSet& pressureBoundaries,
    const PressureCorrectionOptions& options, const cfd::fields::ScalarField* additionalDiagonal,
    const cfd::fields::ScalarField* wResponseCoefficient = nullptr);

// Assembles sum_f F_f' = -R_P* (TODO.md section 19/21), where R_P* is
// cell P's predictor mass imbalance (from evaluateContinuity on
// predictorMassFlux) and F_f' = D_f * (p'_P - p'_N) for an internal
// face. P12-NUM-003: D_f is the geometric coupling of
// pressureCorrectionFaceCoupling above (on an axis-aligned Cartesian face
// exactly the original rho*Af*d_f/dPN with the u-response coefficient for
// an x-normal face and the v-response one for a y-normal face) -- the
// former axis-aligned-faces-only restriction is gone. `options` (default:
// two-point coupling, no explicit term) enables the non-orthogonal
// correction -- see PressureCorrectionOptions. A FixedValue
// (Dirichlet) pressure boundary face gets the analogous F_f' = D_f *
// p'_P term (see the header comment above); every other boundary face
// gets zero coupling. If no FixedValue pressure patch exists, row
// `referenceCell` is replaced with p'[referenceCell] = 0 to remove the
// pressure null space (TODO.md section 23-24) -- its own row's normal
// face contributions are suppressed instead of added-then-overwritten,
// since SparseMatrixBuilder only ever sums entries; if a FixedValue
// patch does exist, `referenceCell` is not forced (see header comment).
//
// Throws InvalidArgumentError if predictorMassFlux/uResponseCoefficient/
// vResponseCoefficient sizes don't match the mesh, density is not
// finite and > 0, or referenceCell >= mesh.numberOfCells(). Every
// boundary patch on mesh must have a condition assigned in
// pressureBoundaries (same precondition as the rest of this codebase's
// BoundaryConditionSet usage).
[[nodiscard]] PressureCorrectionAssembly assemblePressureCorrection(
    const cfd::mesh::Mesh& mesh, const cfd::fields::SurfaceField& predictorMassFlux,
    const cfd::fields::ScalarField& uResponseCoefficient,
    const cfd::fields::ScalarField& vResponseCoefficient, Real density, Index referenceCell,
    const cfd::boundary::BoundaryConditionSet& pressureBoundaries,
    const PressureCorrectionOptions& options = {},
    const cfd::fields::ScalarField* wResponseCoefficient = nullptr);

// F_f = F_f* + F_f', using the SAME faceCoefficient the pressure-
// correction matrix was assembled with (never interpolate(correctedU) --
// TODO.md section 30). At a boundary face this is F_f* + faceCoefficient
// * p'_owner (the F_f' = D_f*(p'_P - 0) term for a FixedValue pressure
// patch); faceCoefficient is exactly 0 for every other boundary face, so
// this is a no-op there, matching the original zero-coupling treatment.
// P12-NUM-003: `explicitFaceFlux` (nullable) is the assembly's own
// PressureCorrectionAssembly::explicitFaceFlux -- when given, it is added
// face by face, so the corrected flux includes exactly the explicit
// non-orthogonal part the pressure-correction RHS assumed (the collocated
// coupling's single representation: matrix, flux update and continuity
// all use the same face terms).
[[nodiscard]] cfd::fields::SurfaceField correctFaceMassFlux(
    const cfd::mesh::Mesh& mesh, const cfd::fields::SurfaceField& predictorMassFlux,
    const cfd::fields::SurfaceField& faceCoefficient,
    const cfd::fields::ScalarField& pressureCorrection,
    const cfd::fields::SurfaceField* explicitFaceFlux = nullptr);

// u_P = u*_P - d_u,P * (dp'/dx)_P, v_P = v*_P - d_v,P * (dp'/dy)_P
// (TODO.md section 18) -- and, P12-MESH-006, w_P = w*_P - d_w,P (dp'/dz)_P
// with `wResponseCoefficient` (required on a 3D mesh, absent in 2D, whose
// result is unchanged) -- using the gradient of the pressure-correction
// field. Boundary condition for that gradient, per patch: FixedValue(0.0)
// (p'=0) where pressureBoundaries has a FixedValue pressure condition,
// matching assemblePressureCorrection's treatment there; FixedGradient(0.0)
// everywhere else, consistent with this file's zero-coupling boundary
// treatment (no correction crosses a boundary that had zero coupling in
// the matrix either). `scheme` (default GreenGauss, exactly today's only
// behavior for every pre-P12-NUM-002 call site, which never passes one)
// selects cfd::discretization::gradient's own reconstruction.
[[nodiscard]] cfd::fields::VectorField correctVelocity(
    const cfd::mesh::Mesh& mesh, const cfd::fields::VectorField& predictorVelocity,
    const cfd::fields::ScalarField& uResponseCoefficient,
    const cfd::fields::ScalarField& vResponseCoefficient,
    const cfd::fields::ScalarField& pressureCorrection,
    const cfd::boundary::BoundaryConditionSet& pressureBoundaries,
    cfd::discretization::GradientScheme scheme = cfd::discretization::GradientScheme::GreenGauss,
    const cfd::fields::ScalarField* wResponseCoefficient = nullptr);

}  // namespace cfd::pressure_velocity
