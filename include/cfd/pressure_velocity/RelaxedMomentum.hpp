#pragma once

#include <optional>
#include <vector>

#include "cfd/algebra/LinearSolver.hpp"
#include "cfd/boundary/BoundaryCondition.hpp"
#include "cfd/discretization/Convection.hpp"
#include "cfd/discretization/Gradient.hpp"
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
// P12-NUM-001: `convectionScheme` (default Upwind, exactly today's
// behavior for every pre-P12-NUM-001 call site) is forwarded unchanged
// to cfd::physics::assembleConvectionContribution -- see that function's
// own header comment for the deferred-correction scheme.
// P12-NUM-002: `gradientScheme` (default GreenGauss, exactly today's
// only behavior for every pre-P12-NUM-002 call site) is forwarded
// unchanged to cfd::physics::assemblePressureSourceContribution's own
// gradient reconstruction.
// P12-NUM-003: `applyNonOrthogonalCorrection` (default false, exactly
// today's behavior) and `nonOrthogonalCorrectionVelocity` (default null)
// are forwarded unchanged to cfd::physics::assembleDiffusionContribution's
// effective-viscosity overload, together with `gradientScheme` (the SAME
// authoritative gradient selection the pressure source already uses) as
// the correction's own gradient reconstruction -- see that function's own
// header comment.
// P12-NUM-006: `momentumSource` (default null) is an optional prescribed
// body force per unit volume (cfd::physics::assembleMomentumSourceContribution),
// added to the RHS with the other pure sources, before relaxation. Null
// leaves the assembly structurally identical to before the parameter
// existed.
[[nodiscard]] cfd::physics::MomentumAssembly assembleRelaxedMomentumComponent(
    const cfd::mesh::Mesh& mesh, const cfd::fields::VectorField& velocity,
    const cfd::fields::ScalarField& pressure, const cfd::fields::SurfaceField& massFlux,
    const cfd::fields::ScalarField& effectiveViscosity,
    const cfd::boundary::BoundaryConditionSet& velocityBoundaries,
    const cfd::boundary::BoundaryConditionSet& pressureBoundaries,
    cfd::physics::VelocityComponent component,
    const cfd::fields::ScalarField& previousComponentValue, Real alpha,
    const cfd::fields::ScalarField* temperature = nullptr,
    const cfd::physics::BoussinesqBuoyancy* buoyancy = nullptr,
    cfd::discretization::ConvectionScheme convectionScheme =
        cfd::discretization::ConvectionScheme::Upwind,
    cfd::discretization::GradientScheme gradientScheme =
        cfd::discretization::GradientScheme::GreenGauss,
    bool applyNonOrthogonalCorrection = false,
    const cfd::fields::VectorField* nonOrthogonalCorrectionVelocity = nullptr,
    const cfd::fields::VectorField* momentumSource = nullptr);

// P12-NUM-003 -- the ONE implementation of the explicit non-orthogonal
// correction loop for the momentum predictor (SIMPLE calls it; nothing
// else duplicates it). Given the pass-1 predictor velocity `velocityStar`
// (already solved with the correction evaluated from the lagged
// `velocity`), runs passes 2..`totalPasses`: each re-assembles the SAME
// relaxed u/v equations (identical implicit matrix -- only the explicit
// S_nonorth . grad(u)_f term changes) with that correction re-evaluated
// from the latest predictor velocity, and re-solves them warm-started from
// it. `totalPasses` <= 1 runs nothing (passesExecuted == 0, velocityStar
// returned unchanged, u/v unset). `passIncrements[k]` is max_i |u_i^(k+2)
// - u_i^(k+1)| (velocity magnitude), the observable that the corrector
// iteration converges. A failed assembly (NumericalError /
// InvalidArgumentError) reports NonFiniteState, an unconverged linear
// solve MomentumFailure, a non-finite solution NonFiniteState -- the
// same classifications SIMPLE applies to its own pass-1 steps. Every
// other argument has assembleRelaxedMomentumComponent's meaning.
enum class NonOrthogonalPassStatus { Completed, NonFiniteState, MomentumFailure };

struct NonOrthogonalPassResult {
  NonOrthogonalPassStatus status{NonOrthogonalPassStatus::Completed};
  Index passesExecuted{0};
  // P12-NUM-007: total inner linear-solver iterations of the pass solves
  // that completed (u and v; fallback attempts included, as in
  // SolverResult::iterations).
  Index linearIterations{0};
  cfd::fields::VectorField velocityStar;
  std::optional<cfd::physics::MomentumAssembly> u;
  std::optional<cfd::physics::MomentumAssembly> v;
  std::vector<Real> passIncrements;
  // P12-NUM-004: the fallback report of every pass solve that went through
  // the linear-solver fallback policy (empty unless the policy was enabled
  // and a primary solve failed with an eligible status), in solve order;
  // on MomentumFailure the last entry (if any) belongs to the failed solve.
  std::vector<cfd::algebra::LinearSolverFallbackReport> fallbackReports;
  // P12-NUM-004: on MomentumFailure, the failed pass solve's result.
  std::optional<cfd::algebra::SolverResult> failedSolve;
};

[[nodiscard]] NonOrthogonalPassResult runNonOrthogonalCorrectionPasses(
    Index totalPasses, const cfd::fields::VectorField& velocityStar,
    const cfd::algebra::LinearSolver& momentumSolver, const cfd::mesh::Mesh& mesh,
    const cfd::fields::VectorField& velocity, const cfd::fields::ScalarField& pressure,
    const cfd::fields::SurfaceField& massFlux, const cfd::fields::ScalarField& effectiveViscosity,
    const cfd::boundary::BoundaryConditionSet& velocityBoundaries,
    const cfd::boundary::BoundaryConditionSet& pressureBoundaries,
    const cfd::fields::ScalarField& previousU, const cfd::fields::ScalarField& previousV,
    Real alpha, const cfd::fields::ScalarField* temperature,
    const cfd::physics::BoussinesqBuoyancy* buoyancy,
    cfd::discretization::ConvectionScheme convectionScheme,
    cfd::discretization::GradientScheme gradientScheme,
    const cfd::fields::VectorField* momentumSource = nullptr);

}  // namespace cfd::pressure_velocity
