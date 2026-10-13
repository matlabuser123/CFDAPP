#pragma once

#include "cfd/algebra/Vector.hpp"
#include "cfd/boundary/BoundaryCondition.hpp"
#include "cfd/discretization/Gradient.hpp"
#include "cfd/fields/VectorField.hpp"
#include "cfd/mesh/Mesh.hpp"
#include "cfd/physics/MomentumEquation.hpp"

namespace cfd::compressible {

// P17.4: the DILATATIONAL part of the compressible viscous stress.
//
// For a Newtonian fluid under the Stokes hypothesis the deviatoric stress is
//
//     tau = mu [grad(U) + grad(U)^T] - (2/3) mu (div U) I
//
// and its divergence contributes, for the component this call assembles,
//
//     [div(tau)]_i  =  mu lap(U_i)  +  (mu/3) d/dx_i (div U)       (constant mu)
//
// The FIRST term is what physics::assembleDiffusionContribution already provides -- the
// incompressible Laplacian. This function supplies the SECOND, which CFDApp did not have:
// measured before P17.4, `grad(div`, `dilatation`, `bulkViscosity`, `twoThirds`, `Stokes`
// and `divU` all returned ZERO hits across include/ and src/.
//
// Written here in the form the momentum RHS needs, as an explicit source:
//
//     rhs[P]  +=  -(2/3) * mu * grad(div U)_component[P] * V_P
//
// SIGN AND FACTOR. The contribution to div(tau) from the -(2/3) mu (div U) I term is
// -(2/3) mu grad(div U). It enters the momentum equation on the same side as -grad(p), so
// it is added to the RHS with that sign and scaled by the cell volume, exactly as
// assemblePressureSourceContribution scales -grad(p). (A reader expecting +mu/3 should note
// that form arises only after combining with the grad(grad . U) piece of
// div(mu grad(U)^T), which the Laplacian operator here does not include; this function
// supplies precisely the -(2/3) mu grad(div U) term and nothing else.)
//
// WHY IT IS OFF BY DEFAULT where it is wired in. For incompressible flow div U = 0 and this
// term vanishes identically, which is why its absence was harmless through P16. Enabling it
// unconditionally would change the equation the compressible system-level MMS is
// manufactured against -- that study's forcing is hand-derived as
// "f = (m.grad)U + grad p - mu lap U" and its own not_covered field names
// "mu/3 grad(div U) stress term". A passing, validated test would begin failing for a
// reason that is not a defect. See results/p17/compressible_momentum/acceptance_gate.md §0.
//
// IMPLEMENTATION. Composes two existing, tested operators rather than inventing
// discretisation:
//     divU     = discretization::divergence(mesh, velocity, velocityBoundaries)
//     gradDivU = discretization::gradient(mesh, divU, <zero-gradient>, scheme)
//
// BOUNDARY TREATMENT, and its accuracy limit. There is no physical boundary condition for
// the derived scalar div U, so zero-gradient is used: a boundary face takes its owner
// cell's value. That is the standard treatment for a derived quantity with no physical BC,
// and it has a measured consequence -- div U is exact everywhere (measured 0 error for
// U = (x^2/2, 0), where div U = x analytically), but grad(div U) is exact only TWO OR MORE
// CELLS from a boundary: the artificial BC makes div U wrong in the boundary-adjacent
// layer, and a Green-Gauss gradient of a wrong neighbour pollutes one cell further in.
// Measured: deep-interior cells give grad(div U).x == 1 exactly, while the worst error over
// all nominally interior cells of an 8x4 mesh is 2. This is a real limitation of the term
// as implemented, recorded rather than hidden, and it is why the gate asserts accuracy in
// the deep interior only.
//
// Throws InvalidArgumentError if velocity.size() != mesh.numberOfCells(), if rhs.size()
// does not match, or if dynamicViscosity is not finite and >= 0.
void assembleDilatationalStressContribution(
    const cfd::mesh::Mesh& mesh, const cfd::fields::VectorField& velocity,
    const cfd::boundary::BoundaryConditionSet& velocityBoundaries, Real dynamicViscosity,
    cfd::physics::VelocityComponent component, cfd::algebra::Vector& rhs,
    cfd::discretization::GradientScheme scheme = cfd::discretization::GradientScheme::GreenGauss);

}  // namespace cfd::compressible
