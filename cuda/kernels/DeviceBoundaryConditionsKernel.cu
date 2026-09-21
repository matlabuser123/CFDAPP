// GPU-DISC-001E -- exercises the shared boundary-condition evaluators.
//
// The evaluators themselves live in DeviceBoundaryConditions.hpp so that every
// operator compiles the same code. This translation unit exists so they can be
// driven directly by a differential test, independently of any equation --
// which is what makes the BC layer qualifiable on its own rather than only
// through the operators that happen to use it.
//
// Compiled with -fmad=false, like every other operator kernel: the gate is
// bitwise equality with the CPU.

#include <cuda_runtime.h>

#include "cfd/core/Timer.hpp"
#include "cfd/gpu/CudaCheck.hpp"
#include "cfd/gpu/DeviceBoundaryConditions.hpp"
#include "cfd/gpu/GPUExecutionStats.hpp"

namespace cfd::gpu {

using cfd::Index;
using cfd::Real;

namespace {

constexpr int kThreadsPerBlock = 256;

int blockCountFor(Index count) {
  return static_cast<int>((count + kThreadsPerBlock - 1) / kThreadsPerBlock);
}

__global__ void evaluateKernel(Index faceCount, DeviceBoundaryView bc,
                               const Index* __restrict__ faceOwner,
                               const Index* __restrict__ faceNeighbor,
                               const Real* __restrict__ scalarField,
                               const Real* __restrict__ ux, const Real* __restrict__ uy,
                               const Real* __restrict__ uz, const Real* __restrict__ faceFlux,
                               Real* __restrict__ outScalar, Real* __restrict__ outAlt,
                               Real* __restrict__ outVecX, Real* __restrict__ outVecY,
                               Real* __restrict__ outVecZ, Real* __restrict__ outGhost,
                               Index* __restrict__ outType, Index* __restrict__ outPrescribes) {
  const Index f = blockIdx.x * blockDim.x + threadIdx.x;
  if (f >= faceCount) return;
  if (faceNeighbor[f] != DeviceMesh::kNoNeighbor) return;  // boundary faces only
  const Index owner = faceOwner[f];
  const Real phiP = scalarField[owner];
  outScalar[f] = bcEvaluateScalar(bc, f, phiP);
  outAlt[f] = bcEvaluateScalarAlt(bc, f, phiP);
  bcEvaluateVector(bc, f, ux[owner], uy[owner], uz[owner], outVecX[f], outVecY[f], outVecZ[f]);
  outGhost[f] = bcGhostValue(bc, f, phiP, faceFlux[f]);
  outType[f] = bcType(bc, f);
  outPrescribes[f] = bcPrescribesValue(bc, f) ? 1 : 0;
}

}  // namespace

void evaluateBoundaryConditionsDevice(const DeviceBoundaryConditions& conditions,
                                      const DeviceMesh& mesh,
                                      const DeviceBuffer<cfd::Real>& scalarField,
                                      const DeviceBuffer<cfd::Real>& velocityX,
                                      const DeviceBuffer<cfd::Real>& velocityY,
                                      const DeviceBuffer<cfd::Real>& velocityZ,
                                      const DeviceBuffer<cfd::Real>& faceFlux,
                                      DeviceBoundaryEvaluation& out) {
  if (!conditions.usable()) {
    throw cfd::InvalidArgumentError("evaluateBoundaryConditionsDevice: conditions are not usable (" +
                                    conditions.unsupportedReason() + ")");
  }
  const Index nf = mesh.faceCount();
  out.scalarValue.resize(nf);
  out.alternativeScalarValue.resize(nf);
  out.vectorX.resize(nf);
  out.vectorY.resize(nf);
  out.vectorZ.resize(nf);
  out.ghostValue.resize(nf);
  out.type.resize(nf);
  out.prescribesValue.resize(nf);
  if (nf == 0) return;

  // Interior entries are never written by the kernel, so they are defined here
  // rather than left as whatever the allocator returned -- initcheck caught
  // exactly this class of thing in 001D.
  const auto zero = [&](auto& buffer) {
    checkCuda(cudaMemset(buffer.data(), 0,
                         static_cast<std::size_t>(nf) * sizeof(*buffer.data())),
              "cudaMemset(boundary evaluation)");
  };
  zero(out.scalarValue);
  zero(out.alternativeScalarValue);
  zero(out.vectorX);
  zero(out.vectorY);
  zero(out.vectorZ);
  zero(out.ghostValue);
  zero(out.type);
  zero(out.prescribesValue);

  cfd::Timer timer;
  evaluateKernel<<<blockCountFor(nf), kThreadsPerBlock>>>(
      nf, conditions.view(), mesh.faceOwner(), mesh.faceNeighbor(), scalarField.data(),
      velocityX.data(), velocityY.data(), velocityZ.data(), faceFlux.data(),
      out.scalarValue.data(), out.alternativeScalarValue.data(), out.vectorX.data(),
      out.vectorY.data(), out.vectorZ.data(), out.ghostValue.data(), out.type.data(),
      out.prescribesValue.data());
  checkCuda(cudaGetLastError(), "boundary evaluateKernel launch");
  auto& stats = gpuExecutionStats();
  ++stats.kernelLaunches;
  stats.kernelSeconds += timer.elapsedSeconds();
}

}  // namespace cfd::gpu
