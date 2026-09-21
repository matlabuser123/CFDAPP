// GPU-DISC-001G -- the CUDA momentum response coefficient.
//
// CUDA port of cfd::pressure_velocity::computeMomentumResponseCoefficient
// (PressureCorrectionEquation.cpp:103):
//
//     d_P = V_P / aP_P
//
// one value per cell. That is the entire CPU implementation; see
// results/gpu-disc-001/momentum-response/audit.md.
//
// Two properties worth stating, because both are unusual in this sequence:
//
// 1. There is NO accumulation. Every cell is a pure function of its own volume
//    and its own diagonal entry, so a per-cell kernel is a faithful mapping and
//    there is no ordering question -- unlike every assembly ported so far.
//
// 2. There are NO safeguards, deliberately. The CPU does an unguarded division
//    because its input contract ("already positive, finite, and nonzero") is
//    enforced upstream at assembly time. Adding a guard here would be a change
//    of numerical semantics, not a port. The unguarded division is reproduced.
//
// Compiled with -fmad=false, like every other operator kernel.

#include <cuda_runtime.h>

#include "cfd/core/Timer.hpp"
#include "cfd/gpu/CudaCheck.hpp"
#include "cfd/gpu/DeviceMomentumResponse.hpp"
#include "cfd/gpu/GPUExecutionStats.hpp"

namespace cfd::gpu {

using cfd::Index;
using cfd::Real;

namespace {

constexpr int kThreadsPerBlock = 256;

__global__ void responseKernel(Index cellCount, const Real* __restrict__ cellVolumes,
                               const Real* __restrict__ momentumDiagonal,
                               Real* __restrict__ response) {
  const Index c = blockIdx.x * blockDim.x + threadIdx.x;
  if (c >= cellCount) return;
  response[c] = cellVolumes[c] / momentumDiagonal[c];
}

}  // namespace

void computeMomentumResponseCoefficientDevice(const DeviceMesh& mesh,
                                              const DeviceBuffer<cfd::Real>& momentumDiagonal,
                                              DeviceBuffer<cfd::Real>& response) {
  const Index nc = mesh.cellCount();
  if (momentumDiagonal.size() != nc) {
    throw cfd::InvalidArgumentError(
        "computeMomentumResponseCoefficientDevice: momentumDiagonal size does not match mesh cell "
        "count");
  }
  response.resize(nc);
  if (nc == 0) return;

  const int blocks = static_cast<int>((nc + kThreadsPerBlock - 1) / kThreadsPerBlock);
  cfd::Timer timer;
  responseKernel<<<blocks, kThreadsPerBlock>>>(nc, mesh.cellVolumes(), momentumDiagonal.data(),
                                               response.data());
  checkCuda(cudaGetLastError(), "responseKernel launch");
  auto& stats = gpuExecutionStats();
  ++stats.kernelLaunches;
  stats.kernelSeconds += timer.elapsedSeconds();
  // No synchronize and no transfer: the result stays device-resident for the
  // Rhie-Chow / pressure-correction work that will consume it.
}

}  // namespace cfd::gpu
