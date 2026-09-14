#pragma once

#include <memory>
#include <string_view>
#include <vector>

#include "cfd/algebra/LinearSolver.hpp"
#include "cfd/algebra/SparseMatrix.hpp"

namespace cfd::algebra {

// P12-NUM-004 -- the ONE linear-solver fallback policy.
//
// When the primary Krylov solve of a system fails with a status that a
// different Krylov method can plausibly recover from, retry with a bounded,
// ordered list of mathematically valid alternatives. Everything else --
// successful solves, input/configuration faults, ordinary budget exhaustion
// -- is returned untouched.
//
// Eligible statuses (isFallbackEligible):
//   Breakdown          -- an algorithmic failure of the method itself (e.g.
//                         BiCGSTAB's near-zero bi-orthogonalization inner
//                         products, P12-COMP-002's failure mode; CG's
//                         p^T A p ~ 0 on a non-SPD matrix);
//   NonFiniteResidual  -- the recurrences overflowed from FINITE inputs.
// Not eligible: NonFiniteInput / InvalidSystem (bad data or configuration --
// no algorithm fixes them) and MaxIterations (a budget outcome, not an
// algorithmic failure; retrying would only multiply the cost).
//
// Candidate order (fallbackCandidates), respecting matrix properties:
//   primary BiCGSTAB -> [CG if the matrix is PROVEN SPD (below)], GMRES
//   primary CG       -> GMRES, BiCGSTAB
//   primary GMRES    -> [CG if proven SPD], BiCGSTAB
// CG is never used unless analyzeMatrix() establishes its requirements.
//
// Every attempt:
//   - uses the SAME preconditioner type as the primary settings (Jacobi or
//     none) -- valid for every method here: Jacobi = diag(A)^-1 is SPD
//     whenever CG is eligible (positive diagonal is part of eligibility),
//     and CG/BiCGSTAB/GMRES accept any nonsingular preconditioner; a
//     zero/near-zero diagonal makes the attempt report InvalidSystem, never
//     a silent drop of preconditioning. The type is recorded per attempt;
//   - runs on the CPU (there is no GPU GMRES; a GPU primary's fallback is
//     CPU -- recorded via the result's backendUsed);
//   - starts from the latest FINITE iterate (the failed primary's, else
//     the caller's initial guess), with the primary's own maxIterations,
//     and converges to EXACTLY the primary's target
//         ||r|| <= max(absoluteTolerance, relativeTolerance * ||r0_primary||)
//     (absoluteTolerance set to that target, relativeTolerance 0), so a
//     recovered solve meets the same accuracy the primary was asked for.
// At most maxAttempts attempts (0..kMaxLinearSolverFallbackAttempts); the
// first converged attempt ends the sequence. If all fail, the last attempt's
// status is returned -- the caller's existing failure propagation (e.g.
// SIMPLEStatus::PressureCorrectionFailure) then applies unchanged.
inline constexpr Index kMaxLinearSolverFallbackAttempts = 3;

struct LinearSolverFallbackSettings {
  // Default false: without it every solver is exactly the pre-P12-NUM-004
  // one (makeLinearSolverWithFallback returns makeLinearSolver's solver).
  bool enabled{false};
  Index maxAttempts{1};

  bool operator==(const LinearSolverFallbackSettings&) const = default;
};

// Throws InvalidArgumentError if maxAttempts > kMaxLinearSolverFallbackAttempts.
void validateLinearSolverFallbackSettings(const LinearSolverFallbackSettings& settings);

// Cheap structural diagnostics (O(nnz log(row length)) plus an O(nnz)
// breadth-first search), evaluated only when a fallback is needed.
//   symmetric            a_ij == a_ji up to kSymmetryRelativeTolerance *
//                        max(|a_ij|, |a_ji|) for every stored entry (a
//                        missing transpose counts as 0);
//   positiveDiagonal     every a_ii stored and > 0;
//   diagonallyDominant   a_ii >= (1 - kDominanceRelativeTolerance) *
//                        sum_{j != i} |a_ij| in every row;
//   strictlyDominantRow  a_ii > (1 + kDominanceRelativeTolerance) * sum in
//                        at least one row;
//   irreducible          the sparsity graph is connected.
// cgEligible() = all five: a symmetric, irreducibly diagonally dominant
// matrix with a positive diagonal is nonsingular (Taussky's theorem) and,
// by Gershgorin, positive semi-definite -- hence symmetric positive
// DEFINITE, exactly CG's requirement. It is a SUFFICIENT condition: an SPD
// matrix failing it is (conservatively) not given to CG. The tolerances
// absorb only floating-point summation-order differences (a row sum
// assembled in insertion order vs. recomputed here in column order).
inline constexpr Real kSymmetryRelativeTolerance = 1e-12;
inline constexpr Real kDominanceRelativeTolerance = 1e-12;

struct MatrixProperties {
  bool square{false};
  bool symmetric{false};
  bool positiveDiagonal{false};
  bool diagonallyDominant{false};
  bool strictlyDominantRow{false};
  bool irreducible{false};

  [[nodiscard]] bool cgEligible() const noexcept {
    return square && symmetric && positiveDiagonal && diagonallyDominant && strictlyDominantRow &&
           irreducible;
  }
};

[[nodiscard]] MatrixProperties analyzeMatrix(const SparseMatrix& matrix);

[[nodiscard]] bool isFallbackEligible(SolverStatus status) noexcept;

[[nodiscard]] std::vector<LinearSolverType> fallbackCandidates(LinearSolverType primary,
                                                               const MatrixProperties& properties);

// Diagnostic names ("CG", "BiCGSTAB", "GMRES"; "Converged", "Breakdown", ...).
[[nodiscard]] std::string_view linearSolverTypeName(LinearSolverType type) noexcept;
[[nodiscard]] std::string_view solverStatusName(SolverStatus status) noexcept;

// A LinearSolver that applies the policy above around a primary solver.
// `primary` (optional) injects the primary solver -- tests use it to force
// a deterministic primary failure; when null, the primary is
// makeLinearSolver(settings). The result of a solve that did not need the
// fallback is the primary's result, bit for bit (its `fallback` report left
// default). See SolverResult::fallback for how a fallback result is formed.
class FallbackLinearSolver final : public LinearSolver {
 public:
  FallbackLinearSolver(LinearSolverSettings settings, LinearSolverFallbackSettings fallback,
                       std::unique_ptr<LinearSolver> primary = nullptr);

  using LinearSolver::solve;

  [[nodiscard]] SolverResult solve(const LinearSystem& system,
                                   const Vector& initialGuess) const override;

 private:
  LinearSolverFallbackSettings fallback_;
  std::unique_ptr<LinearSolver> primary_;
};

// makeLinearSolver(settings) when the fallback is disabled or maxAttempts is
// 0 (exactly the pre-P12-NUM-004 solver), otherwise a FallbackLinearSolver.
// Throws InvalidArgumentError for invalid settings (as makeLinearSolver).
[[nodiscard]] std::unique_ptr<LinearSolver> makeLinearSolverWithFallback(
    const LinearSolverSettings& settings, const LinearSolverFallbackSettings& fallback);

}  // namespace cfd::algebra
