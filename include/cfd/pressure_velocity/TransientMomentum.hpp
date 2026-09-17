#pragma once

#include "cfd/algebra/SparseMatrix.hpp"
#include "cfd/algebra/Vector.hpp"
#include "cfd/boundary/BoundaryCondition.hpp"
#include "cfd/fields/ScalarField.hpp"
#include "cfd/fields/SurfaceField.hpp"
#include "cfd/fields/VectorField.hpp"
#include "cfd/mesh/Mesh.hpp"
#include "cfd/physics/FluidProperties.hpp"
#include "cfd/physics/MomentumEquation.hpp"

namespace cfd::pressure_velocity {

// Adds an already-computed implicit-Euler time-derivative contribution
// (cfd::discretization::implicitEulerTimeDerivative) directly into an
// *already assembled* (but not yet finalized) equation: for row P,
//   diagonal(P) += diagonalContribution[P]  (= rho*V_P/dt)
//   rhs[P]      += sourceContribution[P]    (= diagonalContribution[P] * phiOld[P])
// Pure algebra (SparseMatrixBuilder::add sums with whatever the row
// already holds), the same way applyImplicitUnderRelaxation
// (UnderRelaxation.hpp) adds its own relaxation term -- deliberately
// separate from it: PISO's transient momentum predictor does not carry
// SIMPLE's outer alphaU/alphaP relaxation (TODO.md P2 -- PISO notes:
// "PISO should not simply be SIMPLE + dt"). `builder` must already hold
// every base-physics contribution (diffusion + convection + pressure
// source) at every row; call builder.build() again afterward for the
// final matrix, the same calling convention as
// applyImplicitUnderRelaxation.
//
// Throws InvalidArgumentError if diagonalContribution/sourceContribution
// size does not match rhs's.
void applyTransientTerm(cfd::algebra::SparseMatrixBuilder& builder, cfd::algebra::Vector& rhs,
                        const cfd::algebra::Vector& diagonalContribution,
                        const cfd::algebra::Vector& sourceContribution);

// Assembles one scalar momentum component (diffusion + convection +
// pressure source, reusing the verified contribution assemblers from
// cfd::physics::MomentumEquation directly -- no physics duplicated, the
// same reuse RelaxedMomentum.hpp already establishes for SIMPLE) plus the
// implicit-Euler time-derivative term against `previousComponentValue`
// (the accepted u^n or v^n component -- TODO.md P2 sections 9-10).
// Deliberately does NOT apply implicit under-relaxation: PISO's
// transient diagonal (rho*V/dt) already provides diagonal dominance the
// way SIMPLE's alphaU/alphaP relaxation does for its own steady outer
// loop, so carrying SIMPLE's relaxation into PISO would be doubling up
// on a concern the transient term already covers, not "SIMPLE + dt".
//
// Throws InvalidArgumentError on size mismatches (velocity/pressure vs.
// mesh cell count, massFlux vs. mesh face count) or if dt is not finite
// or <= 0. Throws NumericalError if the final assembled system is
// non-finite.
[[nodiscard]] cfd::physics::MomentumAssembly assembleTransientMomentumComponent(
    const cfd::mesh::Mesh& mesh, const cfd::fields::VectorField& velocity,
    const cfd::fields::ScalarField& pressure, const cfd::fields::SurfaceField& massFlux,
    const cfd::physics::FluidProperties& fluid,
    const cfd::boundary::BoundaryConditionSet& velocityBoundaries,
    const cfd::boundary::BoundaryConditionSet& pressureBoundaries,
    cfd::physics::VelocityComponent component,
    const cfd::fields::ScalarField& previousComponentValue, Real dt);

// P2-TURB-003: same as the overload above, but the diffusion term uses a
// per-cell `effectiveViscosity` field (mu_eff = mu + mu_t, from
// cfd::turbulence::TurbulenceModel::effectiveViscosity()) instead of the
// single constant FluidProperties::dynamicViscosity() -- `fluid` is still
// needed here (unlike RelaxedMomentum.hpp's equivalent change) because
// the implicit-Euler time-derivative term still uses fluid.density().
// This is a separate overload, not a replacement, so every existing
// caller of the constant-viscosity overload above (PISO's own extensive
// test suite included) keeps its exact prior behavior untouched; PISO
// itself calls this overload instead once a turbulence model is wired in.
// Same throwing behavior as the overload above, plus InvalidArgumentError
// if effectiveViscosity.size() != mesh.numberOfCells().
[[nodiscard]] cfd::physics::MomentumAssembly assembleTransientMomentumComponent(
    const cfd::mesh::Mesh& mesh, const cfd::fields::VectorField& velocity,
    const cfd::fields::ScalarField& pressure, const cfd::fields::SurfaceField& massFlux,
    const cfd::physics::FluidProperties& fluid, const cfd::fields::ScalarField& effectiveViscosity,
    const cfd::boundary::BoundaryConditionSet& velocityBoundaries,
    const cfd::boundary::BoundaryConditionSet& pressureBoundaries,
    cfd::physics::VelocityComponent component,
    const cfd::fields::ScalarField& previousComponentValue, Real dt);

// P12-MESH-007 -- the ALE (moving-mesh) momentum component: the effective-
// viscosity overload above with exactly two differences (results/p12-mesh-007/
// architecture.md section 3.6):
//   - convection uses `convectingMassFlux`, the flux RELATIVE to the moving
//     mesh (cfd::physics::relativeMassFlux: F - rho dV/dt), through the same
//     shared assembleConvectionContribution;
//   - the time term is cfd::discretization::aleImplicitEulerTimeDerivative
//     with V^n = previousVolume and V^{n+1} = the mesh's current volumes.
// Everything else (diffusion with mu_eff, pressure source, the 2D guard, the
// size checks, the non-finite check) is identical, and so is the operation
// order: with a stationary mesh (convectingMassFlux == the mass flux,
// previousVolume == the current volumes) the result is bit for bit that of the
// effective-viscosity overload.
//
// Throws as the effective-viscosity overload, plus InvalidArgumentError if
// previousVolume.size() != mesh.numberOfCells().
[[nodiscard]] cfd::physics::MomentumAssembly assembleAleTransientMomentumComponent(
    const cfd::mesh::Mesh& mesh, const cfd::fields::VectorField& velocity,
    const cfd::fields::ScalarField& pressure, const cfd::fields::SurfaceField& convectingMassFlux,
    const cfd::physics::FluidProperties& fluid, const cfd::fields::ScalarField& effectiveViscosity,
    const cfd::boundary::BoundaryConditionSet& velocityBoundaries,
    const cfd::boundary::BoundaryConditionSet& pressureBoundaries,
    cfd::physics::VelocityComponent component,
    const cfd::fields::ScalarField& previousComponentValue,
    const cfd::fields::ScalarField& previousVolume, Real dt);

}  // namespace cfd::pressure_velocity
