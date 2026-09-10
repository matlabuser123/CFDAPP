#pragma once

#include "cfd/boundary/BoundaryCondition.hpp"
#include "cfd/fields/ScalarField.hpp"
#include "cfd/fields/SurfaceField.hpp"
#include "cfd/fields/VectorField.hpp"
#include "cfd/mesh/Mesh.hpp"
#include "cfd/physics/BoussinesqBuoyancy.hpp"
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
//
// P2-TURB-003: `effectiveViscosity` (mu_eff = mu + mu_t, one value per
// cell) replaces the single constant FluidProperties::dynamicViscosity()
// this function used before -- SIMPLE now always computes it from a
// TurbulenceModel (defaulting to a laminar one internally), so it is
// passed in directly rather than this function reaching back into
// FluidProperties itself; FluidProperties is not needed here otherwise
// (relaxation and the other contributions have no density dependence).
//
// P3-PHYS-001: `temperature`/`buoyancy` are an optional, non-owning pair
// (both null, or both non-null -- never one alone) for the Boussinesq
// body-force source (cfd::physics::assembleBuoyancySourceContribution),
// added to the RHS alongside the pressure-source contribution before
// relaxation is applied (mathematically order-independent here --
// applyImplicitUnderRelaxation's added term depends only on the
// diagonal and phiOld, not on rhs's current value, so it commutes with
// any other pure-source addition; see this .cpp's own comment). Left
// null (the default), this function's assembled system is *exactly*
// the same code path as before this parameter pair existed -- not just
// numerically equivalent, structurally identical -- so every pre-
// existing call site (and P3-PHYS-001's own required "zero-buoyancy
// equivalence" gate, in the strongest form: bit-identical, not merely
// close) is preserved automatically, with no special-cased "beta=0"
// branch needed here. Throws InvalidArgumentError if exactly one of
// temperature/buoyancy is non-null (an inconsistent call), on the usual
// size mismatches, or if the buoyancy contribution's own validation
// fails; NumericalError if the final assembled system is non-finite.
[[nodiscard]] cfd::physics::MomentumAssembly assembleRelaxedMomentumComponent(
    const cfd::mesh::Mesh& mesh, const cfd::fields::VectorField& velocity,
    const cfd::fields::ScalarField& pressure, const cfd::fields::SurfaceField& massFlux,
    const cfd::fields::ScalarField& effectiveViscosity,
    const cfd::boundary::BoundaryConditionSet& velocityBoundaries,
    const cfd::boundary::BoundaryConditionSet& pressureBoundaries,
    cfd::physics::VelocityComponent component,
    const cfd::fields::ScalarField& previousComponentValue, Real alpha,
    const cfd::fields::ScalarField* temperature = nullptr,
    const cfd::physics::BoussinesqBuoyancy* buoyancy = nullptr);

}  // namespace cfd::pressure_velocity
