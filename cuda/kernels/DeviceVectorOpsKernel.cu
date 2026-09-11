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
  checkCuda(cudaDeviceSynchronize(), "waxpbyKernel execution");
  auto& stats = gpuExecutionStats();
  const double elapsed = timer.elapsedSeconds();
  stats.kernelSeconds += elapsed;
  stats.vectorOpSeconds += elapsed;
  ++stats.synchronizations;
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
  checkCuda(cudaDeviceSynchronize(), "fillKernel execution");
  auto& stats = gpuExecutionStats();
  const double elapsed = timer.elapsedSeconds();
  stats.kernelSeconds += elapsed;
  stats.vectorOpSeconds += elapsed;
  ++stats.synchronizations;
}

cfd::Real dot(const DeviceVector& a, const DeviceVector& b) {
  checkSameSize(a.size(), b.size(), "dot");
  const cfd::Index n = a.size();
  if (n == 0) return 0.0;

  cfd::Timer totalTimer;
  const int blocks = blockCountFor(n);
  // A persistent, high-water-mark reduction buffer -- dot() is called
  // many times per GpuCG/GpuBiCGSTAB iteration (P6-GPU-002), so a fresh
  // local DeviceBuffer here would cudaMalloc/cudaFree on every single
  // call, exactly the anti-pattern this task exists to eliminate.
  // `static` (not a member of some object dot() doesn't have) is this
  // free function's only way to persist state across calls -- the same
  // "one shared process-wide GPU context" precedent gpuExecutionStats()
  // itself already establishes. resize() only grows (never shrinks,
  // never reallocates once big enough -- DeviceBuffer's own contract),
  // so repeated solves against the same (or smaller) grid size reuse
  // this buffer with zero further allocation.
  static DeviceBuffer<cfd::Real> partialSumsCache;
  partialSumsCache.resize(static_cast<cfd::Index>(blocks));
  DeviceBuffer<cfd::Real>& partialSums = partialSumsCache;

  cfd::Timer kernelTimer;
  partialDotKernel<<<blocks, kThreadsPerBlock, kThreadsPerBlock * sizeof(cfd::Real)>>>(
      n, a.data(), b.data(), partialSums.data());
  checkCuda(cudaGetLastError(), "partialDotKernel launch");
  ++gpuExecutionStats().kernelLaunches;
  checkCuda(cudaDeviceSynchronize(), "partialDotKernel execution");
  auto& stats = gpuExecutionStats();
  stats.kernelSeconds += kernelTimer.elapsedSeconds();
  ++stats.synchronizations;

  std::vector<cfd::Real> hostPartials(static_cast<std::size_t>(blocks));
  partialSums.downloadTo(hostPartials.data(), static_cast<cfd::Index>(blocks));

  cfd::Real sum = 0.0;
  for (cfd::Real partial : hostPartials) sum += partial;

  stats.dotSeconds += totalTimer.elapsedSeconds();
  return sum;
}

cfd::Real l2Norm(const DeviceVector& v) { return std::sqrt(dot(v, v)); }

}  // namespace cfd::gpu
