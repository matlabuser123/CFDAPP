#pragma once

#include "cfd/boundary/BoundaryCondition.hpp"
#include "cfd/fields/ScalarField.hpp"
#include "cfd/fields/SurfaceField.hpp"
#include "cfd/fields/VectorField.hpp"
#include "cfd/mesh/Mesh.hpp"
#include "cfd/physics/MomentumEquation.hpp"

namespace cfd::compressible {

// P12-COMP-002: the compressible analog of
// cfd::pressure_velocity::assembleRelaxedMomentumComponent -- reuses the
// exact same *existing, unmodified* diffusion/convection/pressure-source
// assemblers from cfd::physics::MomentumEquation (no new physics for
// those three contributions at all) plus this module's own
// compressibleMomentumTimeDerivative (also existing, unmodified) for the
// transient-storage term, plus cfd::pressure_velocity's own existing,
// unmodified applyImplicitUnderRelaxation. This function's own new code
// is the composition of those three pieces, not new numerics --
// `assembleCompressibleMomentumComponent` (CompressibleMomentum.hpp)
// itself has no relaxation hook (it calls `.build()` on its own
// SparseMatrixBuilder internally), so it is not reusable as-is inside an
// iterative, relaxed outer loop -- this function exists instead of it
// for exactly that reason (see the P12-COMP-002 plan's own "Refined
// findings" for the full reasoning).
//
// Pseudo-transient (dual-time) framing, not literal unsteadiness:
// `CompressibleSIMPLE` is a steady solver, so `densityOld` and
// `densityNew` are typically the *same* field here (the current
// best-known per-cell density at the start of this outer iteration) --
// `compressibleMomentumTimeDerivative`'s own
// `ConstantDensityMatchesIncompressibleFormula` test already proves this
// degenerates correctly to the same transient-stabilization term
// `assembleRelaxedMomentumComponent` gets from
// `discretization::implicitEulerTimeDerivative` when density is
// literally constant. `pseudoTimeStep` is a numerical stabilization
// parameter (like `alpha` below), not a physical time step.
//
// alpha == 1 disables relaxation (see applyImplicitUnderRelaxation).
// Throws InvalidArgumentError on the usual size mismatches (velocity/
// pressure/densityOld/densityNew vs. mesh cell count, massFlux vs. mesh
// face count) or if pseudoTimeStep is not finite and > 0; NumericalError
// if the final assembled system is non-finite.
[[nodiscard]] cfd::physics::MomentumAssembly assembleRelaxedCompressibleMomentumComponent(
    const cfd::mesh::Mesh& mesh, const cfd::fields::VectorField& velocity,
    const cfd::fields::ScalarField& pressure, const cfd::fields::SurfaceField& massFlux,
    const cfd::fields::ScalarField& densityOld, const cfd::fields::ScalarField& densityNew,
    Real dynamicViscosity, const cfd::boundary::BoundaryConditionSet& velocityBoundaries,
    const cfd::boundary::BoundaryConditionSet& pressureBoundaries,
    cfd::physics::VelocityComponent component,
    const cfd::fields::ScalarField& previousComponentValue, Real alpha, Real pseudoTimeStep);

}  // namespace cfd::compressible
