#pragma once

#include "cfd/algebra/LinearSystem.hpp"
#include "cfd/algebra/Vector.hpp"
#include "cfd/boundary/BoundaryCondition.hpp"
#include "cfd/core/Types.hpp"
#include "cfd/fields/ScalarField.hpp"
#include "cfd/fields/SurfaceField.hpp"
#include "cfd/fields/VectorField.hpp"
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
  // D_f per face: 0 for every boundary face (see the boundary-treatment
  // comment above), rho*Af*d_f/dPN for internal faces -- exposed so
  // correctFaceMassFlux can reuse the *exact* coefficient the matrix was
  // built with (TODO.md section 31's consistency invariant).
  cfd::fields::SurfaceField faceCoefficient;
};

// Assembles sum_f F_f' = -R_P* (TODO.md section 19/21), where R_P* is
// cell P's predictor mass imbalance (from evaluateContinuity on
// predictorMassFlux) and F_f' = D_f * (p'_P - p'_N) for an internal
// face -- using the u-response coefficient for an x-normal face and the
// v-response coefficient for a y-normal face, distance-weighted
// interpolated to the face the same way any other cell-to-face quantity
// is (cfd::discretization::interpolateInternalFace). A FixedValue
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
    const cfd::boundary::BoundaryConditionSet& pressureBoundaries);

// F_f = F_f* + F_f', using the SAME faceCoefficient the pressure-
// correction matrix was assembled with (never interpolate(correctedU) --
// TODO.md section 30). At a boundary face this is F_f* + faceCoefficient
// * p'_owner (the F_f' = D_f*(p'_P - 0) term for a FixedValue pressure
// patch); faceCoefficient is exactly 0 for every other boundary face, so
// this is a no-op there, matching the original zero-coupling treatment.
[[nodiscard]] cfd::fields::SurfaceField correctFaceMassFlux(
    const cfd::mesh::Mesh& mesh, const cfd::fields::SurfaceField& predictorMassFlux,
    const cfd::fields::SurfaceField& faceCoefficient,
    const cfd::fields::ScalarField& pressureCorrection);

// u_P = u*_P - d_u,P * (dp'/dx)_P, v_P = v*_P - d_v,P * (dp'/dy)_P
// (TODO.md section 18), using the verified Gauss gradient of the
// pressure-correction field. Boundary condition for that gradient,
// per patch: FixedValue(0.0) (p'=0) where pressureBoundaries has a
// FixedValue pressure condition, matching assemblePressureCorrection's
// treatment there; FixedGradient(0.0) everywhere else, consistent with
// this file's zero-coupling boundary treatment (no correction crosses a
// boundary that had zero coupling in the matrix either).
[[nodiscard]] cfd::fields::VectorField correctVelocity(
    const cfd::mesh::Mesh& mesh, const cfd::fields::VectorField& predictorVelocity,
    const cfd::fields::ScalarField& uResponseCoefficient,
    const cfd::fields::ScalarField& vResponseCoefficient,
    const cfd::fields::ScalarField& pressureCorrection,
    const cfd::boundary::BoundaryConditionSet& pressureBoundaries);

}  // namespace cfd::pressure_velocity
