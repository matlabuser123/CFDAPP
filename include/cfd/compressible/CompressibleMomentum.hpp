#pragma once

#include "cfd/algebra/Vector.hpp"
#include "cfd/boundary/BoundaryCondition.hpp"
#include "cfd/fields/ScalarField.hpp"
#include "cfd/fields/SurfaceField.hpp"
#include "cfd/fields/VectorField.hpp"
#include "cfd/mesh/Mesh.hpp"
#include "cfd/physics/MomentumEquation.hpp"

namespace cfd::compressible {

// P3-PHYS-006: the compressible momentum transient-storage term (section
// 13-14) --
//   (rho_new*u_new - rho_old*u_old) * V_P / dt
// split into the same diagonal/source form
// cfd::discretization::implicitEulerTimeDerivative already establishes
// for the incompressible transient term, generalized to two *different*
// densities (this task's own section 14 explicit warning: "Do not
// simply use rho_current*(u_new-u_old)*V/dt... without verifying whether
// that is consistent" -- it is not, for a genuinely changing density;
// the correct conservative discretization keeps rho_old with u_old and
// rho_new with u_new separately):
//   diagonal[P] = rho_new[P] * V_P / dt      (multiplies the unknown u_new)
//   source[P]   = rho_old[P] * V_P / dt * u_old[P]   (known, moves to RHS)
// For densityOld == densityNew == a single constant, this reduces
// exactly to discretization::implicitEulerTimeDerivative's own formula
// (proven in test_compressible_momentum.cpp).
struct CompressibleTimeDerivativeCoefficients {
  cfd::fields::ScalarField diagonal;
  cfd::fields::ScalarField source;
};

// `velocityOldComponent` is the previous-time-level scalar component
// (u^n or v^n) this term needs, the same "already-selected scalar
// component" convention pressure_velocity::assembleTransientMomentumComponent
// already uses for `previousComponentValue`. Throws InvalidArgumentError
// if velocityOldComponent/densityOld/densityNew.size() !=
// mesh.numberOfCells(), or dt is not finite or <= 0.
[[nodiscard]] CompressibleTimeDerivativeCoefficients compressibleMomentumTimeDerivative(
    const cfd::mesh::Mesh& mesh, const cfd::fields::ScalarField& velocityOldComponent,
    const cfd::fields::ScalarField& densityOld, const cfd::fields::ScalarField& densityNew, Real dt);

// Assembles one scalar compressible momentum component: diffusion +
// convection + pressure source, reusing the *existing*, unmodified
// physics::MomentumEquation contribution assemblers directly (section 13's
// own "Reuse existing viscous discretization/property infrastructure...
// Do not rewrite all FVM momentum code if the existing assembler can be
// generalized to accept mass flux and variable density" -- the existing
// assemblers already accept an arbitrary massFlux/SurfaceField, so no
// generalization was even needed there: physics::assembleConvectionContribution
// works unchanged with a compressible mass flux, since it never assumes
// how massFlux was computed) -- plus this file's own compressible
// transient-storage term above. `dynamicViscosity` is a single constant
// (section 7: "For the initial validation, constant mu... is
// sufficient") -- a variable-viscosity compressible overload can reuse
// physics::assembleDiffusionContribution's own field-based overload
// (P2-TURB-003/P3-PHYS-003) exactly the same way if ever needed, with no
// change to this function.
//
// `massFlux` must be the canonical compressible flux
// (calculateCompressibleMassFlux) -- this function does not compute or
// validate that itself (section 17's "one authoritative mass-flux path"
// is a calling-convention property, not something enforceable at a
// single function's own signature).
//
// Throws InvalidArgumentError on any size mismatch (velocity/pressure/
// densityOld/densityNew vs. mesh cell count, massFlux vs. mesh face
// count) or if dt is not finite or <= 0. Throws NumericalError if the
// final assembled system is non-finite.
[[nodiscard]] cfd::physics::MomentumAssembly assembleCompressibleMomentumComponent(
    const cfd::mesh::Mesh& mesh, const cfd::fields::VectorField& velocity,
    const cfd::fields::ScalarField& velocityOldComponent, const cfd::fields::ScalarField& pressure,
    const cfd::fields::SurfaceField& massFlux, const cfd::fields::ScalarField& densityOld,
    const cfd::fields::ScalarField& densityNew, Real dynamicViscosity,
    const cfd::boundary::BoundaryConditionSet& velocityBoundaries,
    const cfd::boundary::BoundaryConditionSet& pressureBoundaries,
    cfd::physics::VelocityComponent component, Real dt);

}  // namespace cfd::compressible
