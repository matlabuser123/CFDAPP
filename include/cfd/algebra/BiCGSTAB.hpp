#pragma once

#include <memory>

#include "cfd/algebra/LinearSolver.hpp"
#include "cfd/algebra/Preconditioner.hpp"

namespace cfd::algebra {

// Preconditioned BiCGSTAB. Handles general (non-symmetric) sparse
// systems -- momentum/convection-diffusion equations where CG's SPD
// requirement does not hold.
//
// Breakdown handling (P12-MESH-004). An inner product of the recurrence --
// (rHat, r), (rHat, v), (t, s) -- is numerically zero when it has cancelled
// to the rounding level of its own terms (|(x, y)| <= epsilon sum |x_i y_i|);
// the test is scale-invariant, so a system whose residual is merely small is
// never treated as broken down. On such a numerical breakdown the Krylov
// sequence is restarted from the current iterate (rHat = its recomputed true
// residual) if the residual decreased since the sequence started; otherwise
// -- a breakdown in a sequence's first iteration, which a restart cannot
// cure -- SolverStatus::Breakdown is reported, as it is when t = A sHat is
// numerically zero (t . t below the normal range) or a recurrence scalar is
// not finite. SolverResult::restarts counts the restarts; every iteration
// counts against maxIterations. A residual that meets the tolerance is
// always reported Converged first; a restart never reports success by
// itself.
class BiCGSTAB : public LinearSolver {
 public:
  // preconditioner may be nullptr, meaning no preconditioning (M = I).
  explicit BiCGSTAB(LinearSolverSettings settings,
                    std::shared_ptr<Preconditioner> preconditioner = nullptr);

  // Brings LinearSolver::solve(const LinearSystem&) back into scope --
  // otherwise the override below would hide it.
  using LinearSolver::solve;

  [[nodiscard]] SolverResult solve(const LinearSystem& system,
                                   const Vector& initialGuess) const override;

 private:
  std::shared_ptr<Preconditioner> preconditioner_;
};

}  // namespace cfd::algebra
