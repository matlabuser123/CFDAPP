#pragma once

#include "cfd/algebra/LinearSolver.hpp"
#include "cfd/algebra/LinearSystem.hpp"
#include "cfd/algebra/SparseMatrix.hpp"
#include "cfd/algebra/Vector.hpp"
#include "cfd/boundary/BoundaryCondition.hpp"
#include "cfd/fields/ScalarField.hpp"
#include "cfd/fields/SurfaceField.hpp"
#include "cfd/mesh/Mesh.hpp"

namespace cfd::turbulence {

// P2-TURB-004: a small, model-agnostic scalar-transport assembly layer
// shared by the k and epsilon equations -- neither this file nor its
// .cpp contains any k-epsilon-specific formula (no Cmu/C1epsilon/
// C2epsilon, no mu_t formula, no production term); KEpsilonModel.cpp
// computes the per-cell diffusivity/source fields these functions take
// as plain input and is the only place those formulas live (TODO.md
// P2-TURB-004's own "do not put k-epsilon-specific formulas into SIMPLE,
// PISO, or MomentumEquation" instruction, extended here to this
// assembly layer too -- keeping it reusable for a future k-omega/SST
// model exactly the way MomentumEquation's contribution assemblers are
// reused by both SIMPLE and PISO).
struct ScalarTransportAssembly {
  cfd::algebra::LinearSystem system;
  cfd::algebra::Vector diagonal;
};

// Gamma = mu + mu_t/sigma -- per cell, NOT (mu+mu_t)/sigma (P2-TURB-004
// sections 13-14: only the eddy part is divided by the Prandtl-like
// sigma constant). A small, separately-named, separately-tested function
// specifically so this exact formula -- easy to transcribe wrong as
// "mu_eff/sigma" -- is independently, mechanically verifiable (section
// 31's own diffusivity test targets this function directly) rather than
// only ever appearing inline where a transcription slip could go
// unnoticed. Used by KEpsilonModel.cpp for both Gamma_k (sigma =
// sigmaK) and Gamma_epsilon (sigma = sigmaEpsilon) -- this function has
// no k-epsilon-specific coefficient knowledge of its own beyond the
// sigma value the caller supplies. Throws InvalidArgumentError if
// molecularViscosity is not finite and > 0, sigma is not finite and > 0,
// or any turbulentViscosity entry is not finite or negative.
[[nodiscard]] cfd::fields::ScalarField computeEffectiveDiffusivity(
    Real molecularViscosity, const cfd::fields::ScalarField& turbulentViscosity, Real sigma);

// Gamma*Af/d diffusion contribution with a *per-cell* diffusivity field
// (Gamma_k = mu + mu_t/sigma_k or Gamma_epsilon = mu + mu_t/sigma_epsilon
// -- computed by the caller, not here). Same structure as
// physics::MomentumEquation's P2-TURB-003 field-based
// assembleDiffusionContribution overload and
// thermal::assembleThermalDiffusionContribution: internal faces face-
// interpolate `diffusivity` via
// discretization::interpolateInternalFace's established distance-
// weighted convention; boundary faces use the owner cell's own value (no
// neighbor to interpolate against). Rejects a mismatched-size, non-
// finite, or non-positive diffusivity field at the point it enters
// assembly, same reasoning as the P2-TURB-003 momentum overload.
//
// Throws InvalidArgumentError if phi.size()/diffusivity.size() !=
// mesh.numberOfCells(), or if any diffusivity entry is not finite or not
// > 0.
void assembleScalarDiffusionContribution(const cfd::mesh::Mesh& mesh,
                                         const cfd::fields::ScalarField& diffusivity,
                                         const cfd::fields::ScalarField& phi,
                                         const cfd::boundary::BoundaryConditionSet& boundaries,
                                         cfd::algebra::SparseMatrixBuilder& builder,
                                         cfd::algebra::Vector& rhs);

// Patankar-style implicit linearized source S = Su + Sp*phi, added into
// an *already assembled* (diffusion + convection present, not yet
// finalized) equation (TODO.md P2-TURB-004 section 12):
//   a_P -= Sp_P * V_P   (added to the diagonal via builder.add; Sp <= 0
//                        for a physically stiff destruction term keeps
//                        this a *positive* addition, i.e. stabilizing)
//   b_P += Su_P * V_P
// Su/Sp are per-cell (already volume-unintegrated source densities --
// this function multiplies by cell.volume() itself, once, the same
// convention as assembleThermalSourceContribution). No sign/positivity
// requirement is enforced on Sp here (the caller -- KEpsilonModel -- is
// responsible for supplying a physically stiff, non-positive Sp for the
// destruction terms this exists to stabilize; a badly-behaved Sp is
// still caught downstream by this equation's own allFinite() check, same
// as any other assembled coefficient). Throws InvalidArgumentError if
// Su.size()/Sp.size() != mesh.numberOfCells() or either contains a
// non-finite value.
void applyImplicitScalarSource(const cfd::mesh::Mesh& mesh,
                               cfd::algebra::SparseMatrixBuilder& builder,
                               cfd::algebra::Vector& rhs, const cfd::fields::ScalarField& Su,
                               const cfd::fields::ScalarField& Sp);

// Combines diffusion (above) + convection + the implicit source (above)
// into one complete scalar transport system:
//   div(massFlux * phi) - div(diffusivity * grad(phi)) = Su + Sp*phi
// Convection reuses thermal::assembleThermalConvectionContribution with
// specificHeat = 1.0 -- exact, not approximate: that function computes
// `effectiveFlux = specificHeat * massFlux`, and specificHeat=1.0 leaves
// massFlux bit-for-bit unchanged (x*1.0 == x in IEEE754), so this is
// genuinely "the transported quantity is bare phi, not cp*phi", not a
// coincidental reuse. Deliberately not reimplemented here: this project's
// EnergyEquation.cpp already established and validated exactly this
// upwind-at-matrix-assembly convection scheme (distinct from
// discretization::convection's own ghost-value evaluate-style operator,
// which uses a different boundary treatment -- see Convection.hpp -- and
// is not what MomentumEquation/EnergyEquation actually solve with), and
// this task's own instruction is "do not write a second scalar-equation
// framework if EnergyEquation already provides suitable reusable
// patterns."
//
// Throws InvalidArgumentError on any size mismatch (phi/diffusivity/Su/
// Sp vs. mesh cell count, massFlux vs. mesh face count) or a non-finite/
// non-positive diffusivity entry. Throws NumericalError if the final
// assembled system is non-finite.
[[nodiscard]] ScalarTransportAssembly assembleScalarTransportEquation(
    const cfd::mesh::Mesh& mesh, const cfd::fields::ScalarField& phi,
    const cfd::fields::SurfaceField& massFlux, const cfd::fields::ScalarField& diffusivity,
    const cfd::boundary::BoundaryConditionSet& boundaries, const cfd::fields::ScalarField& Su,
    const cfd::fields::ScalarField& Sp);

// One relaxed scalar-transport solve: assemble (diffusion + convection +
// implicit source, the three pieces above), apply Patankar implicit
// under-relaxation (reusing cfd::pressure_velocity::
// applyImplicitUnderRelaxation -- P2-TURB-004 section 22's "if the
// project already has a scalar relaxation mechanism, reuse it"; this is
// a cross-module reuse of a purely algebraic helper that takes no CFD-
// specific types, the same "pressure_velocity" home this codebase
// already keeps other reusable SparseMatrixBuilder/Vector-level algebra
// in), solve with BiCGSTAB (matching every other convection-diffusion
// system in this codebase -- CG's SPD requirement does not hold once
// convection is present), and clamp the result to `floorValue`.
// Originally introduced for KEpsilonModel (P2-TURB-004); reused
// unmodified by KOmegaModel (P2-TURB-005) -- this function has no
// k-epsilon- or k-omega-specific formula of its own, only Su/Sp/
// diffusivity/floor/relaxation/solver-settings as plain caller-supplied
// input, the same "model-agnostic assembly, model-specific coefficients"
// split as every other function in this file.
//
// Throws InvalidArgumentError on a size mismatch (surfaced by the
// underlying assembly calls). Throws NumericalError if assembly
// produces a non-finite system, the linear solve does not converge, or
// the solution is non-finite -- the caller (KEpsilonModel::correct(),
// KOmegaModel::correct()) lets this propagate to its own caller
// (SIMPLE/PISO), per those classes' own header comments.
[[nodiscard]] cfd::fields::ScalarField solveRelaxedScalarTransport(
    const cfd::mesh::Mesh& mesh, const cfd::fields::ScalarField& phi,
    const cfd::fields::SurfaceField& massFlux, const cfd::fields::ScalarField& diffusivity,
    const cfd::boundary::BoundaryConditionSet& boundaries, const cfd::fields::ScalarField& Su,
    const cfd::fields::ScalarField& Sp, Real alpha, Real floorValue,
    const cfd::algebra::LinearSolverSettings& solverSettings);

}  // namespace cfd::turbulence
