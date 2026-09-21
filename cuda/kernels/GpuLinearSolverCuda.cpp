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
#include <limits>
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
#include "cfd/gpu/GpuResidentSolve.hpp"

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
// GPU-PIPE-001: the BiCGSTAB iteration itself, shared by the host entry point
// (GpuBiCGSTAB::solveImpl, below) and the device-resident one
// (solveBiCGSTABResident). Defined after the class so the class body reads as
// it always did.
//
// The parameter names carry the trailing underscores of the members they
// replaced -- deliberately, so the lifted algorithm body is the ORIGINAL text,
// not a retyping of it. Reviewing this change means checking that the body was
// moved, and identical names make that check mechanical.
[[nodiscard]] SolverResult bicgstabCore(const DeviceCsrMatrix& deviceMatrix_,
                                        const LinearSolverSettings& settings_,
                                        Preconditioner* preconditionerRaw, bool useGpuJacobi,
                                        GpuKrylovWorkspace& ws, Index n);

// ---------------------------------------------------------------------
// GpuBiCGSTAB
// ---------------------------------------------------------------------
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
    if (buildPreconditionerFailed(preconditioner_.get(), settings_, A,
                                  workspace_.jacobiInverseDiagonal, useGpuJacobi)) {
      result.status = SolverStatus::InvalidSystem;
      return result;
    }

    // See GpuCG::solveImpl's identical comment -- every persistent
    // buffer must be correctly sized before its first use as an spmv()
    // output target, and this resize is a no-op on repeated calls.
    workspace_.resize(n);

    workspace_.b.uploadFrom(b);
    workspace_.x.uploadFrom(initialGuess);

    // GPU-PIPE-001: the algorithm itself lives in bicgstabCore, shared with the
    // device-resident entry point. It never touches the host, so this is the
    // ONLY place the solution is downloaded -- previously every one of the
    // eleven exit paths did its own identical download.
    result = bicgstabCore(deviceMatrix_, settings_, preconditioner_.get(), useGpuJacobi,
                          workspace_, n);
    result.solution = workspace_.x.downloadToVector();
    result.backendUsed = LinearSolverBackend::GPU;
    return result;
  }

  std::shared_ptr<Preconditioner> preconditioner_;
  mutable DeviceCsrMatrix deviceMatrix_;
  mutable GpuKrylovWorkspace workspace_;
};

// ---------------------------------------------------------------------
// The BiCGSTAB iteration, device-only.
// ---------------------------------------------------------------------
//
// Lifted verbatim out of GpuBiCGSTAB::solveImpl: same operations in the same
// order, same fused reductions, same breakdown tests, same convergence test.
// The single change is that it no longer downloads the solution -- `x` is left
// resident and each entry point decides what to do with it.
SolverResult bicgstabCore(const DeviceCsrMatrix& deviceMatrix_, const LinearSolverSettings& settings_,
                          Preconditioner* preconditionerRaw, bool useGpuJacobi,
                          GpuKrylovWorkspace& ws, Index n) {
  DeviceVector& x_ = ws.x;
  DeviceVector& b_ = ws.b;
  DeviceVector& r_ = ws.r;
  DeviceVector& rHat_ = ws.rHat;
  DeviceVector& p_ = ws.p;
  DeviceVector& v_ = ws.v;
  DeviceVector& s_ = ws.s;
  DeviceVector& t_ = ws.t;
  DeviceVector& pHat_ = ws.pHat;
  DeviceVector& sHat_ = ws.sHat;
  DeviceVector& jacobiInverseDiagonal_ = ws.jacobiInverseDiagonal;

  SolverResult result;
  result.backendUsed = LinearSolverBackend::GPU;
    spmv(deviceMatrix_, x_, v_);      // v_ temporarily holds A*x0
    waxpby(1.0, b_, -1.0, v_, r_);    // r_ := b - A*x0
    deviceCopy(rHat_, r_);            // rHat_ fixed for the rest of this solve

    // GPU-PIPE-001 rho-carry: one reduction yields both the initial residual
    // norm and the first iteration's rho.
    //
    // rHat_ is a bitwise copy of r_ at this point (the deviceCopy above), so
    // dot(rHat_, r_) and dot(r_, r_) multiply the same pairs in the same block
    // partition through the same tree -- bitwise identical. Taking the raw dot
    // here rather than deriving it from b0 matters: b0*b0 is NOT bitwise
    // dot(r_, r_), because b0 is its square root.
    const Real r0DotR0 = dot(r_, r_);
    const Real b0 = std::sqrt(r0DotR0);
    Real rhoNext = r0DotR0;  // == dot(rHat_, r_) for iteration 1
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
      return result;
    }

    Real rhoOld = 1.0;
    Real alpha = 1.0;
    Real omega = 1.0;
    fill(v_, n, 0.0);
    fill(p_, n, 0.0);

    const auto breakdown = [&](Index completedIterations) {
      result.status = SolverStatus::Breakdown;
      result.finalResidual = l2Norm(r_);
      result.iterations = completedIterations;
      return result;
    };

    // GPU-PCORR-001: scale-relative breakdown tests, mirroring
    // cfd::algebra::BiCGSTAB's cancelledToRoundingLevel. The absolute
    // |rho| < constants::tiny = 1e-30 this file used before is not invariant
    // under (A, b) -> (A, beta b): it read a healthy iteration of a
    // small-residual system as a breakdown, which is exactly how production
    // SIMPLE's pressure correction failed on the GPU at 320^2 and above while
    // the CPU -- already fixed this way by P12-MESH-004 -- completed. An inner
    // product is zero only once it has cancelled down to the rounding level of
    // its own terms, sum_i |x_i y_i|, which absDot() computes on the device.
    const auto cancelledToRoundingLevel = [](Real innerProduct, const DeviceVector& x,
                                             const DeviceVector& y, Real normX, Real normY) {
      const Real epsilon = std::numeric_limits<Real>::epsilon();
      if (std::abs(innerProduct) > epsilon * normX * normY) return false;
      return std::abs(innerProduct) <= epsilon * absDot(x, y);
    };
    const Real rHatNorm = b0;  // rHat_ is fixed for this solve, so its norm is b0
    Real rNorm = b0;

    for (Index iter = 1; iter <= settings_.maxIterations; ++iter) {
      // GPU-PIPE-001 rho-carry: rho was already computed -- either at setup
      // (iteration 1) or fused into the previous iteration's residual-norm
      // reduction, which read the very same r_. r_ is written in exactly two
      // places, setup and the end of the loop body, and nothing between that
      // write and here touches it: the waxpby calls below read r_ and write p_
      // and s_. rHat_ is fixed for the whole solve. So this is the same value
      // dot(rHat_, r_) would return here, bit for bit -- the reduction was
      // moved, not replaced.
      const Real rho = rhoNext;
      if (!std::isfinite(rho) || cancelledToRoundingLevel(rho, rHat_, r_, rHatNorm, rNorm)) {
        return breakdown(iter - 1);
      }

      const Real beta = (rho / rhoOld) * (alpha / omega);
      if (!std::isfinite(beta)) {
        return breakdown(iter - 1);
      }

      waxpby(1.0, p_, -omega, v_, p_);  // p_ := p_ - omega * v_
      waxpby(1.0, r_, beta, p_, p_);    // p_ := r_ + beta * p_

      applyPreconditioner(preconditionerRaw, useGpuJacobi, jacobiInverseDiagonal_, p_, pHat_);
      spmv(deviceMatrix_, pHat_, v_);

      // GPU-PIPE-001 Phase 2B: (rHat,v) and (v,v) are independent reductions
      // over the same length, and the cancellation test below needs BOTH on the
      // same iteration -- l2Norm(v_) was previously evaluated unconditionally as
      // an argument, so fusing costs no extra work and saves a whole host round
      // trip. dot2 is bitwise identical to the two separate dot() calls, so
      // rHatDotV and the norm are the same values this test saw before.
      Real rHatDotV = 0.0;
      Real vDotV = 0.0;
      dot2(rHat_, v_, v_, v_, rHatDotV, vDotV);
      if (!std::isfinite(rHatDotV) ||
          cancelledToRoundingLevel(rHatDotV, rHat_, v_, rHatNorm, std::sqrt(vDotV))) {
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
        result.finalResidual = sNorm;
        result.iterations = iter;
        result.residualHistory.push_back(sNorm);
        return result;
      }

      if (converged(sNorm)) {
        axpy(alpha, pHat_, x_);
        result.status = SolverStatus::Converged;
        result.finalResidual = sNorm;
        result.iterations = iter;
        result.residualHistory.push_back(sNorm);
        return result;
      }

      applyPreconditioner(preconditionerRaw, useGpuJacobi, jacobiInverseDiagonal_, s_, sHat_);
      spmv(deviceMatrix_, sHat_, t_);

      // GPU-PIPE-001 Phase 2B: (t,t) and (t,s) fused into one host round trip.
      // dot2 is bitwise identical to the two dot() calls it replaces, and the
      // two breakdown tests below are applied in exactly their original order,
      // so whichever condition fired first before still fires first now.
      //
      // The one behavioural difference is that tDotS is now computed even when
      // the tDotT test is about to break down. That is deliberate and harmless:
      // breakdown returns immediately either way, so the extra reduction only
      // ever happens on a path that is ending the solve, and it touches no
      // solver state.
      //
      // t . t is a sum of squares (no cancellation): "zero" only when t is, or
      // when it underflows below the normal range -- the CPU's own criterion.
      Real tDotT = 0.0;
      Real tDotS = 0.0;
      dot2(t_, t_, t_, s_, tDotT, tDotS);
      if (!std::isfinite(tDotT) || tDotT < std::numeric_limits<Real>::min()) {
        return breakdown(iter - 1);
      }

      if (!std::isfinite(tDotS) ||
          cancelledToRoundingLevel(tDotS, t_, s_, std::sqrt(tDotT), sNorm)) {
        return breakdown(iter - 1);
      }
      omega = tDotS / tDotT;
      if (!std::isfinite(omega)) {
        return breakdown(iter - 1);
      }

      axpy(alpha, pHat_, x_);
      axpy(omega, sHat_, x_);
      waxpby(1.0, s_, -omega, t_, r_);  // r_ := s_ - omega * t_

      // GPU-PIPE-001 rho-carry: the residual norm for THIS iteration and rho
      // for the NEXT one are two independent reductions over the same, just-
      // updated r_ (and the fixed rHat_), so they fuse into one round trip.
      // 5 reduction groups per iteration become 4.
      //
      // On the iteration that exits -- converged, non-finite, or the last one
      // before MaxIterations -- rhoNext is computed and never used. That costs
      // nothing: it is one quantity inside a reduction this iteration performs
      // anyway, and it touches no solver state.
      Real rDotR = 0.0;
      dot2(r_, r_, rHat_, r_, rDotR, rhoNext);
      const Real residualNorm = std::sqrt(rDotR);
      rNorm = residualNorm;
      if (!std::isfinite(residualNorm)) {
        result.status = SolverStatus::NonFiniteResidual;
        result.finalResidual = residualNorm;
        result.iterations = iter;
        result.residualHistory.push_back(residualNorm);
        return result;
      }
      result.residualHistory.push_back(residualNorm);
      result.iterations = iter;

      if (converged(residualNorm)) {
        result.status = SolverStatus::Converged;
        result.finalResidual = residualNorm;
        return result;
      }

      rhoOld = rho;
    }

    result.status = SolverStatus::MaxIterations;
    result.finalResidual = result.residualHistory.back();
    return result;
  }


}  // namespace

SolverResult solveBiCGSTABResident(const LinearSolverSettings& settings,
                                   const DeviceCsrMatrix& matrix, GpuKrylovWorkspace& workspace) {
  cfd::Timer solveTimer;
  const Index n = matrix.rows();

  SolverResult result;
  result.backendUsed = LinearSolverBackend::GPU;

  if (workspace.x.size() != n || workspace.b.size() != n) {
    result.status = SolverStatus::NonFiniteInput;
    return result;
  }

  // Everything the host entry point learns from Vector::allFinite(),
  // SparseMatrix::allFinite() and computeInverseDiagonal() throwing -- in one
  // 8-byte read. Checked in the host path's order, so a non-finite matrix is
  // still NonFiniteInput and only a bad diagonal is InvalidSystem.
  const bool wantJacobi = settings.preconditioner == PreconditionerType::Jacobi;
  const ResidentSystemCheck check = checkSystemAndBuildJacobi(matrix, workspace, wantJacobi);
  if (!check.inputsFinite) {
    result.status = SolverStatus::NonFiniteInput;
    return result;
  }
  if (wantJacobi && !check.diagonalUsable) {
    result.status = SolverStatus::InvalidSystem;
    return result;
  }
  const bool useGpuJacobi = wantJacobi;

  workspace.resize(n);
  result = bicgstabCore(matrix, settings, nullptr, useGpuJacobi, workspace, n);
  // NO download: workspace.x holds the solution and stays resident. That single
  // omission is the whole point of this entry point.

  auto& stats = gpuExecutionStats();
  stats.gpuSolveSeconds += solveTimer.elapsedSeconds();
  ++stats.gpuLinearSolves;
  stats.gpuLinearSolverIterations += static_cast<std::uint64_t>(result.iterations);
  return result;
}

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
