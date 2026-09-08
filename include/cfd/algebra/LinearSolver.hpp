#pragma once

#include <vector>

#include "cfd/algebra/LinearSystem.hpp"
#include "cfd/algebra/Vector.hpp"
#include "cfd/core/Types.hpp"

namespace cfd::algebra {

// Use SolverStatus for legitimate numerical solve outcomes; use exceptions
// (InvalidArgumentError etc.) for invalid programming/configuration states
// (size mismatches, invalid tolerances, malformed CSR, ...). This keeps
// control flow clean: a caller never needs to catch an exception just to
// find out an iterative solve didn't converge.
enum class SolverStatus {
  Converged,
  MaxIterations,
  Breakdown,
  NonFiniteInput,
  NonFiniteResidual,
  InvalidSystem,
};

// residualHistory stores the absolute residual norm ||b - A x||_2 --
// element 0 is the initial residual, element k is the residual after
// iteration k, so residualHistory.size() == iterations + 1 for an
// ordinary solve. Relative residual is derived from this (against the
// initial residual) rather than stored separately.
struct SolverResult {
  Vector solution;
  SolverStatus status{SolverStatus::MaxIterations};
  Index iterations{0};
  Real initialResidual{};
  Real finalResidual{};
  std::vector<Real> residualHistory;

  [[nodiscard]] bool converged() const noexcept { return status == SolverStatus::Converged; }
};

struct LinearSolverSettings {
  Real absoluteTolerance{1e-12};
  Real relativeTolerance{1e-10};
  Index maxIterations{1000};
};

// Common base for iterative Krylov solvers (CG, BiCGSTAB). Settings are
// fixed at construction: clearer than re-passing them to every solve()
// call, since one solver instance is typically configured once and reused.
class LinearSolver {
 public:
  explicit LinearSolver(LinearSolverSettings settings);
  virtual ~LinearSolver() = default;

  [[nodiscard]] virtual SolverResult solve(const LinearSystem& system,
                                           const Vector& initialGuess) const = 0;

  // Convenience overload: solves starting from x0 = 0.
  [[nodiscard]] SolverResult solve(const LinearSystem& system) const;

  [[nodiscard]] const LinearSolverSettings& settings() const noexcept;

 protected:
  LinearSolverSettings settings_;
};

}  // namespace cfd::algebra
