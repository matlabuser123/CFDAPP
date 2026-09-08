#pragma once

#include <memory>

#include "cfd/algebra/LinearSolver.hpp"
#include "cfd/algebra/Preconditioner.hpp"

namespace cfd::algebra {

// Preconditioned BiCGSTAB. Handles general (non-symmetric) sparse
// systems -- momentum/convection-diffusion equations where CG's SPD
// requirement does not hold.
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
