#include "cfd/algebra/BiCGSTAB.hpp"

#include <cmath>
#include <limits>
#include <utility>

#include "cfd/core/Exception.hpp"

namespace cfd::algebra {

namespace {

// P12-MESH-004 (results/p12-mesh-004/summary.md, "Solver robustness"): the
// breakdown tests are SCALE-RELATIVE. BiCGSTAB divides by three inner
// products -- rho = (rHat, r), (rHat, v) and (t, s) (through omega) -- and
// each vanishes at a genuine breakdown because its two vectors are
// orthogonal, not because they are small. Their magnitudes scale with the
// residual (rho, (rHat, v)) or with its square, so the former test against
// the absolute constants::tiny = 1e-30 misread a healthy iteration of a
// system whose residual was simply small (|rHat| ~ 2e-8, |v| ~ 1e-11) as a
// breakdown. What decides whether a computed inner product can be
// distinguished from zero is the rounding error of its own sum, which is
// bounded by the magnitudes of its terms, sum_i |x_i y_i| (not by its size
// relative to 1e-30, and not by |x| |y|: a sum without cancellation -- e.g.
// one dominant term -- is accurate however small it is relative to |x| |y|).
// An inner product is treated as zero when it has cancelled down to the
// rounding level of its terms:
//   |(x, y)| <= epsilon * sum_i |x_i y_i|      (epsilon = machine epsilon).
// Invariant under any scaling of the system (A, b) -> (alpha A, beta b), so
// a tiny-but-valid step is never rejected, and exact (or round-off-level)
// cancellation -- a true breakdown -- still is. Since sum_i |x_i y_i| <=
// |x| |y| (Cauchy-Schwarz), the extra pass over the terms is only needed
// when |(x, y)| <= epsilon |x| |y|, which a healthy iteration never meets.
bool cancelledToRoundingLevel(Real innerProduct, const Vector& x, const Vector& y, Real normX,
                              Real normY) {
  const Real epsilon = std::numeric_limits<Real>::epsilon();
  if (std::abs(innerProduct) > epsilon * normX * normY) return false;
  Real termMagnitudes = 0.0;
  for (Vector::size_type i = 0; i < x.size(); ++i) termMagnitudes += std::abs(x[i] * y[i]);
  return std::abs(innerProduct) <= epsilon * termMagnitudes;
}

}  // namespace

BiCGSTAB::BiCGSTAB(LinearSolverSettings settings, std::shared_ptr<Preconditioner> preconditioner)
    : LinearSolver(settings), preconditioner_(std::move(preconditioner)) {}

SolverResult BiCGSTAB::solve(const LinearSystem& system, const Vector& initialGuess) const {
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

  // P6-GPU-003: see CG.cpp's identical comment -- a preconditioner build
  // failure is a legitimate SolverStatus::InvalidSystem outcome, not an
  // exception left to escape solve().
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
  Vector rHat = r;  // the shadow residual; replaced by a restart

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

  Index completed = 0;        // completed iterations (a restart completes none)
  Real rHatNorm = b0;         // |rHat|
  Real rNorm = b0;            // |r| of the current iterate
  Real restartResidual = b0;  // |r| where the current Krylov sequence started

  const auto breakdown = [&]() {
    result.status = SolverStatus::Breakdown;
    result.solution = x;
    result.finalResidual = l2Norm(r);
    result.iterations = completed;
    return result;
  };

  // P12-MESH-004: when an inner product of the recurrence has cancelled to
  // the rounding level of its terms (a numerical breakdown of the Lanczos
  // biorthogonality, not of the iterate), the sequence is restarted from the
  // current iterate: its TRUE residual is recomputed (b - A x) and becomes
  // the new shadow residual rHat, and the recurrence coefficients are reset
  // -- the standard BiCGSTAB restart. Bounded and deterministic: a restart
  // is taken only if the residual has decreased strictly since the current
  // sequence started (so at least one iteration made progress, and a
  // breakdown in a sequence's first iteration -- a true breakdown the
  // restart cannot cure -- is reported as Breakdown), and every iteration
  // still counts against maxIterations. A restart never declares success on
  // its own: only a residual that meets the tolerance does.
  enum class Restart { Restarted, Converged, Stop };
  const auto restart = [&]() {
    if (!(rNorm < restartResidual)) return Restart::Stop;
    r = b - A.multiply(x);
    rNorm = l2Norm(r);
    if (!std::isfinite(rNorm)) return Restart::Stop;
    if (converged(rNorm)) return Restart::Converged;
    rHat = r;
    rHatNorm = rNorm;
    restartResidual = rNorm;
    rhoOld = 1.0;
    alpha = 1.0;
    omega = 1.0;
    v = Vector(n, 0.0);
    p = Vector(n, 0.0);
    ++result.restarts;
    return Restart::Restarted;
  };
  const auto restartedConvergence = [&]() {
    result.status = SolverStatus::Converged;
    result.solution = x;
    result.finalResidual = rNorm;
    result.iterations = completed;
    return result;
  };

  // Convergence is always tested before any breakdown test of the same
  // residual: r0 above, s and the updated r below, each right after it is
  // formed -- so a residual that meets the tolerance is reported Converged
  // even when the next recurrence scalar would be (numerically) zero.
  while (completed < settings_.maxIterations) {
    const Index iter = completed + 1;
    const Real rho = dot(rHat, r);
    if (!std::isfinite(rho)) return breakdown();
    if (cancelledToRoundingLevel(rho, rHat, r, rHatNorm, rNorm)) {
      const Restart outcome = restart();
      if (outcome == Restart::Restarted) continue;
      if (outcome == Restart::Converged) return restartedConvergence();
      return breakdown();
    }

    const Real beta = (rho / rhoOld) * (alpha / omega);
    if (!std::isfinite(beta)) {
      return breakdown();
    }

    p = r + (p - (v * omega)) * beta;

    Vector pHat(n);
    applyPreconditionerOrIdentity(preconditioner_.get(), p, pHat);
    v = A.multiply(pHat);

    const Real rHatDotV = dot(rHat, v);
    if (!std::isfinite(rHatDotV)) return breakdown();
    if (cancelledToRoundingLevel(rHatDotV, rHat, v, rHatNorm, l2Norm(v))) {
      const Restart outcome = restart();
      if (outcome == Restart::Restarted) continue;
      if (outcome == Restart::Converged) return restartedConvergence();
      return breakdown();
    }

    alpha = rho / rHatDotV;
    if (!std::isfinite(alpha)) {
      return breakdown();
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

    // t . t is a sum of squares (no cancellation): it is only "zero" when t
    // is -- A annihilates sHat -- or when it underflows below the normal
    // range, where it has no relative precision left.
    const Real tDotT = dot(t, t);
    if (!std::isfinite(tDotT) || tDotT < std::numeric_limits<Real>::min()) {
      return breakdown();
    }

    // omega = (t, s) / (t, t) vanishes when t is orthogonal to s: the
    // stabilizing minimal-residual step makes no progress and the next beta
    // would divide by omega.
    const Real tDotS = dot(t, s);
    if (!std::isfinite(tDotS)) return breakdown();
    if (cancelledToRoundingLevel(tDotS, t, s, std::sqrt(tDotT), sNorm)) {
      const Restart outcome = restart();
      if (outcome == Restart::Restarted) continue;
      if (outcome == Restart::Converged) return restartedConvergence();
      return breakdown();
    }
    omega = tDotS / tDotT;
    if (!std::isfinite(omega)) {
      return breakdown();
    }

    x += pHat * alpha;
    x += sHat * omega;
    r = s - (t * omega);

    const Real residualNorm = l2Norm(r);
    rNorm = residualNorm;
    if (!std::isfinite(residualNorm)) {
      result.status = SolverStatus::NonFiniteResidual;
      result.solution = x;
      result.finalResidual = residualNorm;
      result.iterations = iter;
      result.residualHistory.push_back(residualNorm);
      return result;
    }
    completed = iter;
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
