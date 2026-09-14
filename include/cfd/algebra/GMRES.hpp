#pragma once

#include <memory>

#include "cfd/algebra/LinearSolver.hpp"
#include "cfd/algebra/Preconditioner.hpp"

namespace cfd::algebra {

// P12-NUM-004: restarted GMRES(m) with RIGHT preconditioning (Saad,
// "Iterative Methods for Sparse Linear Systems", 2nd ed., Algorithm 9.5):
// minimizes ||b - A x|| over x0 + M^-1 K_m(A M^-1, r0) with modified
// Gram-Schmidt Arnoldi and Givens rotations, restarting every
// m = min(settings.gmresRestart, n) iterations from the TRUE residual.
// Right preconditioning keeps the minimized quantity the unpreconditioned
// residual norm, so the convergence test is exactly CG's/BiCGSTAB's:
// ||r|| <= absoluteTolerance OR ||r|| / ||r0|| <= relativeTolerance.
//
// Valid for any nonsingular A (no symmetry or definiteness requirement) --
// the reason the linear-solver fallback policy uses it for general
// (non-symmetric, or not-provably-SPD) systems. Unlike BiCGSTAB it has no
// breakdown other than the "lucky" one (h_{j+1,j} = 0: the Krylov space
// contains the exact solution), which is treated as convergence of that
// cycle. Reported statuses:
//   Converged        -- the true residual (recomputed at every restart and
//                       at exit) satisfies the criterion;
//   MaxIterations    -- settings.maxIterations total Arnoldi steps used;
//   Breakdown        -- the small triangular least-squares system became
//                       singular (a zero pivot: A M^-1 singular on the
//                       Krylov space), never a silent NaN;
//   NonFiniteInput / NonFiniteResidual / InvalidSystem -- same meaning as
//                       for CG/BiCGSTAB.
// residualHistory: element 0 is ||r0||; element k is the Givens estimate of
// the residual after Arnoldi step k (the true residual replaces the
// estimate at each restart/exit step). Memory: (m + 1) basis vectors.
// CPU only (no GPU implementation).
class GMRES : public LinearSolver {
 public:
  // preconditioner may be nullptr, meaning no preconditioning (M = I).
  explicit GMRES(LinearSolverSettings settings,
                 std::shared_ptr<Preconditioner> preconditioner = nullptr);

  using LinearSolver::solve;

  [[nodiscard]] SolverResult solve(const LinearSystem& system,
                                   const Vector& initialGuess) const override;

 private:
  std::shared_ptr<Preconditioner> preconditioner_;
};

}  // namespace cfd::algebra
