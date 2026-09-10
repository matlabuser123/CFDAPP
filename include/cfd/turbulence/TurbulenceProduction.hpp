#pragma once

#include "cfd/discretization/VectorGradient.hpp"
#include "cfd/fields/ScalarField.hpp"
#include "cfd/mesh/Mesh.hpp"

namespace cfd::turbulence {

// Turbulent kinetic energy production for the standard eddy-viscosity
// closure (P2-TURB-004 sections 5-6):
//   P_k = 2 * mu_t * Sij Sij
// where Sij = 0.5*(dUi/dxj + dUj/dxi) is the mean strain-rate tensor. For
// 2D:
//   Sxx = du/dx,  Syy = dv/dy,  Sxy = 0.5*(du/dy + dv/dx)
//   2 Sij Sij = 2*Sxx^2 + 2*Syy^2 + 4*Sxy^2
// so:
//   P_k = mu_t * (2*(du/dx)^2 + 2*(dv/dy)^2 + (du/dy + dv/dx)^2)
// (the (du/dy+dv/dx)^2 term already absorbs the factor of 4*Sxy^2 =
// 4*(0.5*(du/dy+dv/dx))^2 = (du/dy+dv/dx)^2 -- the cross-shear
// contribution is not dropped, per this task's own explicit requirement).
//
// mu_t is per-cell (dynamic, matching cfd::turbulence::TurbulenceModel's
// own unit convention); the gradients are per-cell velocity-component
// gradients (cfd::discretization::computeVelocityGradient). Result is
// per-cell P_k [W/m^3 dimensionally, i.e. rho * (m^2/s^2) / s in this
// dynamic-mu_t convention]. Throws InvalidArgumentError if
// turbulentViscosity/gradU/gradV are not all sized to
// mesh.numberOfCells().
[[nodiscard]] cfd::fields::ScalarField computeTurbulentProduction(
    const cfd::mesh::Mesh& mesh, const cfd::fields::ScalarField& turbulentViscosity,
    const cfd::discretization::VelocityGradientField& velocityGradient);

// P2-TURB-006: the bare strain-rate invariant
//   S^2 = 2*(du/dx)^2 + 2*(dv/dy)^2 + (du/dy + dv/dx)^2
// i.e. P_k without the mu_t multiplier -- SST needs this on its own for
// two purposes computeTurbulentProduction alone cannot serve: the
// strain magnitude S = sqrt(S^2) feeds directly into the SST eddy-
// viscosity limiter (SSTEquation.hpp), and P_k must be recomputed as
// mu_t*S^2 using SST's own *limited* mu_t rather than whatever
// turbulentViscosity a caller already has on hand. computeTurbulentProduction
// itself is now implemented in terms of this function (production[i] =
// turbulentViscosity[i] * strainRateMagnitudeSquared[i]) -- the exact
// same single multiplication, same operand order, as before this
// function existed, so this refactor is bit-for-bit identical to the
// pre-P2-TURB-006 implementation (verified by TurbulenceProductionTest's
// existing hand-derived tests, unchanged, still passing). Throws
// InvalidArgumentError if gradU/gradV are not sized to
// mesh.numberOfCells().
[[nodiscard]] cfd::fields::ScalarField computeStrainRateMagnitudeSquared(
    const cfd::mesh::Mesh& mesh,
    const cfd::discretization::VelocityGradientField& velocityGradient);

}  // namespace cfd::turbulence
