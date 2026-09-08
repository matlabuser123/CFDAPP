#pragma once

#include <memory>

#include "cfd/algebra/LinearSolver.hpp"
#include "cfd/algebra/Preconditioner.hpp"

namespace cfd::algebra {

// Preconditioned Conjugate Gradient. Requires A to be symmetric
// positive-definite -- CFDApp does not attempt to verify this (an
// expensive check to do robustly); use CG only on matrices known to be
// SPD by construction (e.g. certain diffusion/pressure discretizations).
class CG : public LinearSolver {
 public:
  // preconditioner may be nullptr, meaning no preconditioning (M = I).
  explicit CG(LinearSolverSettings settings,
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
