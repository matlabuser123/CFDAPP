#include "cfd/algebra/CG.hpp"

#include <cmath>
#include <utility>

#include "cfd/core/Constants.hpp"
#include "cfd/core/Exception.hpp"

namespace cfd::algebra {

CG::CG(LinearSolverSettings settings, std::shared_ptr<Preconditioner> preconditioner)
    : LinearSolver(settings), preconditioner_(std::move(preconditioner)) {}

SolverResult CG::solve(const LinearSystem& system, const Vector& initialGuess) const {
  const SparseMatrix& A = system.matrix();
  const Vector& b = system.rhs();
  const Index n = system.size();

  SolverResult result;
  result.solution = initialGuess;
  result.backendUsed = LinearSolverBackend::CPU;

  if (initialGuess.size() != n || !A.allFinite() || !b.allFinite() || !initialGuess.allFinite()) {
    result.status = SolverStatus::NonFiniteInput;
    return result;
  }

  // P6-GPU-003: a preconditioner that rejects this matrix (e.g. Jacobi
  // against a zero/near-zero/non-finite diagonal) is a legitimate solve()-
  // time outcome, not an exception that should escape solve() -- the same
  // "SolverStatus for numerical outcomes, exceptions for programming
  // errors" convention this file already follows for NonFiniteInput above.
  // SolverStatus::InvalidSystem exists in LinearSolver.hpp specifically
  // for this case.
  if (preconditioner_ != nullptr) {
    try {
      preconditioner_->build(A);
    } catch (const InvalidArgumentError&) {
      result.status = SolverStatus::InvalidSystem;
      return result;
    }
  }

  Vector x = initialGuess;
  Vector r = b - A.multiply(x);

  const Real b0 = l2Norm(r);
  result.initialResidual = b0;
  result.residualHistory.push_back(b0);

  const auto converged = [&](Real absRes) {
    const bool absOk = absRes <= settings_.absoluteTolerance;
    const bool relOk = (b0 > 0.0) && (absRes / b0 <= settings_.relativeTolerance);
    return absOk || relOk;
  };

  if (b0 == 0.0 || converged(b0)) {
    result.status = SolverStatus::Converged;
    result.iterations = 0;
    result.finalResidual = b0;
    result.solution = x;
    return result;
  }

  Vector z(n);
  applyPreconditionerOrIdentity(preconditioner_.get(), r, z);
  Vector p = z;
  Real rz = dot(r, z);

  if (!std::isfinite(rz)) {
    result.status = SolverStatus::Breakdown;
    result.solution = x;
    result.finalResidual = b0;
    return result;
  }

  for (Index iter = 1; iter <= settings_.maxIterations; ++iter) {
    const Vector Ap = A.multiply(p);
    const Real pAp = dot(p, Ap);

    if (!std::isfinite(pAp) || std::abs(pAp) < constants::tiny) {
      result.status = SolverStatus::Breakdown;
      result.solution = x;
      result.finalResidual = l2Norm(r);
      result.iterations = iter - 1;
      return result;
    }

    const Real alpha = rz / pAp;
    if (!std::isfinite(alpha)) {
      result.status = SolverStatus::Breakdown;
      result.solution = x;
      result.finalResidual = l2Norm(r);
      result.iterations = iter - 1;
      return result;
    }

    x += p * alpha;
    const Vector rNew = r - (Ap * alpha);
    const Real residualNorm = l2Norm(rNew);

    if (!std::isfinite(residualNorm)) {
      result.status = SolverStatus::NonFiniteResidual;
      result.solution = x;
      result.finalResidual = residualNorm;
      result.iterations = iter;
      result.residualHistory.push_back(residualNorm);
      return result;
    }
    result.residualHistory.push_back(residualNorm);
    result.iterations = iter;

    if (converged(residualNorm)) {
      result.status = SolverStatus::Converged;
      result.solution = x;
      result.finalResidual = residualNorm;
      return result;
    }

    Vector zNew(n);
    applyPreconditionerOrIdentity(preconditioner_.get(), rNew, zNew);
    const Real rzNew = dot(rNew, zNew);

    if (!std::isfinite(rzNew)) {
      result.status = SolverStatus::Breakdown;
      result.solution = x;
      result.finalResidual = residualNorm;
      return result;
    }

    const Real beta = rzNew / rz;
    if (!std::isfinite(beta)) {
      result.status = SolverStatus::Breakdown;
      result.solution = x;
      result.finalResidual = residualNorm;
      return result;
    }

    p = zNew + (p * beta);
    r = rNew;
    rz = rzNew;
  }

  result.status = SolverStatus::MaxIterations;
  result.solution = x;
  result.finalResidual = result.residualHistory.back();
  return result;
}

}  // namespace cfd::algebra
