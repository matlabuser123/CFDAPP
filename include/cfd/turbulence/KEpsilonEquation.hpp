#pragma once

#include <vector>

#include "cfd/algebra/LinearSolver.hpp"
#include "cfd/algebra/LinearSystem.hpp"
#include "cfd/algebra/SparseMatrix.hpp"
#include "cfd/algebra/Vector.hpp"
#include "cfd/boundary/BoundaryCondition.hpp"
#include "cfd/discretization/NonOrthogonalDiffusion.hpp"
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

// P16-GPU-TURB PROMOTION. The k equation's Patankar source pair.
//
//   Su[i] = production[i]
//   Sp[i] = -density * epsilon[i] / max(k[i], kFloor)
//
// WHY THIS MOVED. These two lines were inline in KEpsilonModel::correct, so the
// only way to check a device port against them was to re-type them into a test
// -- and a re-typed copy agrees with a wrong port for the same reason it agrees
// with a right one. Moved verbatim so the differential compares against the
// expression production actually evaluates. Same reasoning, and the same shape,
// as computeTurbulentViscosityField's promotion.
//
// `production` is taken AS GIVEN. When wall functions are active the caller has
// already replaced the wall cells' entries with the equilibrium expression;
// that replacement is the wall treatment's job, not this function's.
//
// kFloor guards a DENOMINATOR, nothing else. Sp*k_new -> -rho*epsilon_old at
// self-consistency (P2-TURB-004 section 12), and the floor only stops that
// ratio blowing up in a cell whose k has collapsed. It is not a limiter on Sp
// and not a positivity clip.
struct KSourceTerms {
  cfd::fields::ScalarField Su;
  cfd::fields::ScalarField Sp;
};

[[nodiscard]] KSourceTerms computeKSourceTerms(Real density, const cfd::fields::ScalarField& k,
                                               const cfd::fields::ScalarField& epsilon,
                                               const cfd::fields::ScalarField& production,
                                               Real kFloor);

// P16-GPU-TURB PROMOTION. The epsilon equation's Patankar source pair.
//
//   epsOverK = epsilon[i] / max(k[i], kFloor)
//   Su[i]    = c1Epsilon * epsOverK * production[i]
//   Sp[i]    = -c2Epsilon * density * epsOverK
//
// Moved verbatim from KEpsilonModel::correct for the same reason the k pair
// was: a device port compared against a re-typed copy of these lines proves
// nothing about the formula that ships.
//
// `epsOverK` IS COMPUTED ONCE and reused by both terms. That is not a
// micro-optimisation to preserve out of politeness -- recomputing
// `epsilon[i]/kSafe` for Sp gives the same double here, but writing Su as
// `c1Epsilon * epsilon[i] * production[i] / kSafe` does NOT: it reassociates
// the division past two multiplications.
//
// Sp*epsilon_new -> -c2Epsilon*rho*epsilon_old^2/k_old at self-consistency
// (P2-TURB-004 sections 13-14), which is the destruction term. kFloor guards
// that denominator and nothing else.
struct EpsilonSourceTerms {
  cfd::fields::ScalarField Su;
  cfd::fields::ScalarField Sp;
};

[[nodiscard]] EpsilonSourceTerms computeEpsilonSourceTerms(
    Real density, Real c1Epsilon, Real c2Epsilon, const cfd::fields::ScalarField& k,
    const cfd::fields::ScalarField& epsilon, const cfd::fields::ScalarField& production,
    Real kFloor);

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
// P16: one cell whose transported value is IMPOSED rather than solved.
//
// A wall function fixes epsilon in the wall-adjacent cell because the epsilon
// transport equation is not valid in a cell that spans the log layer. The row
// is replaced by `phi_cell = value` (SparseMatrixBuilder::fixRow), which is
// exact and well-conditioned -- unlike a large-coefficient Patankar source,
// which only approaches the value and needs an arbitrary magnitude.
struct ImposedCell {
  Index cell{};
  Real value{};
};

[[nodiscard]] cfd::fields::ScalarField solveRelaxedScalarTransport(
    const cfd::mesh::Mesh& mesh, const cfd::fields::ScalarField& phi,
    const cfd::fields::SurfaceField& massFlux, const cfd::fields::ScalarField& diffusivity,
    const cfd::boundary::BoundaryConditionSet& boundaries, const cfd::fields::ScalarField& Su,
    const cfd::fields::ScalarField& Sp, Real alpha, Real floorValue,
    const cfd::algebra::LinearSolverSettings& solverSettings,
    const cfd::discretization::NonOrthogonalCorrectionOptions& nonOrthogonal = {},
    // Empty (the default) leaves the assembled system byte-for-byte what it was
    // before P16, which is what keeps every existing turbulence result
    // unchanged when no wall treatment is active.
    const std::vector<ImposedCell>& imposedCells = {});

}  // namespace cfd::turbulence
