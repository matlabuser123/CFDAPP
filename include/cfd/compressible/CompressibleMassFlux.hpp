#pragma once

#include "cfd/boundary/BoundaryCondition.hpp"
#include "cfd/compressible/ThermodynamicProperties.hpp"
#include "cfd/fields/ScalarField.hpp"
#include "cfd/fields/SurfaceField.hpp"
#include "cfd/fields/VectorField.hpp"
#include "cfd/mesh/Mesh.hpp"

namespace cfd::compressible {

// P3-PHYS-006: the one canonical compressible face mass flux (this
// task's own section 11: "Define one canonical compressible face mass
// flux... Do not independently calculate face mass flux in each
// equation") --
//   mDot_f = rho_f * (u_f . Sf)
// -- the direct compressible generalization of
// physics::calculateMassFlux (rho_f a per-cell field here instead of one
// constant), reusing the exact same face-velocity evaluation
// (cfd::discretization::interpolateFace, which already dispatches
// correctly to every existing velocity BC -- section 25/26 do not need a
// second velocity-interpolation mechanism).
//
// Face-density policy (section 12, superseded at boundaries by
// P12-COMP-001 -- see below):
//   - Internal faces: this project's established distance-weighted
//     linear interpolation (cfd::discretization::interpolateInternalFace,
//     arithmetic mean on a uniform grid) of the per-cell EOS-evaluated
//     `density` field -- the same "for low Mach, arithmetic interpolation
//     may be sufficient initially" policy section 12 endorses. Unchanged
//     by P12-COMP-001.
//   - Boundary faces (P12-COMP-001): the EOS evaluated at that face's own
//     boundary-interpolated absolute pressure and temperature --
//     `rho_face = thermodynamics.density(referencePressure +
//     interpolateFace(pressureGauge, pressureBoundaries), T_face)`, where
//     `T_face` is either the (spatially uniform) isothermal temperature
//     or `interpolateFace(temperature, *temperatureBoundaries)` when
//     `temperature.thermal_coupled` is set. This reuses the same generic,
//     already-existing `cfd::discretization::interpolateFace` every other
//     boundary-aware field evaluation in this codebase already goes
//     through -- no new boundary-condition types or per-patch case-format
//     keys were needed, since a compressible case's existing
//     velocity/pressure (and, when thermal-coupled, temperature)
//     boundary conditions already fully determine the thermodynamic state
//     at every boundary face.
//
//   This makes every existing boundary-condition type behave physically:
//   an `Outlet` typically paired with a Dirichlet (`fixed_value`)
//   pressure BC gets the *exact* reference-pressure-consistent density
//   at that face (not the interior cell's own, generally different,
//   density); an `Inlet`/`Wall` typically paired with a zero-gradient
//   pressure BC gets a boundary density that reduces to the owner cell's
//   own value when the gradient is genuinely zero (the previous
//   simplification's result, now derived rather than assumed); a `Wall`
//   face's mass flux is `rho_face * 0` regardless of `rho_face` (the
//   velocity BC there is zero-normal-flow by construction), so the
//   boundary-density choice has no effect on conservation at walls
//   either way -- correctness there was never actually at stake, only at
//   inlets/outlets with a genuinely prescribed or extrapolated boundary
//   thermodynamic state differing from the interior.
//
// Historical note: prior to P12-COMP-001, a boundary face had no
// separate boundary-density state at all (density was treated purely as
// a cell-centered derived field with no BC of its own in this
// foundation) -- the owner cell's own density was used directly, the
// same "no neighbor to interpolate against, use the owner value"
// convention already established for field-based effective-
// viscosity/conductivity at boundaries (P2-TURB-003, P3-PHYS-003). That
// was a disclosed simplification (see TODO.md's own P3-PHYS-006 status
// note and `results/p12-comp-001/summary.md` for this task's own
// evidence), not a defect -- P12-COMP-001 supersedes it with the
// EOS-based treatment above rather than correcting a bug.
//
// Throws InvalidArgumentError if velocity.size(), density.size(),
// pressureGauge.size(), or temperature.size() != mesh.numberOfCells(),
// or (propagated from `thermodynamics.density()`) if any boundary face's
// evaluated absolute pressure or temperature is not finite and strictly
// positive.
[[nodiscard]] cfd::fields::SurfaceField calculateCompressibleMassFlux(
    const cfd::mesh::Mesh& mesh, const cfd::fields::VectorField& velocity,
    const cfd::fields::ScalarField& density,
    const cfd::boundary::BoundaryConditionSet& velocityBoundaries,
    const cfd::fields::ScalarField& pressureGauge,
    const cfd::boundary::BoundaryConditionSet& pressureBoundaries, Real referencePressure,
    const ThermodynamicProperties& thermodynamics, const cfd::fields::ScalarField& temperature,
    const cfd::boundary::BoundaryConditionSet* temperatureBoundaries);

// P12-COMP-002: the exact per-face density `calculateCompressibleMassFlux`
// evaluates internally (internal faces: distance-weighted interpolation
// of `density`; boundary faces: the P12-COMP-001 EOS-based treatment,
// see this header's own comment above), now also exposed directly. A
// compressible pressure-correction equation's D_f face coefficient must
// use *this exact same* per-face density -- not a second, independently-
// computed one -- for the same "one canonical compressible face
// quantity, consumed consistently everywhere" reason this file's own
// mass-flux function already follows (P3-PHYS-006 section 11/17), and
// the same consistency invariant `PressureCorrectionEquation.hpp`'s own
// header comment documents for its incompressible `faceCoefficient`.
// `calculateCompressibleMassFlux` itself calls this internally (a pure
// refactor -- its behavior/signature are unchanged); this function is
// the shared building block, not a second, parallel implementation.
//
// Same throws contract as `calculateCompressibleMassFlux`.
[[nodiscard]] cfd::fields::SurfaceField evaluateCompressibleFaceDensity(
    const cfd::mesh::Mesh& mesh, const cfd::fields::ScalarField& density,
    const cfd::fields::ScalarField& pressureGauge,
    const cfd::boundary::BoundaryConditionSet& pressureBoundaries, Real referencePressure,
    const ThermodynamicProperties& thermodynamics, const cfd::fields::ScalarField& temperature,
    const cfd::boundary::BoundaryConditionSet* temperatureBoundaries);

}  // namespace cfd::compressible
