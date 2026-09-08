#include "cfd/algebra/BiCGSTAB.hpp"

#include <cmath>
#include <utility>

#include "cfd/core/Constants.hpp"

namespace cfd::algebra {

BiCGSTAB::BiCGSTAB(LinearSolverSettings settings, std::shared_ptr<Preconditioner> preconditioner)
    : LinearSolver(settings), preconditioner_(std::move(preconditioner)) {}

SolverResult BiCGSTAB::solve(const LinearSystem& system, const Vector& initialGuess) const {
  const SparseMatrix& A = system.matrix();
  const Vector& b = system.rhs();
  const Index n = system.size();

  SolverResult result;
  result.solution = initialGuess;

  if (initialGuess.size() != n || !A.allFinite() || !b.allFinite() || !initialGuess.allFinite()) {
    result.status = SolverStatus::NonFiniteInput;
    return result;
  }

  if (preconditioner_ != nullptr) {
    preconditioner_->build(A);
  }

  Vector x = initialGuess;
  Vector r = b - A.multiply(x);
  const Vector rHat = r;

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

  Real rhoOld = 1.0;
  Real alpha = 1.0;
  Real omega = 1.0;
  Vector v(n, 0.0);
  Vector p(n, 0.0);

  const auto breakdown = [&](Index completedIterations) {
    result.status = SolverStatus::Breakdown;
    result.solution = x;
    result.finalResidual = l2Norm(r);
    result.iterations = completedIterations;
    return result;
  };

  for (Index iter = 1; iter <= settings_.maxIterations; ++iter) {
    const Real rho = dot(rHat, r);
    if (!std::isfinite(rho) || std::abs(rho) < constants::tiny) {
      return breakdown(iter - 1);
    }

    const Real beta = (rho / rhoOld) * (alpha / omega);
    if (!std::isfinite(beta)) {
      return breakdown(iter - 1);
    }

    p = r + (p - (v * omega)) * beta;

    Vector pHat(n);
    applyPreconditionerOrIdentity(preconditioner_.get(), p, pHat);
    v = A.multiply(pHat);

    const Real rHatDotV = dot(rHat, v);
    if (!std::isfinite(rHatDotV) || std::abs(rHatDotV) < constants::tiny) {
      return breakdown(iter - 1);
    }

    alpha = rho / rHatDotV;
    if (!std::isfinite(alpha)) {
      return breakdown(iter - 1);
    }

    const Vector s = r - (v * alpha);
    const Real sNorm = l2Norm(s);

    if (!std::isfinite(sNorm)) {
      result.status = SolverStatus::NonFiniteResidual;
      result.solution = x;
      result.finalResidual = sNorm;
      result.iterations = iter;
      result.residualHistory.push_back(sNorm);
      return result;
    }

    if (converged(sNorm)) {
      x += pHat * alpha;
      result.status = SolverStatus::Converged;
      result.solution = x;
      result.finalResidual = sNorm;
      result.iterations = iter;
      result.residualHistory.push_back(sNorm);
      return result;
    }

    Vector sHat(n);
    applyPreconditionerOrIdentity(preconditioner_.get(), s, sHat);
    const Vector t = A.multiply(sHat);

    const Real tDotT = dot(t, t);
    if (!std::isfinite(tDotT) || tDotT < constants::tiny) {
      return breakdown(iter - 1);
    }

    omega = dot(t, s) / tDotT;
    if (!std::isfinite(omega) || std::abs(omega) < constants::tiny) {
      return breakdown(iter - 1);
    }

    x += pHat * alpha;
    x += sHat * omega;
    r = s - (t * omega);

    const Real residualNorm = l2Norm(r);
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

    rhoOld = rho;
  }

  result.status = SolverStatus::MaxIterations;
  result.solution = x;
  result.finalResidual = result.residualHistory.back();
  return result;
}

}  // namespace cfd::algebra
