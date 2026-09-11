// P6-GPU-002 -- Performance: the real, CUDA-backed GpuCG/GpuBiCGSTAB
// implementations -- see include/cfd/gpu/GpuLinearSolver.hpp's own
// header comment for the ODR split with src/gpu/GpuLinearSolver.cpp's
// CPU stub (both factories there always return nullptr). A plain .cpp
// (host-compiled, not nvcc) for the same reason
// cuda/kernels/GpuResidencyManagerCuda.cpp is one -- no __global__ kernel
// of its own here, only calls into the kernels DeviceVectorOpsKernel.cu
// and CsrSpmvKernel.cu already define, and this project's nvcc 11.5
// cannot parse this GCC 11 toolchain's <functional>/<memory> template
// internals reliably (see GpuResidencyManagerCuda's own header comment
// for the discovered incompatibility) -- <memory> for
// std::unique_ptr/make_unique is unavoidable here, so this stays a
// g++-compiled .cpp, still linked into the cfdcuda target.
//
// Both classes below are declared and defined entirely in this
// translation unit -- deliberately no public header names them (only
// makeGpuCG()/makeGpuBiCGSTAB()'s return type, the existing
// CUDA-independent cfd::algebra::LinearSolver base, is ever visible
// outside this file). Each mirrors its CPU counterpart
// (src/algebra/CG.cpp / BiCGSTAB.cpp) instruction-for-instruction --
// same convergence formula, same breakdown thresholds, same status
// values -- the only difference is every Vector lives in persistent
// device memory (DeviceVector, reused across repeated solve() calls on
// the same instance -- P6-GPU-001's pipeline) and every vector operation
// (waxpby/axpy/dot/l2Norm/fill/deviceCopy, DeviceVectorOps.hpp) runs on
// the GPU instead of the host. Where the CPU version names a fresh local
// (rNew, zNew, ...), the GPU version instead overwrites the
// already-persistent buffer in place -- always safe for these
// elementwise/reduction kernels since none of them has a cross-index
// dependency, and it is exactly what lets repeated solve() calls avoid
// reallocating: see this file's own per-class header comment for exactly
// which buffer plays which CPU-algorithm role.
#include <cmath>
#include <cstdint>
#include <memory>
#include <utility>

#include "cfd/algebra/LinearSystem.hpp"
#include "cfd/algebra/Preconditioner.hpp"
#include "cfd/algebra/SparseMatrix.hpp"
#include "cfd/algebra/Vector.hpp"
#include "cfd/core/Constants.hpp"
#include "cfd/core/Exception.hpp"
#include "cfd/core/Timer.hpp"
#include "cfd/gpu/DeviceCsrMatrix.hpp"
#include "cfd/gpu/DeviceVectorOps.hpp"
#include "cfd/gpu/GPUBackend.hpp"
#include "cfd/gpu/GPUExecutionStats.hpp"
#include "cfd/gpu/GpuLinearSolver.hpp"
#include "cfd/gpu/GpuPreconditioner.hpp"

namespace cfd::gpu {

namespace {

using cfd::Index;
using cfd::Real;
using cfd::algebra::LinearSolver;
using cfd::algebra::LinearSolverBackend;
using cfd::algebra::LinearSolverSettings;
using cfd::algebra::LinearSystem;
using cfd::algebra::Preconditioner;
using cfd::algebra::PreconditionerType;
using cfd::algebra::SolverResult;
using cfd::algebra::SolverStatus;
using cfd::algebra::SparseMatrix;
using cfd::algebra::Vector;

// Shared by GpuCG/GpuBiCGSTAB: uploads matrix structure once per
// distinct structure, value-only re-upload thereafter -- the exact same
// auto-detection GpuResidencyManager's own syncMatrix() uses (P6-GPU-001
// -- see that file's own header comment).
void syncMatrix(DeviceCsrMatrix& deviceMatrix, const SparseMatrix& matrix) {
  if (!deviceMatrix.hasStructure() || deviceMatrix.rows() != matrix.rows() ||
      deviceMatrix.columns() != matrix.columns() || deviceMatrix.nonZeros() != matrix.nonZeros()) {
    deviceMatrix.uploadStructureAndValues(matrix);
  } else {
    deviceMatrix.updateValues(matrix);
  }
}

// Applies the active preconditioner (or the identity) to `input`, writing
// into `output`. Three cases, checked in order:
//
//   1. useGpuJacobi: settings requested PreconditionerType::Jacobi and no
//      explicit CPU-side preconditioner was supplied (see solveImpl's own
//      build-step comment) -- applies GpuPreconditioner.hpp's device-
//      resident diag(A)^-1 entirely on the GPU, no host round trip at
//      all. This is P6-GPU-003's actual production fast path.
//   2. preconditioner != nullptr: an explicit CPU-side
//      cfd::algebra::Preconditioner the caller constructed directly
//      (e.g. a test injecting a custom preconditioner, or JacobiPrecon-
//      ditioner passed in by hand rather than selected via settings).
//      Preconditioner::apply() takes host Vectors by interface, so this
//      is the one remaining path that pays a real per-iteration
//      device<->host round trip -- of exactly the one vector being
//      preconditioned, not the whole iterative state. Kept as a general-
//      purpose escape hatch for a preconditioner this codebase has no
//      GPU-resident implementation of; not the production default.
//   3. neither: identity (M = I), unchanged pre-P6-GPU-003 behavior.
void applyPreconditioner(const Preconditioner* preconditioner, bool useGpuJacobi,
                         const DeviceVector& jacobiInverseDiagonal, const DeviceVector& input,
                         DeviceVector& output) {
  if (useGpuJacobi) {
    applyJacobiDiagonal(jacobiInverseDiagonal, input, output);
    return;
  }
  if (preconditioner == nullptr) {
    deviceCopy(output, input);
    return;
  }
  const Vector hostInput = input.downloadToVector();
  Vector hostOutput(hostInput.size());
  preconditioner->apply(hostInput, hostOutput);
  output.uploadFrom(hostOutput);
}

// Shared by GpuCG/GpuBiCGSTAB's own solveImpl: builds whichever
// preconditioner state `settings`/`preconditioner` select, returning
// true if the build failed and the caller must report
// SolverStatus::InvalidSystem and stop (mirrors CG.cpp/BiCGSTAB.cpp's own
// identical CPU-side try/catch around Preconditioner::build()). On
// success, sets `useGpuJacobi` to whether the fast device-resident Jacobi
// path is now active (mutually exclusive with a non-null `preconditioner`
// -- an explicit CPU-side preconditioner always takes priority over
// `settings.preconditioner`, matching LinearSolverFactory.cpp's own
// "explicit argument wins" contract).
[[nodiscard]] bool buildPreconditionerFailed(Preconditioner* preconditioner,
                                             const LinearSolverSettings& settings,
                                             const SparseMatrix& A,
                                             DeviceVector& jacobiInverseDiagonal,
                                             bool& useGpuJacobi) {
  useGpuJacobi = false;
  if (preconditioner != nullptr) {
    try {
      preconditioner->build(A);
    } catch (const cfd::InvalidArgumentError&) {
      return true;
    }
    return false;
  }
  if (settings.preconditioner == PreconditionerType::Jacobi) {
    try {
      buildJacobiDiagonal(A, jacobiInverseDiagonal);
    } catch (const cfd::InvalidArgumentError&) {
      return true;
    }
    useGpuJacobi = true;
  }
  return false;
}

// ---------------------------------------------------------------------
// GpuCG
// ---------------------------------------------------------------------

// Device-buffer roles mirror CG.cpp's own locals exactly: x_/r_/p_ are
// the algorithm's own persistent x/r/p; ap_ holds A*p (CPU's `Ap`); z_
// holds the current preconditioned residual (CPU's `z`, later
// overwritten in place to become `zNew` -- old z is never needed again
// once rz has been read into the host-side `Real rz`).
class GpuCG final : public LinearSolver {
 public:
  explicit GpuCG(LinearSolverSettings settings, std::shared_ptr<Preconditioner> preconditioner)
      : LinearSolver(settings), preconditioner_(std::move(preconditioner)) {}

  using LinearSolver::solve;

  [[nodiscard]] SolverResult solve(const LinearSystem& system,
                                   const Vector& initialGuess) const override {
    cfd::Timer solveTimer;
    SolverResult result = solveImpl(system, initialGuess);
    auto& stats = gpuExecutionStats();
    stats.gpuSolveSeconds += solveTimer.elapsedSeconds();
    ++stats.gpuLinearSolves;
    stats.gpuLinearSolverIterations += static_cast<std::uint64_t>(result.iterations);
    return result;
  }

 private:
  [[nodiscard]] SolverResult solveImpl(const LinearSystem& system, const Vector& initialGuess) const {
    const SparseMatrix& A = system.matrix();
    const Vector& b = system.rhs();
    const Index n = system.size();

    SolverResult result;
    result.solution = initialGuess;
    result.backendUsed = LinearSolverBackend::GPU;

    if (initialGuess.size() != n || !A.allFinite() || !b.allFinite() || !initialGuess.allFinite()) {
      result.status = SolverStatus::NonFiniteInput;
      return result;
    }

    syncMatrix(deviceMatrix_, A);
    bool useGpuJacobi = false;
    if (buildPreconditionerFailed(preconditioner_.get(), settings_, A, jacobiInverseDiagonal_,
                                  useGpuJacobi)) {
      result.status = SolverStatus::InvalidSystem;
      return result;
    }

    // Every persistent buffer must already be the right size *before*
    // it is first used as an spmv() output target -- spmv() itself
    // throws InvalidArgumentError on a size mismatch rather than
    // resizing its own output (DeviceCsrMatrix.hpp's own contract).
    // resize() is a no-op once a buffer is already this size (P6-GPU-001
    // -- DeviceBuffer::resize()'s own contract), so this costs nothing
    // on repeated solve() calls.
    x_.resize(n);
    b_.resize(n);
    r_.resize(n);
    p_.resize(n);
    ap_.resize(n);
    z_.resize(n);

    b_.uploadFrom(b);
    x_.uploadFrom(initialGuess);

    spmv(deviceMatrix_, x_, ap_);          // ap_ := A*x0 (temporary reuse)
    waxpby(1.0, b_, -1.0, ap_, r_);        // r_ := b - A*x0

    const Real b0 = l2Norm(r_);
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
      result.solution = x_.downloadToVector();
      return result;
    }

    applyPreconditioner(preconditioner_.get(), useGpuJacobi, jacobiInverseDiagonal_, r_, z_);
    deviceCopy(p_, z_);
    Real rz = dot(r_, z_);

    if (!std::isfinite(rz)) {
      result.status = SolverStatus::Breakdown;
      result.solution = x_.downloadToVector();
      result.finalResidual = b0;
      return result;
    }

    for (Index iter = 1; iter <= settings_.maxIterations; ++iter) {
      spmv(deviceMatrix_, p_, ap_);
      const Real pAp = dot(p_, ap_);

      if (!std::isfinite(pAp) || std::abs(pAp) < constants::tiny) {
        result.status = SolverStatus::Breakdown;
        result.solution = x_.downloadToVector();
        result.finalResidual = l2Norm(r_);
        result.iterations = iter - 1;
        return result;
      }

      const Real alpha = rz / pAp;
      if (!std::isfinite(alpha)) {
        result.status = SolverStatus::Breakdown;
        result.solution = x_.downloadToVector();
        result.finalResidual = l2Norm(r_);
        result.iterations = iter - 1;
        return result;
      }

      axpy(alpha, p_, x_);    // x_ += alpha * p_
      axpy(-alpha, ap_, r_);  // r_ -= alpha * Ap  (becomes r_new in place)
      const Real residualNorm = l2Norm(r_);

      if (!std::isfinite(residualNorm)) {
        result.status = SolverStatus::NonFiniteResidual;
        result.solution = x_.downloadToVector();
        result.finalResidual = residualNorm;
        result.iterations = iter;
        result.residualHistory.push_back(residualNorm);
        return result;
      }
      result.residualHistory.push_back(residualNorm);
      result.iterations = iter;

      if (converged(residualNorm)) {
        result.status = SolverStatus::Converged;
        result.solution = x_.downloadToVector();
        result.finalResidual = residualNorm;
        return result;
      }

      applyPreconditioner(preconditioner_.get(), useGpuJacobi, jacobiInverseDiagonal_, r_,
                         z_);  // z_ becomes z_new in place
      const Real rzNew = dot(r_, z_);

      if (!std::isfinite(rzNew)) {
        result.status = SolverStatus::Breakdown;
        result.solution = x_.downloadToVector();
        result.finalResidual = residualNorm;
        return result;
      }

      const Real beta = rzNew / rz;
      if (!std::isfinite(beta)) {
        result.status = SolverStatus::Breakdown;
        result.solution = x_.downloadToVector();
        result.finalResidual = residualNorm;
        return result;
      }

      waxpby(1.0, z_, beta, p_, p_);  // p_ := z_ + beta * p_  (old p_ read then overwritten)
      rz = rzNew;
    }

    result.status = SolverStatus::MaxIterations;
    result.solution = x_.downloadToVector();
    result.finalResidual = result.residualHistory.back();
    return result;
  }

  std::shared_ptr<Preconditioner> preconditioner_;
  mutable DeviceCsrMatrix deviceMatrix_;
  mutable DeviceVector x_, b_, r_, p_, ap_, z_;
  // P6-GPU-003: device-resident diag(A)^-1, reused across repeated
  // solve() calls (rebuilt once per call when settings_.preconditioner
  // == Jacobi -- see buildPreconditionerFailed's own header comment) --
  // never reallocated mid-solve, only ever mid-iteration-loop reused via
  // applyJacobiDiagonal().
  mutable DeviceVector jacobiInverseDiagonal_;
};

// ---------------------------------------------------------------------
// GpuBiCGSTAB
// ---------------------------------------------------------------------

// Device-buffer roles mirror BiCGSTAB.cpp's own locals: x_/r_/p_/v_/s_/
// t_ are the algorithm's own x/r/p/v/s/t; rHat_ is the fixed shadow
// residual (uploaded once, never mutated for the rest of the solve());
// pHat_/sHat_ hold the current preconditioned p/s (CPU's `pHat`/`sHat`).
class GpuBiCGSTAB final : public LinearSolver {
 public:
  explicit GpuBiCGSTAB(LinearSolverSettings settings, std::shared_ptr<Preconditioner> preconditioner)
      : LinearSolver(settings), preconditioner_(std::move(preconditioner)) {}

  using LinearSolver::solve;

  [[nodiscard]] SolverResult solve(const LinearSystem& system,
                                   const Vector& initialGuess) const override {
    cfd::Timer solveTimer;
    SolverResult result = solveImpl(system, initialGuess);
    auto& stats = gpuExecutionStats();
    stats.gpuSolveSeconds += solveTimer.elapsedSeconds();
    ++stats.gpuLinearSolves;
    stats.gpuLinearSolverIterations += static_cast<std::uint64_t>(result.iterations);
    return result;
  }

 private:
  [[nodiscard]] SolverResult solveImpl(const LinearSystem& system, const Vector& initialGuess) const {
    const SparseMatrix& A = system.matrix();
    const Vector& b = system.rhs();
    const Index n = system.size();

    SolverResult result;
    result.solution = initialGuess;
    result.backendUsed = LinearSolverBackend::GPU;

    if (initialGuess.size() != n || !A.allFinite() || !b.allFinite() || !initialGuess.allFinite()) {
      result.status = SolverStatus::NonFiniteInput;
      return result;
    }

    syncMatrix(deviceMatrix_, A);
    bool useGpuJacobi = false;
    if (buildPreconditionerFailed(preconditioner_.get(), settings_, A, jacobiInverseDiagonal_,
                                  useGpuJacobi)) {
      result.status = SolverStatus::InvalidSystem;
      return result;
    }

    // See GpuCG::solveImpl's identical comment -- every persistent
    // buffer must be correctly sized before its first use as an spmv()
    // output target, and this resize is a no-op on repeated calls.
    x_.resize(n);
    b_.resize(n);
    r_.resize(n);
    rHat_.resize(n);
    p_.resize(n);
    v_.resize(n);
    s_.resize(n);
    t_.resize(n);
    pHat_.resize(n);
    sHat_.resize(n);

    b_.uploadFrom(b);
    x_.uploadFrom(initialGuess);

    spmv(deviceMatrix_, x_, v_);      // v_ temporarily holds A*x0
    waxpby(1.0, b_, -1.0, v_, r_);    // r_ := b - A*x0
    deviceCopy(rHat_, r_);            // rHat_ fixed for the rest of this solve

    const Real b0 = l2Norm(r_);
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
      result.solution = x_.downloadToVector();
      return result;
    }

    Real rhoOld = 1.0;
    Real alpha = 1.0;
    Real omega = 1.0;
    fill(v_, n, 0.0);
    fill(p_, n, 0.0);

    const auto breakdown = [&](Index completedIterations) {
      result.status = SolverStatus::Breakdown;
      result.solution = x_.downloadToVector();
      result.finalResidual = l2Norm(r_);
      result.iterations = completedIterations;
      return result;
    };

    for (Index iter = 1; iter <= settings_.maxIterations; ++iter) {
      const Real rho = dot(rHat_, r_);
      if (!std::isfinite(rho) || std::abs(rho) < constants::tiny) {
        return breakdown(iter - 1);
      }

      const Real beta = (rho / rhoOld) * (alpha / omega);
      if (!std::isfinite(beta)) {
        return breakdown(iter - 1);
      }

      waxpby(1.0, p_, -omega, v_, p_);  // p_ := p_ - omega * v_
      waxpby(1.0, r_, beta, p_, p_);    // p_ := r_ + beta * p_

      applyPreconditioner(preconditioner_.get(), useGpuJacobi, jacobiInverseDiagonal_, p_, pHat_);
      spmv(deviceMatrix_, pHat_, v_);

      const Real rHatDotV = dot(rHat_, v_);
      if (!std::isfinite(rHatDotV) || std::abs(rHatDotV) < constants::tiny) {
        return breakdown(iter - 1);
      }

      alpha = rho / rHatDotV;
      if (!std::isfinite(alpha)) {
        return breakdown(iter - 1);
      }

      waxpby(1.0, r_, -alpha, v_, s_);  // s_ := r_ - alpha * v_
      const Real sNorm = l2Norm(s_);

      if (!std::isfinite(sNorm)) {
        result.status = SolverStatus::NonFiniteResidual;
        result.solution = x_.downloadToVector();
        result.finalResidual = sNorm;
        result.iterations = iter;
        result.residualHistory.push_back(sNorm);
        return result;
      }

      if (converged(sNorm)) {
        axpy(alpha, pHat_, x_);
        result.status = SolverStatus::Converged;
        result.solution = x_.downloadToVector();
        result.finalResidual = sNorm;
        result.iterations = iter;
        result.residualHistory.push_back(sNorm);
        return result;
      }

      applyPreconditioner(preconditioner_.get(), useGpuJacobi, jacobiInverseDiagonal_, s_, sHat_);
      spmv(deviceMatrix_, sHat_, t_);

      const Real tDotT = dot(t_, t_);
      if (!std::isfinite(tDotT) || tDotT < constants::tiny) {
        return breakdown(iter - 1);
      }

      omega = dot(t_, s_) / tDotT;
      if (!std::isfinite(omega) || std::abs(omega) < constants::tiny) {
        return breakdown(iter - 1);
      }

      axpy(alpha, pHat_, x_);
      axpy(omega, sHat_, x_);
      waxpby(1.0, s_, -omega, t_, r_);  // r_ := s_ - omega * t_

      const Real residualNorm = l2Norm(r_);
      if (!std::isfinite(residualNorm)) {
        result.status = SolverStatus::NonFiniteResidual;
        result.solution = x_.downloadToVector();
        result.finalResidual = residualNorm;
        result.iterations = iter;
        result.residualHistory.push_back(residualNorm);
        return result;
      }
      result.residualHistory.push_back(residualNorm);
      result.iterations = iter;

      if (converged(residualNorm)) {
        result.status = SolverStatus::Converged;
        result.solution = x_.downloadToVector();
        result.finalResidual = residualNorm;
        return result;
      }

      rhoOld = rho;
    }

    result.status = SolverStatus::MaxIterations;
    result.solution = x_.downloadToVector();
    result.finalResidual = result.residualHistory.back();
    return result;
  }

  std::shared_ptr<Preconditioner> preconditioner_;
  mutable DeviceCsrMatrix deviceMatrix_;
  mutable DeviceVector x_, b_, r_, rHat_, p_, v_, s_, t_, pHat_, sHat_;
  // P6-GPU-003: see GpuCG's identical member comment.
  mutable DeviceVector jacobiInverseDiagonal_;
};

}  // namespace

std::unique_ptr<LinearSolver> makeGpuCG(LinearSolverSettings settings,
                                        std::shared_ptr<Preconditioner> preconditioner) {
  if (!cudaAvailable()) return nullptr;
  return std::make_unique<GpuCG>(settings, std::move(preconditioner));
}

std::unique_ptr<LinearSolver> makeGpuBiCGSTAB(LinearSolverSettings settings,
                                              std::shared_ptr<Preconditioner> preconditioner) {
  if (!cudaAvailable()) return nullptr;
  return std::make_unique<GpuBiCGSTAB>(settings, std::move(preconditioner));
}

}  // namespace cfd::gpu
