// GPU-PIPE-001 (GPU-resident pressure solve) -- the device-side replacements
// for the three things GpuBiCGSTAB::solveImpl could only do with a host matrix:
// check the inputs are finite, build the Jacobi inverse diagonal, and reject a
// system whose diagonal makes that impossible.
//
// Each reproduces its host counterpart EXACTLY -- the same arithmetic and, just
// as importantly, the same rejection. A resident path that quietly accepted a
// system the host path rejects would be a behavioural difference hiding inside
// a performance change.
//
// Compiled with -fmad=false, like every other kernel whose results are compared
// bitwise against the CPU. Nothing here has an a*b+c to contract today; the
// flag is on the translation unit so that adding one later cannot silently
// change a qualified number.
#include <cuda_runtime.h>

#include "cfd/core/Constants.hpp"
#include "cfd/core/Timer.hpp"
#include "cfd/core/Types.hpp"
#include "cfd/gpu/CudaCheck.hpp"
#include "cfd/gpu/DeviceCsrMatrix.hpp"
#include "cfd/gpu/DeviceVector.hpp"
#include "cfd/gpu/GPUExecutionStats.hpp"
#include "cfd/gpu/GpuResidentSolve.hpp"

namespace cfd::gpu {
namespace {

constexpr int kThreadsPerBlock = 256;

int blockCountFor(cfd::Index n) {
  return static_cast<int>((n + kThreadsPerBlock - 1) / kThreadsPerBlock);
}

__global__ void countNonFiniteKernel(cfd::Index count, const cfd::Real* __restrict__ values,
                                     unsigned int* __restrict__ out) {
  const cfd::Index i = static_cast<cfd::Index>(blockIdx.x) * blockDim.x + threadIdx.x;
  if (i >= count) return;
  if (!isfinite(values[i])) atomicAdd(out, 1u);
}

// One thread per ROW, scanning that row for its diagonal -- the same search
// SparseMatrix::diagonal(row) performs, and the same four rejections
// computeInverseDiagonal applies, in the same order. `reject` is a counter
// rather than a flag so a single 4-byte read answers "was any row rejected".
__global__ void jacobiInverseDiagonalKernel(cfd::Index rows,
                                            const cfd::Index* __restrict__ rowOffsets,
                                            const cfd::Index* __restrict__ columnIndices,
                                            const cfd::Real* __restrict__ values,
                                            cfd::Real* __restrict__ inverseDiagonal,
                                            unsigned int* __restrict__ reject) {
  const cfd::Index row = static_cast<cfd::Index>(blockIdx.x) * blockDim.x + threadIdx.x;
  if (row >= rows) return;

  bool found = false;
  cfd::Real diag = 0.0;
  for (cfd::Index k = rowOffsets[row]; k < rowOffsets[row + 1]; ++k) {
    if (columnIndices[k] == row) {
      diag = values[k];
      found = true;
      break;
    }
  }
  // Mirrors, in order: diagonal() throwing when no diagonal is stored, then
  // non-finite, then exactly zero, then below constants::small.
  if (!found || !isfinite(diag) || diag == 0.0 || fabs(diag) < cfd::constants::small) {
    atomicAdd(reject, 1u);
    inverseDiagonal[row] = 0.0;
    return;
  }
  inverseDiagonal[row] = 1.0 / diag;
}

}  // namespace

void GpuKrylovWorkspace::resize(cfd::Index n) {
  x.resize(n);
  b.resize(n);
  r.resize(n);
  rHat.resize(n);
  p.resize(n);
  v.resize(n);
  s.resize(n);
  t.resize(n);
  pHat.resize(n);
  sHat.resize(n);
}

std::size_t GpuKrylovWorkspace::residentBytes() const noexcept {
  const auto bytes = [](const DeviceVector& d) {
    return static_cast<std::size_t>(d.size()) * sizeof(cfd::Real);
  };
  return bytes(x) + bytes(b) + bytes(r) + bytes(rHat) + bytes(p) + bytes(v) + bytes(s) + bytes(t) +
         bytes(pHat) + bytes(sHat) + bytes(jacobiInverseDiagonal) +
         static_cast<std::size_t>(counters.size()) * sizeof(unsigned int);
}

ResidentSystemCheck checkSystemAndBuildJacobi(const DeviceCsrMatrix& matrix,
                                              GpuKrylovWorkspace& workspace, bool wantJacobi) {
  const cfd::Index rows = matrix.rows();
  const cfd::Index nnz = matrix.nonZeros();

  // Persistent, so this allocates on the FIRST solve and never again.
  workspace.counters.resize(2);
  checkCuda(cudaMemset(workspace.counters.data(), 0, 2 * sizeof(unsigned int)),
            "cudaMemset(resident solve counters)");
  unsigned int* const nonFinite = workspace.counters.data();
  unsigned int* const reject = workspace.counters.data() + 1;

  const auto count = [&](const cfd::Real* values, cfd::Index n) {
    if (n == 0) return;
    countNonFiniteKernel<<<blockCountFor(n), kThreadsPerBlock>>>(n, values, nonFinite);
    checkCuda(cudaGetLastError(), "countNonFiniteKernel launch");
    ++gpuExecutionStats().kernelLaunches;
  };
  count(matrix.valuesDevice(), nnz);
  count(workspace.b.data(), workspace.b.size());
  count(workspace.x.data(), workspace.x.size());

  if (wantJacobi && rows > 0) {
    cfd::Timer setupTimer;
    workspace.jacobiInverseDiagonal.resize(rows);
    jacobiInverseDiagonalKernel<<<blockCountFor(rows), kThreadsPerBlock>>>(
        rows, matrix.rowOffsetsDevice(), matrix.columnIndicesDevice(), matrix.valuesDevice(),
        workspace.jacobiInverseDiagonal.data(), reject);
    checkCuda(cudaGetLastError(), "jacobiInverseDiagonalKernel launch");
    ++gpuExecutionStats().kernelLaunches;
    gpuExecutionStats().preconditionerSetupSeconds += setupTimer.elapsedSeconds();
  }

  // THE single host round trip: both answers, eight bytes, once per solve.
  unsigned int host[2] = {0, 0};
  checkCuda(cudaMemcpy(host, workspace.counters.data(), 2 * sizeof(unsigned int),
                       cudaMemcpyDeviceToHost),
            "cudaMemcpy(resident solve counters D2H)");
  auto& stats = gpuExecutionStats();
  ++stats.deviceToHostCalls;
  stats.deviceToHostBytes += 2 * sizeof(unsigned int);

  return ResidentSystemCheck{host[0] == 0, host[1] == 0};
}

void copyToWorkspaceRhs(const DeviceBuffer<cfd::Real>& rhs, GpuKrylovWorkspace& workspace) {
  const cfd::Index n = rhs.size();
  workspace.b.resize(n);
  if (n == 0) return;
  checkCuda(cudaMemcpy(workspace.b.data(), rhs.data(),
                       static_cast<std::size_t>(n) * sizeof(cfd::Real), cudaMemcpyDeviceToDevice),
            "cudaMemcpy(resident rhs D2D)");
}

void copyToWorkspaceGuess(const DeviceBuffer<cfd::Real>& guess, GpuKrylovWorkspace& workspace) {
  const cfd::Index n = guess.size();
  workspace.x.resize(n);
  if (n == 0) return;
  checkCuda(cudaMemcpy(workspace.x.data(), guess.data(),
                       static_cast<std::size_t>(n) * sizeof(cfd::Real), cudaMemcpyDeviceToDevice),
            "cudaMemcpy(resident initial guess D2D)");
}

void copyWorkspaceSolution(const GpuKrylovWorkspace& workspace, cfd::Index count,
                           DeviceBuffer<cfd::Real>& out) {
  out.resize(count);
  if (count == 0) return;
  checkCuda(cudaMemcpy(out.data(), workspace.x.data(),
                       static_cast<std::size_t>(count) * sizeof(cfd::Real),
                       cudaMemcpyDeviceToDevice),
            "cudaMemcpy(resident solution D2D)");
}

}  // namespace cfd::gpu
