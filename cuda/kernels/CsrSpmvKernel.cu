// P4 -- Performance: CSR sparse matrix-vector multiplication on the GPU
// -- the narrow, benchmarkable first CUDA target (section 32). One
// thread per row (the simplest correct CSR SpMV kernel; a warp-per-row
// or vectorized variant is a further optimization this foundation-level
// task deliberately does not chase -- "do not build unnecessary
// abstraction", and there is no profiling evidence yet that this simple
// kernel is the bottleneck of anything larger). Also provides
// cfd::gpu::cudaAvailable()'s real (runtime-checking) implementation --
// see GPUBackend.hpp's own header comment for why exactly one of this
// file and src/gpu/GPUBackend.cpp's stub is ever compiled.
//
// P6-PERF-001 -- Performance: the kernel itself is unchanged, but this
// file now exposes two ways to run it: the original stateless
// csrSpmvCuda(matrix, x) (a fresh upload/compute/download every call --
// kept, unchanged behavior, for one-off callers) and the new persistent-
// residency spmv(DeviceCsrMatrix, DeviceVector, DeviceVector) (no
// allocation or transfer at all -- for a caller that keeps its own
// DeviceCsrMatrix/DeviceVector alive across repeated calls). csrSpmvCuda
// is now implemented in terms of the persistent primitives internally,
// so there is exactly one CUDA code path, not two.
#include <cuda_runtime.h>

#include "cfd/core/Exception.hpp"
#include "cfd/core/Timer.hpp"
#include "cfd/gpu/CudaCheck.hpp"
#include "cfd/gpu/CudaSpmv.hpp"
#include "cfd/gpu/DeviceCsrMatrix.hpp"
#include "cfd/gpu/DeviceVector.hpp"
#include "cfd/gpu/GPUBackend.hpp"
#include "cfd/gpu/GPUExecutionStats.hpp"

namespace cfd::gpu {

bool cudaAvailable() {
  int deviceCount = 0;
  const cudaError_t status = cudaGetDeviceCount(&deviceCount);
  // cudaGetDeviceCount itself returns cudaErrorNoDevice (not a thrown
  // exception) when no GPU is present -- exactly the "clean CPU
  // fallback path" section 29/30 wants, not a crash.
  return status == cudaSuccess && deviceCount > 0;
}

namespace {

// One thread per matrix row -- CSR SpMV's simplest correct
// parallelization, directly analogous to
// SparseMatrix::multiply's own OpenMP-parallelized per-row CPU loop
// (SparseMatrix.cpp's own header comment): each row's inner sum is
// still accumulated by a single thread, in the same fixed ascending-k
// order, so this kernel's floating-point result is bit-identical to the
// CPU one for the exact same input -- checked directly in
// test_cuda_spmv.cpp, not just "close enough".
__global__ void csrSpmvKernel(cfd::Index rows, const cfd::Real* values,
                              const cfd::Index* columnIndices, const cfd::Index* rowOffsets,
                              const cfd::Real* x, cfd::Real* y) {
  const cfd::Index row = static_cast<cfd::Index>(blockIdx.x) * blockDim.x + threadIdx.x;
  if (row >= rows) return;
  cfd::Real sum = 0.0;
  for (cfd::Index k = rowOffsets[row]; k < rowOffsets[row + 1]; ++k) {
    sum += values[k] * x[columnIndices[k]];
  }
  y[row] = sum;
}

}  // namespace

void spmv(const DeviceCsrMatrix& matrix, const DeviceVector& x, DeviceVector& y) {
  if (x.size() != matrix.columns()) {
    throw InvalidArgumentError("spmv: vector size does not match matrix columns");
  }
  if (y.size() != matrix.rows()) {
    throw InvalidArgumentError("spmv: output vector size does not match matrix rows");
  }

  constexpr int kThreadsPerBlock = 256;
  const int blocks = static_cast<int>(
      (matrix.rows() + kThreadsPerBlock - 1) / static_cast<cfd::Index>(kThreadsPerBlock));
  cfd::Timer kernelTimer;
  csrSpmvKernel<<<blocks, kThreadsPerBlock>>>(matrix.rows(), matrix.valuesDevice(),
                                              matrix.columnIndicesDevice(),
                                              matrix.rowOffsetsDevice(), x.data(), y.data());
  checkCuda(cudaGetLastError(), "csrSpmvKernel launch");
  ++gpuExecutionStats().kernelLaunches;
  checkCuda(cudaDeviceSynchronize(), "csrSpmvKernel execution");
  auto& stats = gpuExecutionStats();
  stats.kernelSeconds += kernelTimer.elapsedSeconds();
  ++stats.synchronizations;
}

cfd::algebra::Vector csrSpmvCuda(const cfd::algebra::SparseMatrix& matrix,
                                 const cfd::algebra::Vector& x) {
  if (x.size() != matrix.columns()) {
    throw InvalidArgumentError("csrSpmvCuda: vector size does not match matrix columns");
  }
  if (!cudaAvailable()) {
    throw NumericalError("csrSpmvCuda: no usable CUDA device is available at runtime");
  }

  // A private, short-lived DeviceCsrMatrix/DeviceVector pair built on
  // top of the shared persistent-residency primitives -- preserves this
  // function's original stateless contract (fresh upload/compute/
  // download every call, correct for a one-off caller that never
  // revisits the same matrix) while sharing one CUDA code path instead
  // of keeping a second copy. A caller that *does* revisit the same
  // matrix repeatedly should hold its own DeviceCsrMatrix/DeviceVector
  // and call spmv() directly to get the actual reuse benefit -- see
  // DeviceCsrMatrix.hpp's own header comment.
  DeviceCsrMatrix deviceMatrix;
  deviceMatrix.uploadStructureAndValues(matrix);

  DeviceVector deviceX;
  deviceX.uploadFrom(x);

  DeviceVector deviceY(matrix.rows());
  spmv(deviceMatrix, deviceX, deviceY);

  return deviceY.downloadToVector();
}

}  // namespace cfd::gpu
