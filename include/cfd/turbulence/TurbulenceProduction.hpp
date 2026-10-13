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

// P16-GPU-TURB PROMOTION. The standard k-epsilon eddy viscosity
//
//     mu_t = rho C_mu k^2 / epsilon
//
// with k and epsilon floored where they appear in a DENOMINATOR or would
// otherwise vanish.
//
// WHY THIS MOVED. The body below lived in an anonymous namespace inside
// KEpsilonModel.cpp and was declared in no header, so the production formula
// had no caller outside one translation unit. A CPU/GPU differential for mu_t
// therefore had nothing to compare the device against except a re-typed copy
// of the expression -- and a re-typed copy is exactly what a differential must
// not use, because it agrees with the device for the same reason it would
// agree with a wrong device: both came from the same fingers, not from the
// code that ships.
//
// So this is a PROMOTION, not a new function: the body is moved verbatim,
// KEpsilonModel keeps calling it by the same unqualified name (it is in this
// namespace already), and the device port is compared against the function
// production actually runs.
//
// The operand order is load-bearing and is preserved exactly:
// `density * cMu * kSafe * kSafe / epsSafe` associates as
// `(((density*cMu)*kSafe)*kSafe)/epsSafe`, which is not the same double as
// `density*cMu*(kSafe*kSafe/epsSafe)`.
//
// No size validation is performed, matching the moved original. Adding one
// here would change behaviour in a phase that is not authorized to.
[[nodiscard]] cfd::fields::ScalarField computeTurbulentViscosityField(
    const cfd::mesh::Mesh& mesh, Real density, Real cMu, const cfd::fields::ScalarField& k,
    const cfd::fields::ScalarField& epsilon, Real kFloor, Real epsilonFloor);

}  // namespace cfd::turbulence
