// P6-GPU-002 -- Performance: the general-purpose device vector-op kernel
// set GpuCG/GpuBiCGSTAB are built from (waxpby/axpy/copy/fill/dot/
// l2Norm) -- see DeviceVectorOps.hpp's own header comment. One thread
// per element for the elementwise kernels (the same "simplest correct
// parallelization, no premature optimization" precedent
// CsrSpmvKernel.cu's own header comment sets for SpMV); dot() is a
// standard two-stage block-reduction (shared-memory tree reduction per
// block, then a small host-side sum over one partial-sum-per-block
// buffer -- see DeviceVectorOps.hpp's own header comment on why that
// download is a "small reduction buffer", not a whole-vector round
// trip).
//
// Deliberately minimal includes -- no <unordered_map>/<algorithm> --
// this project's nvcc 11.5 cannot parse this GCC 11 toolchain's
// <functional> (transitively pulled in by <unordered_map>, a documented
// nvcc/libstdc++ incompatibility discovered while building
// GpuResidencyManagerCuda in P6-GPU-001; see that file's own header
// comment). Every header below is already proven safe by
// CsrSpmvKernel.cu's own successful build.
#include <cuda_runtime.h>

#include <cmath>
#include <cstddef>
#include <vector>

#include "cfd/core/Exception.hpp"
#include "cfd/core/Timer.hpp"
#include "cfd/gpu/CudaCheck.hpp"
#include "cfd/gpu/DeviceBuffer.hpp"
#include "cfd/gpu/DeviceVectorOps.hpp"
#include "cfd/gpu/GPUExecutionStats.hpp"

namespace cfd::gpu {

namespace {

constexpr int kThreadsPerBlock = 256;

int blockCountFor(cfd::Index n) {
  return static_cast<int>((n + kThreadsPerBlock - 1) / static_cast<cfd::Index>(kThreadsPerBlock));
}

void checkSameSize(cfd::Index a, cfd::Index b, const char* what) {
  if (a != b) {
    throw cfd::InvalidArgumentError(std::string(what) + ": vector size mismatch");
  }
}

__global__ void waxpbyKernel(cfd::Index n, cfd::Real a, const cfd::Real* x, cfd::Real b,
                             const cfd::Real* y, cfd::Real* w) {
  const cfd::Index i = static_cast<cfd::Index>(blockIdx.x) * blockDim.x + threadIdx.x;
  if (i >= n) return;
  w[i] = a * x[i] + b * y[i];
}

__global__ void fillKernel(cfd::Index n, cfd::Real value, cfd::Real* v) {
  const cfd::Index i = static_cast<cfd::Index>(blockIdx.x) * blockDim.x + threadIdx.x;
  if (i >= n) return;
  v[i] = value;
}

// blockDim.x is always kThreadsPerBlock (a power of two), so the
// halving tree reduction below is exact regardless of how many threads
// in the last block correspond to real elements (out-of-range threads
// contribute 0, same convention csrSpmvKernel's own row>=rows guard
// uses).
__global__ void partialDotKernel(cfd::Index n, const cfd::Real* a, const cfd::Real* b,
                                 cfd::Real* partialSums) {
  extern __shared__ cfd::Real shared[];
  const cfd::Index i = static_cast<cfd::Index>(blockIdx.x) * blockDim.x + threadIdx.x;
  shared[threadIdx.x] = (i < n) ? a[i] * b[i] : 0.0;
  __syncthreads();
  for (unsigned int stride = blockDim.x / 2; stride > 0; stride >>= 1) {
    if (threadIdx.x < stride) {
      shared[threadIdx.x] += shared[threadIdx.x + stride];
    }
    __syncthreads();
  }
  if (threadIdx.x == 0) {
    partialSums[blockIdx.x] = shared[0];
  }
}

// GPU-PIPE-001 Phase 2B: two independent dot products over the same length in
// ONE launch. Each product keeps its OWN shared-memory halving tree and its own
// partial-sum slice (partialSums[0..blocks) and partialSums[blocks..2*blocks)),
// so every partial is computed from exactly the same terms, in exactly the same
// block partition, by exactly the same tree as partialDotKernel would produce
// for that product alone. Fusing changes *when* the arithmetic happens, never
// *what* it is -- which is why dot2() is bitwise identical to two dot() calls
// and needs no tolerance of its own.
//
// Shared memory is 2 * blockDim * sizeof(Real); the two trees are kept in
// disjoint halves so neither can perturb the other.
__global__ void partialDot2Kernel(cfd::Index n, const cfd::Real* a0, const cfd::Real* b0,
                                  const cfd::Real* a1, const cfd::Real* b1,
                                  cfd::Real* partialSums) {
  extern __shared__ cfd::Real shared[];
  cfd::Real* s0 = shared;
  cfd::Real* s1 = shared + blockDim.x;
  const cfd::Index i = static_cast<cfd::Index>(blockIdx.x) * blockDim.x + threadIdx.x;
  s0[threadIdx.x] = (i < n) ? a0[i] * b0[i] : 0.0;
  s1[threadIdx.x] = (i < n) ? a1[i] * b1[i] : 0.0;
  __syncthreads();
  for (unsigned int stride = blockDim.x / 2; stride > 0; stride >>= 1) {
    if (threadIdx.x < stride) {
      s0[threadIdx.x] += s0[threadIdx.x + stride];
      s1[threadIdx.x] += s1[threadIdx.x + stride];
    }
    __syncthreads();
  }
  if (threadIdx.x == 0) {
    partialSums[blockIdx.x] = s0[0];
    partialSums[static_cast<cfd::Index>(gridDim.x) + blockIdx.x] = s1[0];
  }
}

// GPU-PIPE-001 Phase 2A was implemented here as finalizeSumsKernel -- one
// thread per quantity walking its partial slice in ascending block order, so
// the summation order (and therefore the bits) matched the host loop exactly --
// and then REMOVED on its own measurement.
//
// Measured, paired, same session (results/gpu-pipe-001/phase2-reductions/
// 40_isolate_finalizer.log), gpu_solve median:
//
//     grid   blocks   device finalise   host finalise
//     160^2     100        2.0315 s        2.0191 s
//     640^2    1600        5.0178 s        3.7741 s     <- 25% worse
//
// The serial finalise is O(blocks) dependent adds on a single CUDA core, and
// blocks grows with the problem, so its cost grows while the thing it buys --
// an 8-byte download instead of a blocks-sized one -- buys nothing: Phase 1
// measured the round trip at 79-101 us per call REGARDLESS of payload
// (results/gpu-pipe-001/baseline/summary.md 5). Bytes were never the
// bottleneck; calls and synchronizations are, and device finalisation reduces
// neither. It was never faster at any measured size.
//
// Host finalisation is therefore kept. The fused partial kernel (Phase 2B) is
// what actually removes round trips, and it is retained.

// partialDotKernel over |a[i] * b[i]| -- see absDot()'s header comment. Same
// exact halving tree, same out-of-range convention.
__global__ void partialAbsDotKernel(cfd::Index n, const cfd::Real* a, const cfd::Real* b,
                                    cfd::Real* partialSums) {
  extern __shared__ cfd::Real shared[];
  const cfd::Index i = static_cast<cfd::Index>(blockIdx.x) * blockDim.x + threadIdx.x;
  shared[threadIdx.x] = (i < n) ? fabs(a[i] * b[i]) : 0.0;
  __syncthreads();
  for (unsigned int stride = blockDim.x / 2; stride > 0; stride >>= 1) {
    if (threadIdx.x < stride) {
      shared[threadIdx.x] += shared[threadIdx.x + stride];
    }
    __syncthreads();
  }
  if (threadIdx.x == 0) {
    partialSums[blockIdx.x] = shared[0];
  }
}

}  // namespace

void waxpby(cfd::Real a, const DeviceVector& x, cfd::Real b, const DeviceVector& y,
           DeviceVector& w) {
  checkSameSize(x.size(), y.size(), "waxpby");
  w.resize(x.size());
  if (x.size() == 0) return;
  const int blocks = blockCountFor(x.size());
  cfd::Timer timer;
  waxpbyKernel<<<blocks, kThreadsPerBlock>>>(x.size(), a, x.data(), b, y.data(), w.data());
  checkCuda(cudaGetLastError(), "waxpbyKernel launch");
  ++gpuExecutionStats().kernelLaunches;
  // GPU-PIPE-001 Phase 3: no cudaDeviceSynchronize here. w is consumed by
  // later kernels on the default stream, which are ordered after this one by
  // CUDA's own stream semantics -- the synchronize bought correctness that
  // stream ordering already guarantees, and charged a full host-device round
  // trip for it on every vector operation. See phase3-sync/audit.md.
  auto& stats = gpuExecutionStats();
  const double elapsed = timer.elapsedSeconds();
  stats.kernelSeconds += elapsed;
  stats.vectorOpSeconds += elapsed;
}

void axpy(cfd::Real alpha, const DeviceVector& x, DeviceVector& y) { waxpby(alpha, x, 1.0, y, y); }

void deviceCopy(DeviceVector& dst, const DeviceVector& src) {
  dst.resize(src.size());
  if (src.size() == 0) return;
  cfd::Timer timer;
  checkCuda(cudaMemcpy(dst.data(), src.data(), static_cast<std::size_t>(src.size()) * sizeof(cfd::Real),
                       cudaMemcpyDeviceToDevice),
            "cudaMemcpy(deviceCopy)");
  gpuExecutionStats().vectorOpSeconds += timer.elapsedSeconds();
}

void fill(DeviceVector& v, cfd::Index count, cfd::Real value) {
  v.resize(count);
  if (count == 0) return;
  const int blocks = blockCountFor(count);
  cfd::Timer timer;
  fillKernel<<<blocks, kThreadsPerBlock>>>(count, value, v.data());
  checkCuda(cudaGetLastError(), "fillKernel launch");
  ++gpuExecutionStats().kernelLaunches;
  // GPU-PIPE-001 Phase 3: stream-ordered, no synchronize. See waxpby above.
  auto& stats = gpuExecutionStats();
  const double elapsed = timer.elapsedSeconds();
  stats.kernelSeconds += elapsed;
  stats.vectorOpSeconds += elapsed;
}

namespace {

// GPU-PIPE-001 Phase 2A/2B: the one place a reduction becomes host-visible.
//
// Launches the caller's partial-sum kernel for a GROUP of `count` independent
// quantities, synchronizes once, downloads the partials once, and finalises
// each quantity on the host in ascending block order -- the same order, and so
// the same bits, as one dot() per quantity produced before.
//
// Before: per QUANTITY -- 1 launch + 1 sync + 1 blocks-sized download.
// After:  per GROUP     -- 1 launch + 1 sync + 1 (blocks*count)-sized download.
//
// Bytes are unchanged; calls and synchronizations fall by the group size. That
// is the right target: Phase 1 measured the round trip at 79-101 us per call
// regardless of payload, so the cost is paid per call, not per byte.
template <typename LaunchPartials>
void reduceToHost(cfd::Index n, int count, LaunchPartials&& launchPartials, cfd::Real* out) {
  const int blocks = blockCountFor(n);

  // Persistent high-water-mark buffer, same rationale (and the same
  // never-shrink DeviceBuffer contract) as the original dot() cache: this is
  // hit several times per Krylov iteration, so a local buffer would
  // cudaMalloc/cudaFree on every call -- the anti-pattern this phase exists to
  // remove. Sized for the widest group any caller uses.
  static DeviceBuffer<cfd::Real> partialSumsCache;
  static std::vector<cfd::Real> hostPartials;
  partialSumsCache.resize(static_cast<cfd::Index>(blocks) * count);
  hostPartials.resize(static_cast<std::size_t>(blocks) * count);

  cfd::Timer kernelTimer;
  launchPartials(blocks, partialSumsCache.data());
  ++gpuExecutionStats().kernelLaunches;
  checkCuda(cudaDeviceSynchronize(), "reduction execution");
  auto& stats = gpuExecutionStats();
  stats.kernelSeconds += kernelTimer.elapsedSeconds();
  ++stats.synchronizations;
  ++stats.reductionGroups;
  stats.reductionQuantities += static_cast<std::uint64_t>(count);

  partialSumsCache.downloadTo(hostPartials.data(), static_cast<cfd::Index>(blocks) * count);

  // Per quantity, in ascending block order -- identical to the loop each
  // separate dot() ran, which is what makes a fused group bitwise identical to
  // the individual calls it replaces.
  for (int q = 0; q < count; ++q) {
    const cfd::Real* slice = hostPartials.data() + static_cast<std::size_t>(q) * blocks;
    cfd::Real sum = 0.0;
    for (int i = 0; i < blocks; ++i) sum += slice[i];
    out[q] = sum;
  }
}

}  // namespace

cfd::Real dot(const DeviceVector& a, const DeviceVector& b) {
  checkSameSize(a.size(), b.size(), "dot");
  const cfd::Index n = a.size();
  if (n == 0) return 0.0;

  cfd::Timer totalTimer;
  cfd::Real result = 0.0;
  reduceToHost(
      n, 1,
      [&](int blocks, cfd::Real* partials) {
        partialDotKernel<<<blocks, kThreadsPerBlock, kThreadsPerBlock * sizeof(cfd::Real)>>>(
            n, a.data(), b.data(), partials);
        checkCuda(cudaGetLastError(), "partialDotKernel launch");
      },
      &result);
  gpuExecutionStats().dotSeconds += totalTimer.elapsedSeconds();
  return result;
}

// GPU-PIPE-001 Phase 2B: two dot products, one launch, one synchronization, one
// 16-byte download -- instead of two of each. Bitwise identical to calling
// dot(a0,b0) and dot(a1,b1): same terms, same block partition, same per-product
// tree, same final summation order. Requires all four vectors to share a length.
void dot2(const DeviceVector& a0, const DeviceVector& b0, const DeviceVector& a1,
          const DeviceVector& b1, cfd::Real& out0, cfd::Real& out1) {
  checkSameSize(a0.size(), b0.size(), "dot2");
  checkSameSize(a1.size(), b1.size(), "dot2");
  checkSameSize(a0.size(), a1.size(), "dot2");
  const cfd::Index n = a0.size();
  if (n == 0) {
    out0 = 0.0;
    out1 = 0.0;
    return;
  }

  cfd::Timer totalTimer;
  cfd::Real results[2] = {0.0, 0.0};
  reduceToHost(
      n, 2,
      [&](int blocks, cfd::Real* partials) {
        partialDot2Kernel<<<blocks, kThreadsPerBlock, 2 * kThreadsPerBlock * sizeof(cfd::Real)>>>(
            n, a0.data(), b0.data(), a1.data(), b1.data(), partials);
        checkCuda(cudaGetLastError(), "partialDot2Kernel launch");
      },
      results);
  out0 = results[0];
  out1 = results[1];
  gpuExecutionStats().dotSeconds += totalTimer.elapsedSeconds();
}

cfd::Real l2Norm(const DeviceVector& v) { return std::sqrt(dot(v, v)); }

cfd::Real absDot(const DeviceVector& a, const DeviceVector& b) {
  checkSameSize(a.size(), b.size(), "absDot");
  const cfd::Index n = a.size();
  if (n == 0) return 0.0;

  cfd::Timer totalTimer;
  cfd::Real result = 0.0;
  reduceToHost(
      n, 1,
      [&](int blocks, cfd::Real* partials) {
        partialAbsDotKernel<<<blocks, kThreadsPerBlock, kThreadsPerBlock * sizeof(cfd::Real)>>>(
            n, a.data(), b.data(), partials);
        checkCuda(cudaGetLastError(), "partialAbsDotKernel launch");
      },
      &result);
  gpuExecutionStats().dotSeconds += totalTimer.elapsedSeconds();
  return result;
}

}  // namespace cfd::gpu
