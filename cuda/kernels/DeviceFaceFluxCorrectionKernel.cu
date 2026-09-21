// GPU-DISC-001K -- the CUDA face-flux correction.
//
// Transcribed from correctFaceMassFlux (PressureCorrectionEquation.cpp:369).
// See the header for what is load-bearing and why there is no plan.

#include <cuda_runtime.h>

#include "cfd/core/Timer.hpp"
#include "cfd/gpu/CudaCheck.hpp"
#include "cfd/gpu/DeviceFaceFluxCorrection.hpp"
#include "cfd/gpu/GPUExecutionStats.hpp"

namespace cfd::gpu {

using cfd::Index;
using cfd::Real;

namespace {

constexpr int kThreadsPerBlock = 256;

int blockCountFor(Index count) {
  return static_cast<int>((count + kThreadsPerBlock - 1) / kThreadsPerBlock);
}

__global__ void correctFaceMassFluxKernel(Index faceCount, const Index* __restrict__ faceOwner,
                                          const Index* __restrict__ faceNeighbor,
                                          const Real* __restrict__ predictorMassFlux,
                                          const Real* __restrict__ faceCoefficient,
                                          const Real* __restrict__ pressureCorrection,
                                          const Real* __restrict__ explicitFaceFlux,
                                          Real* __restrict__ corrected) {
  const Index f = blockIdx.x * blockDim.x + threadIdx.x;
  if (f >= faceCount) return;

  const Real pOwner = pressureCorrection[faceOwner[f]];
  const Index neighbor = faceNeighbor[f];
  // A boundary face takes a LITERAL 0.0 for the neighbour value -- the
  // Dirichlet p' = 0 the assembly put in the matrix. It is not skipped: what
  // makes a Neumann-like boundary face a no-op is faceCoefficient being
  // exactly 0.0 there, not a branch.
  const Real pNeighbor = neighbor == DeviceMesh::kNoNeighbor ? 0.0 : pressureCorrection[neighbor];
  const Real fluxCorrection = faceCoefficient[f] * (pOwner - pNeighbor);
  Real value = predictorMassFlux[f] + fluxCorrection;
  // A SEPARATE addition, matching the CPU's `+=`: (F* + F') + E.
  if (explicitFaceFlux != nullptr) value = value + explicitFaceFlux[f];
  corrected[f] = value;
}

void recordLaunch(const char* what, cfd::Timer& timer) {
  checkCuda(cudaGetLastError(), what);
  auto& stats = gpuExecutionStats();
  ++stats.kernelLaunches;
  stats.kernelSeconds += timer.elapsedSeconds();
}

}  // namespace

void correctFaceMassFluxDevice(const DeviceMesh& mesh,
                               const DeviceBuffer<Real>& predictorMassFlux,
                               const DeviceBuffer<Real>& faceCoefficient,
                               const DeviceBuffer<Real>& pressureCorrection,
                               const DeviceBuffer<Real>* explicitFaceFlux,
                               DeviceBuffer<Real>& corrected) {
  const Index faceCount = mesh.faceCount();
  const Index cellCount = mesh.cellCount();
  if (predictorMassFlux.size() < faceCount || faceCoefficient.size() < faceCount) {
    throw cfd::InvalidArgumentError(
        "correctFaceMassFluxDevice: field size does not match mesh face count");
  }
  if (explicitFaceFlux != nullptr && explicitFaceFlux->size() < faceCount) {
    throw cfd::InvalidArgumentError(
        "correctFaceMassFluxDevice: explicitFaceFlux size does not match mesh face count");
  }
  if (pressureCorrection.size() < cellCount) {
    throw cfd::InvalidArgumentError(
        "correctFaceMassFluxDevice: pressureCorrection size does not match mesh cell count");
  }

  corrected.resize(faceCount);
  if (faceCount == 0) return;

  cfd::Timer timer;
  correctFaceMassFluxKernel<<<blockCountFor(faceCount), kThreadsPerBlock>>>(
      faceCount, mesh.faceOwner(), mesh.faceNeighbor(), predictorMassFlux.data(),
      faceCoefficient.data(), pressureCorrection.data(),
      explicitFaceFlux == nullptr ? nullptr : explicitFaceFlux->data(), corrected.data());
  recordLaunch("correctFaceMassFluxKernel", timer);
}

}  // namespace cfd::gpu
