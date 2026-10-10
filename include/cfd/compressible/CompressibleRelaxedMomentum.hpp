#pragma once

#include "cfd/boundary/BoundaryCondition.hpp"
#include "cfd/discretization/NonOrthogonalDiffusion.hpp"
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
    const cfd::fields::ScalarField& previousComponentValue, Real alpha, Real pseudoTimeStep,
    // P12-NUM-003: forwarded to physics::assembleDiffusionContribution
    // (default disabled -- exactly the pre-existing viscous operator).
    const cfd::discretization::NonOrthogonalCorrectionOptions& nonOrthogonal = {},
    // P12-NUM-003: the pressure-source gradient (default GreenGauss --
    // exactly the pre-existing term), forwarded to
    // physics::assemblePressureSourceContribution, so CompressibleSIMPLE's
    // gradientScheme drives the pressure source exactly as SIMPLE's does.
    cfd::discretization::GradientScheme pressureGradientScheme =
        cfd::discretization::GradientScheme::GreenGauss,
    // P12-NUM-006: optional prescribed body force per unit volume
    // (cfd::physics::assembleMomentumSourceContribution), added to the RHS
    // with the pressure source, before relaxation. Null (default): the
    // assembly is structurally identical to before.
    const cfd::fields::VectorField* momentumSource = nullptr,
    // P17.4: include the DILATATIONAL part of the compressible viscous stress,
    // -(2/3) mu grad(div U), via assembleDilatationalStressContribution.
    //
    // DEFAULT FALSE -- exactly the pre-P17.4 assembly, bit for bit. The same convention
    // nonOrthogonalCorrections{0} and robustness already use: a new numerical term arrives
    // switched off so a qualified baseline is provably undisturbed.
    //
    // Off is not merely conservative here. The compressible system-level MMS manufactures
    // its forcing as "f = (m.grad)U + grad p - mu lap U" and names
    // "mu/3 grad(div U) stress term" in its own not_covered field, so enabling this term
    // unconditionally would make that passing, validated study fail for a reason that is
    // not a defect. Re-deriving its forcing is follow-on work with its own evidence burden.
    //
    // For incompressible flow div U = 0 and the term vanishes identically, which is why its
    // absence was harmless through P16 -- and why enabling it cannot disturb a
    // divergence-free field. See include/cfd/compressible/DilatationalStress.hpp for the
    // formulation and its measured boundary-accuracy limit.
    bool includeDilatationalStress = false);

}  // namespace cfd::compressible
