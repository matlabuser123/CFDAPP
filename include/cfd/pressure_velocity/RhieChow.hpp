#pragma once

#include "cfd/boundary/BoundaryCondition.hpp"
#include "cfd/core/Types.hpp"
#include "cfd/fields/ScalarField.hpp"
#include "cfd/fields/SurfaceField.hpp"
#include "cfd/fields/VectorField.hpp"
#include "cfd/mesh/Mesh.hpp"
#include "cfd/physics/FluidProperties.hpp"

namespace cfd::pressure_velocity {

// P12-MESH-006 -- Rhie-Chow momentum interpolation of the predictor face mass
// flux (opt-in: SIMPLESettings::faceFlux, automatic for 3D meshes).
//
// Derivation (Rhie & Chow 1983; Ferziger & Peric, ch. 8; Moukalled et al.,
// ch. 15). The discrete momentum equation of cell P reads
// (a_P / alpha) u*_P = H_P - V_P (grad p)_P + ..., so u*_P carries the WIDE
// (cell-centred) pressure gradient. Rhie-Chow gives the face velocity the
// response to the COMPACT face pressure difference instead:
//   u_f = u_f,lin - D_f [ (grad p)_f,compact - (grad p)_f,lin ],
// D = diag(d_u, d_v[, d_w]) interpolated to the face. With the compact
// gradient taken along d = x_N - x_P,
//   F*_f = rho u_f . Sf = F_f,lin - C_f [ (p_N - p_P) - (grad p)_f,lin . d ],
//   C_f = D_f / alpha_u,
// where D_f is the pressure-correction coupling of the same face
// (pressureCorrectionFaceCoupling, two-point form: rho |S_D| / |d|, on an
// axis-aligned Cartesian face exactly rho A_f d_{n,f} / |d| with d_n the
// response of the face's normal component) and the division by alpha_u turns
// the RELAXED response d = V alpha / a_P into the unrelaxed V / a_P. That is
// the converged form of Majumdar's (1988) relaxation-independent correction:
// the converged face flux, hence the converged solution, does not depend on
// the under-relaxation factor. (grad p)_f,lin is the linear interpolation of
// the SAME cell pressure gradient the momentum equation used.
//
// For a smooth field the bracket is O(h^3) (a third difference), so the flux
// changes by an amount that vanishes at the discretization order; for an
// odd-even (checkerboard) pressure mode, which the wide gradient cannot see,
// it is O(1) -- the pressure-correction equation, which enforces continuity
// on this flux, then removes the mode.
//
// Boundary faces are left to the boundary conditions exactly as in the
// linear flux (calculateMassFlux: prescribed velocity for Wall/MovingWall/
// Inlet, the owner value for Outlet, the tangential part for Symmetry).
//
// `pressureGradient` is the cell gradient of `pressure` used by the momentum
// assembly; dU, dV (and dW, required for a 3D mesh) are the RELAXED momentum
// response coefficients (computeMomentumResponseCoefficient); `alpha` is the
// velocity relaxation factor of the iteration, in (0, 1].
//
// Throws InvalidArgumentError on a size mismatch, a missing dW on a 3D mesh,
// or alpha outside (0, 1].
[[nodiscard]] cfd::fields::SurfaceField rhieChowFaceCorrection(
    const cfd::mesh::Mesh& mesh, const cfd::fields::ScalarField& pressure,
    const cfd::fields::VectorField& pressureGradient, const cfd::fields::ScalarField& dU,
    const cfd::fields::ScalarField& dV, const cfd::fields::ScalarField* dW, Real density,
    Real alpha);

// F*_f = calculateMassFlux(velocityStar) + rhieChowFaceCorrection(...): the
// linear flux on every face plus the Rhie-Chow term on internal faces (0 on
// boundary faces).
[[nodiscard]] cfd::fields::SurfaceField rhieChowMassFlux(
    const cfd::mesh::Mesh& mesh, const cfd::fields::VectorField& velocityStar,
    const cfd::fields::ScalarField& pressure, const cfd::fields::VectorField& pressureGradient,
    const cfd::fields::ScalarField& dU, const cfd::fields::ScalarField& dV,
    const cfd::fields::ScalarField* dW, const cfd::physics::FluidProperties& fluid,
    const cfd::boundary::BoundaryConditionSet& velocityBoundaries, Real alpha);

}  // namespace cfd::pressure_velocity
