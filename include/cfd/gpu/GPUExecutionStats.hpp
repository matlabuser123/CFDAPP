#pragma once

#include <cstdint>

namespace cfd::gpu {

// P6-PERF-001 -- Performance: process-wide counters proving (not just
// claiming) that the persistent GPU pipeline actually reduces host<->
// device traffic. Deliberately has no CUDA dependency -- it is plain
// counters, so it is compiled unconditionally into cfdcore and stays
// includable/usable in a CPU-only build (where every counter simply
// stays zero forever, since nothing on the CPU-only path ever touches a
// device). This mirrors GPUBackend.hpp's own "never claim GPU activity
// that didn't happen" philosophy, just for volume instead of a single
// boolean.
struct GPUExecutionStats {
  std::uint64_t hostToDeviceBytes{};
  std::uint64_t deviceToHostBytes{};
  std::uint64_t hostToDeviceCalls{};
  std::uint64_t deviceToHostCalls{};
  std::uint64_t allocations{};
  // P6-GPU-001 -- Performance: the subset of `allocations` that grew an
  // already-once-allocated buffer (DeviceBuffer::resize() past its
  // current capacity a second-or-later time), as opposed to a buffer's
  // very first cudaMalloc. Distinguishing the two is what lets a caller
  // tell "case initialization allocated N buffers" (expected) apart from
  // "iteration 7 unexpectedly grew a buffer" (a persistence-path bug) --
  // see DeviceBuffer.hpp's own resize() for exactly where this is
  // counted.
  std::uint64_t reallocations{};
  std::uint64_t frees{};
  std::uint64_t kernelLaunches{};
  std::uint64_t synchronizations{};
  // Wall-clock time (cfd::Timer, std::chrono::steady_clock) spent inside
  // the cudaMemcpy calls themselves -- host-side timing around a
  // synchronous copy, not a separate CUDA-event-based device timer
  // (this task's own "do not build unnecessary abstraction" -- a second
  // timing mechanism would only matter if these numbers needed to
  // exclude host-side call overhead, and nothing here does). Summed
  // across every DeviceBuffer::uploadFrom/downloadTo call process-wide.
  double uploadSeconds{};
  double downloadSeconds{};
  // Wall-clock time spent between a kernel launch and the
  // cudaDeviceSynchronize() that follows it -- every GPU kernel this
  // codebase launches (SpMV, and P6-GPU-002's vector ops/reductions)
  // adds to this one running total; includes launch overhead, not just
  // device execution, for the same no-second-timing-mechanism reason as
  // upload/downloadSeconds above.
  double kernelSeconds{};

  // P6-GPU-002 -- Performance: finer-grained breakdowns of kernelSeconds
  // for the GPU Krylov solvers specifically -- dotSeconds covers the
  // reduction kernels (dot/l2Norm), vectorOpSeconds covers the
  // elementwise ones (waxpby/axpy/copy/fill). Both also count toward
  // kernelSeconds above (not instead of it) so existing callers reading
  // only kernelSeconds still see the true total.
  double dotSeconds{};
  double vectorOpSeconds{};
  // P6-GPU-003 -- Performance: GPU-resident Jacobi preconditioning
  // (GpuPreconditioner.hpp) breakdown, separate from the general-purpose
  // dotSeconds/vectorOpSeconds above so a caller can isolate exactly how
  // much a preconditioner costs instead of reading it out of the
  // generic Krylov-primitive totals. preconditionerSetupSeconds is the
  // one-time-per-solve() host diagonal extraction + single upload
  // (GpuPreconditioner::buildJacobiDiagonal, called once at the top of
  // GpuCG/GpuBiCGSTAB::solveImpl, never per Krylov iteration);
  // preconditionerApplySeconds is the per-iteration device-only M^-1 r
  // kernel (GpuPreconditioner::applyJacobiDiagonal). Both also count
  // toward kernelSeconds/uploadSeconds above (not instead of them), same
  // "superset, not a separate mechanism" contract as dotSeconds/
  // vectorOpSeconds already document.
  double preconditionerSetupSeconds{};
  double preconditionerApplySeconds{};
  // Wall-clock time spent inside one GpuCG/GpuBiCGSTAB solve() call,
  // summed across every call -- the one number that answers "how much
  // wall time did GPU linear solving cost", inclusive of every
  // upload/kernel/download/download it performed (a superset of
  // upload+download+kernel+dot+vectorOp Seconds, since it also covers
  // host-side control-flow time between them).
  double gpuSolveSeconds{};
  // Process-wide counters proving repeated production solves reuse the
  // persistent GPU pipeline (P6-GPU-001) rather than reinitializing it:
  // gpuLinearSolves is incremented once per GpuCG/GpuBiCGSTAB::solve()
  // call, gpuLinearSolverIterations by that call's own iteration count --
  // a caller can check "many solves, allocations still low" the same way
  // the P6-GPU-001 persistence tests check DeviceCsrMatrix reuse.
  std::uint64_t gpuLinearSolves{};
  std::uint64_t gpuLinearSolverIterations{};
  // Incremented by cfd::algebra::makeLinearSolver() every time GPU
  // execution was requested (LinearSolverSettings::backend == GPU) but
  // unavailable, so the deterministic CPU fallback it performs is
  // provable in a test without parsing log output (see
  // LinearSolverFactory.cpp's own header comment).
  std::uint64_t gpuBackendFallbacks{};
};

// A single process-wide instance: this project has exactly one GPU
// execution context at a time (see GPUBackend.hpp), so a per-context
// object would be unused generality. Device-resident types
// (DeviceBuffer et al.) record into this on every allocation/transfer/
// launch; callers (tests, benchmarks) read it directly.
[[nodiscard]] GPUExecutionStats& gpuExecutionStats() noexcept;

// Zeroes every counter -- call before a measurement window so deltas are
// unambiguous (e.g. "did structure get re-uploaded on iteration 2?").
void resetGpuExecutionStats() noexcept;

}  // namespace cfd::gpu
