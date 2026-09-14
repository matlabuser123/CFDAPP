#include "cfd/algebra/GMRES.hpp"

#include <algorithm>
#include <cmath>
#include <utility>
#include <vector>

#include "cfd/core/Exception.hpp"

namespace cfd::algebra {

GMRES::GMRES(LinearSolverSettings settings, std::shared_ptr<Preconditioner> preconditioner)
    : LinearSolver(settings), preconditioner_(std::move(preconditioner)) {}

SolverResult GMRES::solve(const LinearSystem& system, const Vector& initialGuess) const {
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

  // Same policy as CG/BiCGSTAB: a preconditioner build failure is a
  // SolverStatus::InvalidSystem outcome, not an escaping exception.
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
  Real beta = l2Norm(r);
  const Real r0 = beta;
  result.initialResidual = r0;
  result.residualHistory.push_back(r0);

  const auto converged = [&](Real absRes) {
    const bool absOk = absRes <= settings_.absoluteTolerance;
    const bool relOk = (r0 > 0.0) && (absRes / r0 <= settings_.relativeTolerance);
    return absOk || relOk;
  };

  if (r0 == 0.0 || converged(r0)) {
    result.status = SolverStatus::Converged;
    result.finalResidual = r0;
    result.solution = x;
    return result;
  }

  const Index m = std::min<Index>(settings_.gmresRestart, n);
  std::vector<Vector> basis;
  basis.reserve(m + 1);
  std::vector<std::vector<Real>> h(m + 1, std::vector<Real>(m, 0.0));
  std::vector<Real> cs(m, 0.0);
  std::vector<Real> sn(m, 0.0);
  std::vector<Real> g(m + 1, 0.0);

  const auto finish = [&](SolverStatus status, Real finalResidual, Index iterations) {
    result.status = status;
    result.solution = x;
    result.finalResidual = finalResidual;
    result.iterations = iterations;
    return result;
  };

  Index total = 0;
  while (total < settings_.maxIterations) {
    basis.clear();
    basis.push_back(r / beta);
    std::fill(g.begin(), g.end(), 0.0);
    g[0] = beta;
    for (auto& row : h) std::fill(row.begin(), row.end(), 0.0);

    Index k = 0;
    for (Index j = 0; j < m && total < settings_.maxIterations; ++j) {
      Vector z(n);
      applyPreconditionerOrIdentity(preconditioner_.get(), basis[j], z);
      Vector w = A.multiply(z);
      // Modified Gram-Schmidt against the current basis.
      for (Index i = 0; i <= j; ++i) {
        h[i][j] = dot(w, basis[i]);
        w -= basis[i] * h[i][j];
      }
      const Real hNext = l2Norm(w);
      h[j + 1][j] = hNext;

      // Apply the previous Givens rotations to the new column, then form
      // the rotation that annihilates h[j+1][j].
      for (Index i = 0; i < j; ++i) {
        const Real upper = (cs[i] * h[i][j]) + (sn[i] * h[i + 1][j]);
        h[i + 1][j] = (-sn[i] * h[i][j]) + (cs[i] * h[i + 1][j]);
        h[i][j] = upper;
      }
      const Real diagonal = h[j][j];
      const Real sub = h[j + 1][j];
      const Real radius = std::hypot(diagonal, sub);
      if (radius == 0.0) {
        cs[j] = 1.0;
        sn[j] = 0.0;
      } else {
        cs[j] = diagonal / radius;
        sn[j] = sub / radius;
      }
      h[j][j] = (cs[j] * diagonal) + (sn[j] * sub);
      h[j + 1][j] = 0.0;
      g[j + 1] = -sn[j] * g[j];
      g[j] = cs[j] * g[j];

      ++total;
      k = j + 1;
      const Real estimate = std::abs(g[j + 1]);
      if (!std::isfinite(estimate) || !std::isfinite(hNext)) {
        result.residualHistory.push_back(estimate);
        return finish(SolverStatus::NonFiniteResidual, estimate, total);
      }
      result.residualHistory.push_back(estimate);
      // hNext == 0: "lucky" breakdown -- the Krylov space already holds the
      // exact solution of this cycle; stop the cycle and update x.
      if (hNext == 0.0 || converged(estimate)) {
        break;
      }
      basis.push_back(w / hNext);
    }

    // Back-substitution for the k x k upper-triangular least-squares system.
    std::vector<Real> y(k, 0.0);
    for (Index ii = k; ii-- > 0;) {
      Real sum = g[ii];
      for (Index jj = ii + 1; jj < k; ++jj) sum -= h[ii][jj] * y[jj];
      if (h[ii][ii] == 0.0) {
        return finish(SolverStatus::Breakdown, beta, total);
      }
      y[ii] = sum / h[ii][ii];
      if (!std::isfinite(y[ii])) {
        return finish(SolverStatus::Breakdown, beta, total);
      }
    }
    Vector combination(n, 0.0);
    for (Index ii = 0; ii < k; ++ii) combination += basis[ii] * y[ii];
    Vector correction(n);
    applyPreconditionerOrIdentity(preconditioner_.get(), combination, correction);
    x += correction;

    // Restart (or exit) from the TRUE residual, replacing the last estimate.
    r = b - A.multiply(x);
    beta = l2Norm(r);
    result.residualHistory.back() = beta;
    if (!std::isfinite(beta)) {
      return finish(SolverStatus::NonFiniteResidual, beta, total);
    }
    if (converged(beta)) {
      return finish(SolverStatus::Converged, beta, total);
    }
  }

  return finish(SolverStatus::MaxIterations, beta, total);
}

}  // namespace cfd::algebra
