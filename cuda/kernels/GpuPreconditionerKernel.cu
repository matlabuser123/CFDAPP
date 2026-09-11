// P6-GPU-003 -- Performance: the CUDA-backed implementation of
// GpuPreconditioner.hpp's two free functions -- GPU-resident Jacobi
// preconditioning for GpuCG/GpuBiCGSTAB (cuda/kernels/
// GpuLinearSolverCuda.cpp). Same minimal-includes constraint as
// DeviceVectorOpsKernel.cu's own header comment (nvcc cannot reliably
// parse this toolchain's <functional>/<unordered_map> internals) --
// SparseMatrix.hpp/Preconditioner.hpp only pull in <vector>, already
// proven safe by DeviceVector.hpp's own successful build.
#include <cuda_runtime.h>

#include "cfd/algebra/Preconditioner.hpp"
#include "cfd/core/Exception.hpp"
#include "cfd/core/Timer.hpp"
#include "cfd/gpu/CudaCheck.hpp"
#include "cfd/gpu/GPUExecutionStats.hpp"
#include "cfd/gpu/GpuPreconditioner.hpp"

namespace cfd::gpu {

namespace {

constexpr int kThreadsPerBlock = 256;

int blockCountFor(cfd::Index n) {
  return static_cast<int>((n + kThreadsPerBlock - 1) / static_cast<cfd::Index>(kThreadsPerBlock));
}

// z_i = invDiag_i * r_i -- the Jacobi preconditioner application kernel,
// one thread per element (same simplest-correct-parallelization
// precedent as every other elementwise kernel in this codebase --
// DeviceVectorOpsKernel.cu's own header comment).
__global__ void jacobiApplyKernel(cfd::Index n, const cfd::Real* inverseDiagonal,
                                  const cfd::Real* input, cfd::Real* output) {
  const cfd::Index i = static_cast<cfd::Index>(blockIdx.x) * blockDim.x + threadIdx.x;
  if (i >= n) return;
  output[i] = inverseDiagonal[i] * input[i];
}

}  // namespace

void buildJacobiDiagonal(const cfd::algebra::SparseMatrix& matrix, DeviceVector& inverseDiagonal) {
  cfd::Timer setupTimer;
  // Host-side extraction + validation (throws InvalidArgumentError on a
  // missing/zero/near-zero/non-finite diagonal -- propagates to the
  // caller, GpuCG/GpuBiCGSTAB::solveImpl, which converts it to
  // SolverStatus::InvalidSystem exactly like a rejected CPU-side
  // Preconditioner::build() does). O(n), done once per solve() call, not
  // per Krylov iteration.
  const cfd::algebra::Vector hostInverseDiagonal = cfd::algebra::computeInverseDiagonal(matrix);
  // The one host->device transfer this preconditioner ever pays per
  // solve() -- DeviceVector::uploadFrom already resizes (reusing
  // capacity when the row count is unchanged, DeviceBuffer::resize()'s
  // own contract) and records itself in GPUExecutionStats::
  // uploadSeconds/hostToDeviceBytes.
  inverseDiagonal.uploadFrom(hostInverseDiagonal);
  gpuExecutionStats().preconditionerSetupSeconds += setupTimer.elapsedSeconds();
}

void applyJacobiDiagonal(const DeviceVector& inverseDiagonal, const DeviceVector& input,
                         DeviceVector& output) {
  if (input.size() != inverseDiagonal.size()) {
    throw cfd::InvalidArgumentError("applyJacobiDiagonal: input size does not match diagonal size");
  }
  output.resize(input.size());
  if (input.size() == 0) return;

  const int blocks = blockCountFor(input.size());
  cfd::Timer timer;
  jacobiApplyKernel<<<blocks, kThreadsPerBlock>>>(input.size(), inverseDiagonal.data(), input.data(),
                                                  output.data());
  checkCuda(cudaGetLastError(), "jacobiApplyKernel launch");
  ++gpuExecutionStats().kernelLaunches;
  checkCuda(cudaDeviceSynchronize(), "jacobiApplyKernel execution");
  auto& stats = gpuExecutionStats();
  const double elapsed = timer.elapsedSeconds();
  stats.kernelSeconds += elapsed;
  stats.preconditionerApplySeconds += elapsed;
  ++stats.synchronizations;
}

}  // namespace cfd::gpu
