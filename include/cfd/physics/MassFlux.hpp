#pragma once

#include "cfd/boundary/BoundaryCondition.hpp"
#include "cfd/fields/SurfaceField.hpp"
#include "cfd/fields/VectorField.hpp"
#include "cfd/mesh/Mesh.hpp"
#include "cfd/physics/FluidProperties.hpp"

namespace cfd::physics {

// Face mass flux for constant-density incompressible flow:
//   massFlux_f = rho * (U_f . Sf)
// where U_f is the face velocity from the already-verified linear
// interpolation (interior faces) / boundary-condition evaluation
// (boundary faces) -- see cfd/discretization/Interpolation.hpp. Sf is
// owner-oriented (owner->neighbor internally, outward from the domain at
// a boundary), so massFlux_f > 0 means mass leaving the owner cell
// through this face.
//
// This is the P0 -- Incompressible Physics geometric/interpolated flux,
// not the later SIMPLE-phase pressure-corrected (Rhie-Chow) flux: on a
// collocated grid, naive interpolation can permit pressure-velocity
// decoupling, which the pressure-velocity phase addresses separately.
// Do not treat this as the final face-flux formulation.
//
// 2D convention: the result is a flux per unit depth (kg/(s*m) in a 3D
// reading), not kg/s, since the mesh itself has no thickness dimension.
//
// Throws InvalidArgumentError if velocity.size() != mesh.numberOfCells().
[[nodiscard]] cfd::fields::SurfaceField calculateMassFlux(
    const cfd::mesh::Mesh& mesh, const cfd::fields::VectorField& velocity,
    const FluidProperties& fluid, const cfd::boundary::BoundaryConditionSet& velocityBoundaries);

}  // namespace cfd::physics
