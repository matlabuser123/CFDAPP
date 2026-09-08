#pragma once

#include "cfd/boundary/BoundaryCondition.hpp"
#include "cfd/fields/ScalarField.hpp"
#include "cfd/fields/SurfaceField.hpp"
#include "cfd/fields/VectorField.hpp"
#include "cfd/mesh/Mesh.hpp"
#include "cfd/physics/FluidProperties.hpp"
#include "cfd/physics/MomentumEquation.hpp"

namespace cfd::pressure_velocity {

// Assembles one scalar momentum component (diffusion + convection +
// pressure source, reusing the verified contribution assemblers from
// cfd::physics::MomentumEquation directly rather than duplicating any
// physics -- TODO.md P0 -- SIMPLE section 2) with Patankar implicit
// under-relaxation applied against `previousComponentValue` (TODO.md
// section 10) -- the SAME component of the current velocity iterate,
// e.g. velocity[i].x for VelocityComponent::U.
//
// This exists in the pressure_velocity layer, not physics, because
// relaxation is a SIMPLE-algorithm concern applied *after* assembly
// (TODO.md P0 -- Incompressible Physics section 40); it cannot be a
// thin wrapper around cfd::physics::assembleMomentum because
// SparseMatrix is immutable once built (see UnderRelaxation.hpp), so
// the relaxation terms must be added to the SAME SparseMatrixBuilder
// before its one finalizing build() call.
//
// alpha == 1 disables relaxation (see applyImplicitUnderRelaxation).
// Throws InvalidArgumentError on size mismatches; NumericalError if the
// final assembled system is non-finite.
[[nodiscard]] cfd::physics::MomentumAssembly assembleRelaxedMomentumComponent(
    const cfd::mesh::Mesh& mesh, const cfd::fields::VectorField& velocity,
    const cfd::fields::ScalarField& pressure, const cfd::fields::SurfaceField& massFlux,
    const cfd::physics::FluidProperties& fluid,
    const cfd::boundary::BoundaryConditionSet& velocityBoundaries,
    const cfd::boundary::BoundaryConditionSet& pressureBoundaries,
    cfd::physics::VelocityComponent component,
    const cfd::fields::ScalarField& previousComponentValue, Real alpha);

}  // namespace cfd::pressure_velocity
