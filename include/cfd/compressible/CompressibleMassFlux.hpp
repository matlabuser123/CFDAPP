#pragma once

#include "cfd/boundary/BoundaryCondition.hpp"
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
// Face-density policy (section 12): internal faces use this project's
// established distance-weighted linear interpolation
// (cfd::discretization::interpolateInternalFace, arithmetic mean on a
// uniform grid) -- the same "for low Mach, arithmetic interpolation may
// be sufficient initially" policy this task's own section 12 endorses. A
// boundary face has no separate boundary-density state (density is a
// derived field, not a primary transported quantity with its own BCs in
// this foundation) -- disclosed simplification: the owner cell's own
// density is used directly, the same "no neighbor to interpolate
// against, use the owner value" convention already established for
// field-based effective-viscosity/conductivity at boundaries
// (P2-TURB-003, P3-PHYS-003). A physically complete compressible inlet/
// outlet boundary-density model (evaluating a prescribed boundary
// thermodynamic state through the EOS, sections 26-27) is deferred --
// see TODO.md's own P3-PHYS-006 status note.
//
// Throws InvalidArgumentError if velocity.size() or density.size() !=
// mesh.numberOfCells().
[[nodiscard]] cfd::fields::SurfaceField calculateCompressibleMassFlux(
    const cfd::mesh::Mesh& mesh, const cfd::fields::VectorField& velocity,
    const cfd::fields::ScalarField& density,
    const cfd::boundary::BoundaryConditionSet& velocityBoundaries);

}  // namespace cfd::compressible
