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
#include <cuda_runtime.h>

#include <string>
#include <vector>

#include "cfd/core/Exception.hpp"
#include "cfd/gpu/CudaSpmv.hpp"
#include "cfd/gpu/GPUBackend.hpp"

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

void checkCuda(cudaError_t status, const char* what) {
  if (status != cudaSuccess) {
    throw NumericalError(std::string("csrSpmvCuda: ") + what +
                         " failed: " + cudaGetErrorString(status));
  }
}

}  // namespace

cfd::algebra::Vector csrSpmvCuda(const cfd::algebra::SparseMatrix& matrix,
                                 const cfd::algebra::Vector& x) {
  if (x.size() != matrix.columns()) {
    throw InvalidArgumentError("csrSpmvCuda: vector size does not match matrix columns");
  }
  if (!cudaAvailable()) {
    throw NumericalError("csrSpmvCuda: no usable CUDA device is available at runtime");
  }

  const cfd::Index rows = matrix.rows();
  const cfd::Index nnz = matrix.nonZeros();

  cfd::Real* deviceValues = nullptr;
  cfd::Index* deviceColumnIndices = nullptr;
  cfd::Index* deviceRowOffsets = nullptr;
  cfd::Real* deviceX = nullptr;
  cfd::Real* deviceY = nullptr;

  checkCuda(cudaMalloc(&deviceValues, nnz * sizeof(cfd::Real)), "cudaMalloc(values)");
  checkCuda(cudaMalloc(&deviceColumnIndices, nnz * sizeof(cfd::Index)),
            "cudaMalloc(columnIndices)");
  checkCuda(cudaMalloc(&deviceRowOffsets, (rows + 1) * sizeof(cfd::Index)),
            "cudaMalloc(rowOffsets)");
  checkCuda(cudaMalloc(&deviceX, x.size() * sizeof(cfd::Real)), "cudaMalloc(x)");
  checkCuda(cudaMalloc(&deviceY, rows * sizeof(cfd::Real)), "cudaMalloc(y)");

  checkCuda(cudaMemcpy(deviceValues, matrix.valuesData(), nnz * sizeof(cfd::Real),
                       cudaMemcpyHostToDevice),
            "cudaMemcpy(values)");
  checkCuda(cudaMemcpy(deviceColumnIndices, matrix.columnIndicesData(), nnz * sizeof(cfd::Index),
                       cudaMemcpyHostToDevice),
            "cudaMemcpy(columnIndices)");
  checkCuda(cudaMemcpy(deviceRowOffsets, matrix.rowOffsetsData(), (rows + 1) * sizeof(cfd::Index),
                       cudaMemcpyHostToDevice),
            "cudaMemcpy(rowOffsets)");
  checkCuda(cudaMemcpy(deviceX, x.data(), x.size() * sizeof(cfd::Real), cudaMemcpyHostToDevice),
            "cudaMemcpy(x)");

  constexpr int kThreadsPerBlock = 256;
  const int blocks =
      static_cast<int>((rows + kThreadsPerBlock - 1) / static_cast<cfd::Index>(kThreadsPerBlock));
  csrSpmvKernel<<<blocks, kThreadsPerBlock>>>(rows, deviceValues, deviceColumnIndices,
                                              deviceRowOffsets, deviceX, deviceY);
  checkCuda(cudaGetLastError(), "csrSpmvKernel launch");
  checkCuda(cudaDeviceSynchronize(), "csrSpmvKernel execution");

  cfd::algebra::Vector y(rows);
  checkCuda(cudaMemcpy(y.data(), deviceY, rows * sizeof(cfd::Real), cudaMemcpyDeviceToHost),
            "cudaMemcpy(y back to host)");

  cudaFree(deviceValues);
  cudaFree(deviceColumnIndices);
  cudaFree(deviceRowOffsets);
  cudaFree(deviceX);
  cudaFree(deviceY);

  return y;
}

}  // namespace cfd::gpu
