#pragma once

#include "cfd/boundary/BoundaryCondition.hpp"
#include "cfd/fields/VectorField.hpp"
#include "cfd/mesh/Mesh.hpp"

namespace cfd::discretization {

// grad(U) as two component gradients, one per velocity component --
// P2-TURB-004 needs the full 2D strain-rate tensor (du/dx, du/dy, dv/dx,
// dv/dy) for turbulent production, which Gradient.hpp's scalar gradient()
// cannot produce directly: it takes one cfd::fields::ScalarField plus a
// *scalar* BoundaryConditionSet, but velocity components share one
// *vector* condition per patch (Wall/MovingWall/Inlet/Outlet/Symmetry),
// not two independent scalar ones.
struct VelocityGradientField {
  // gradU[i] = (du/dx, du/dy) at cell i.
  cfd::fields::VectorField gradU;
  // gradV[i] = (dv/dx, dv/dy) at cell i.
  cfd::fields::VectorField gradV;
};

// Plain (unpaired) finite-volume Gauss/Green gradient of each velocity
// component:
//   grad(u)_P = (1/V_P) * sum_f u_f * Sf_cell
// with u_f/v_f taken from discretization::interpolateFace's vector
// overload (interior: distance-weighted linear; boundary: evaluated from
// the assigned VectorBoundaryCondition) -- reusing that already-verified
// building block rather than duplicating face-value logic.
//
// Deliberately the PLAIN Green-Gauss formula, not Gradient.hpp's
// boundary-exact quadratic-fit refinement (Gradient.cpp's
// tryPairedBoundaryContribution): that refinement is built specifically
// around a *scalar* ScalarBoundaryCondition::boundaryValue(ownerValue,
// distance) call, which has no vector equivalent here. This is therefore
// second-order accurate in the interior (same interior formula as
// Gradient.hpp) but only first-order at boundary-adjacent cells -- an
// acceptable, explicitly-scoped limitation for P2-TURB-004: turbulent
// production near a wall is only meaningful once wall functions exist
// (TODO.md P2-TURB-004 section 17, "do NOT implement full wall functions
// yet"), so boundary-cell gradient accuracy is not this task's concern.
// For a field that is exactly linear AND whose boundary values are
// themselves exact evaluations of that same linear field (as in a
// manufactured test), this plain formula is still exact everywhere,
// boundary cells included -- a linear field has no curvature for the
// quadratic-fit refinement to correct.
//
// Throws InvalidArgumentError if velocity.size() != mesh.numberOfCells().
[[nodiscard]] VelocityGradientField computeVelocityGradient(
    const cfd::mesh::Mesh& mesh, const cfd::fields::VectorField& velocity,
    const cfd::boundary::BoundaryConditionSet& velocityBoundaries);

}  // namespace cfd::discretization
