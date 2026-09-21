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
  // Explicit cudaDeviceSynchronize() calls. After GPU-PIPE-001 Phase 3 the only
  // remaining one in the solver path is the reduction's, which precedes a
  // blocking D2H copy the host cannot proceed without -- so this counter is now
  // effectively "how many times the host had to wait for the device", which is
  // the quantity the persistent-pipeline work is trying to drive down.
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
  // Wall-clock host time spent issuing GPU kernels.
  //
  // GPU-PIPE-001 Phase 3 CHANGED WHAT THIS MEASURES. It used to be the time
  // between a launch and the cudaDeviceSynchronize() that followed it, i.e.
  // device execution time, because every kernel synchronized. Those
  // synchronizations were removed for the elementwise ops, SpMV and the
  // preconditioner apply (stream ordering already guarantees what they were
  // buying), so for those kernels this now measures LAUNCH time only -- the
  // device work completes asynchronously afterwards.
  //
  // Only the reduction path still synchronizes, so only its contribution here
  // is still device execution time. Per-kernel device time is no longer
  // separable without CUDA events; `gpuSolveSeconds` remains the meaningful
  // aggregate, and the end-to-end benchmark remains the authority on cost.
  // Stated explicitly because a counter that quietly changes meaning is worse
  // than one that is removed.
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
  // GPU-PIPE-001 Phase 2C: reduction accounting, so "how many host round trips
  // did the Krylov solver cost" is measured directly instead of inferred from
  // deviceToHostCalls (which mixes reductions with the solution download) or
  // from an assumed reductions-per-iteration count.
  //
  // reductionGroups is incremented once per reduction ROUND TRIP -- one kernel
  // group, one synchronization, one download -- so it is exactly the quantity
  // Phase 1 measured at 79-101 us each. reductionQuantities counts the scalars
  // those round trips produced. Before Phase 2B the two were equal (one dot per
  // trip); a fused dot2() advances quantities by 2 and groups by 1, so
  // quantities/groups is the realised fusion factor.
  std::uint64_t reductionGroups{};
  std::uint64_t reductionQuantities{};
  // GPU-DISC-001Q -- Performance: device bytes currently held by live
  // DeviceBuffers, and the high-water mark of that quantity. Maintained by
  // DeviceBuffer's own resize()/release(), so it counts exactly what this
  // project allocates and nothing the driver or another library holds.
  //
  // Two questions needed it and no existing counter could answer either:
  // "what is peak GPU memory for this case" (peakDeviceBytes across a solve)
  // and "does VRAM creep per outer iteration" (currentDeviceBytes sampled at
  // the same point in successive iterations, or peak vs the steady value).
  //
  // deviceBytes is NOT the process's total GPU footprint -- the CUDA context,
  // the module images and any cuBLAS-style workspace live outside it. It is
  // the part this codebase controls, which is the part a leak or a creep would
  // appear in. cudaMemGetInfo is used alongside it for the absolute figure.
  std::uint64_t currentDeviceBytes{};
  std::uint64_t peakDeviceBytes{};
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
