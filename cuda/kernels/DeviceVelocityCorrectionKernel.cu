// GPU-DISC-001J -- the CUDA cell-velocity correction.
//
// Transcribed from correctVelocity (PressureCorrectionEquation.cpp:404) and
// makeGradientBoundaries (:43).
//
// Three things are load-bearing:
//
// 1. NO FUSED MULTIPLY-ADD -- compiled with -fmad=false. Each component is
//    `u - (d * g)`: one rounded product, then one rounded subtract. Contracting
//    them would change the result on every cell.
//
// 2. THE 2D W CONTRACT. `Vector2{x, y}` value-initialises z to exactly +0.0, so
//    the 2D branch DISCARDS the predictor's z. The z buffer is still written
//    (cellCount zeros), not left unwritten: the CPU produces a full VectorField
//    and a downstream read of an unwritten buffer is exactly what initcheck
//    caught during convection qualification.
//
// 3. The branch is the W-response POINTER, not the mesh dimension.

#include <cuda_runtime.h>

#include "cfd/core/Timer.hpp"
#include "cfd/discretization/Gradient.hpp"
#include "cfd/gpu/CudaCheck.hpp"
#include "cfd/gpu/DeviceVelocityCorrection.hpp"
#include "cfd/gpu/GPUExecutionStats.hpp"

namespace cfd::gpu {

using cfd::Index;
using cfd::Real;

namespace {

constexpr int kThreadsPerBlock = 256;

int blockCountFor(Index count) {
  return static_cast<int>((count + kThreadsPerBlock - 1) / kThreadsPerBlock);
}

__global__ void correctVelocityKernel(Index cellCount, const Real* __restrict__ ux,
                                      const Real* __restrict__ uy, const Real* __restrict__ uz,
                                      const Real* __restrict__ dU, const Real* __restrict__ dV,
                                      const Real* __restrict__ dW, const Real* __restrict__ gradX,
                                      const Real* __restrict__ gradY, const Real* __restrict__ gradZ,
                                      Real* __restrict__ outX, Real* __restrict__ outY,
                                      Real* __restrict__ outZ) {
  const Index c = blockIdx.x * blockDim.x + threadIdx.x;
  if (c >= cellCount) return;
  outX[c] = ux[c] - (dU[c] * gradX[c]);
  outY[c] = uy[c] - (dV[c] * gradY[c]);
  if (dW == nullptr) {
    // Vector2{x, y}: z is value-initialised, NOT carried over from uz.
    outZ[c] = 0.0;
    return;
  }
  outZ[c] = uz[c] - (dW[c] * gradZ[c]);
}

void recordLaunch(const char* what, cfd::Timer& timer) {
  checkCuda(cudaGetLastError(), what);
  auto& stats = gpuExecutionStats();
  ++stats.kernelLaunches;
  stats.kernelSeconds += timer.elapsedSeconds();
}

}  // namespace

void correctVelocityDevice(const DeviceVelocityCorrectionPlan& plan,
                           const DeviceVelocity& predictorVelocity, const DeviceBuffer<Real>& dU,
                           const DeviceBuffer<Real>& dV, const DeviceBuffer<Real>* dW,
                           const DeviceBuffer<Real>& pressureCorrection, Index scheme,
                           DeviceBuffer<Real>& gradX, DeviceBuffer<Real>& gradY,
                           DeviceBuffer<Real>& gradZ, DeviceVelocity& corrected) {
  if (!plan.usable()) {
    throw cfd::InvalidArgumentError("correctVelocityDevice: plan is not usable");
  }
  const Index cellCount = plan.cellCount();
  if (predictorVelocity.x.size() < cellCount || predictorVelocity.y.size() < cellCount ||
      predictorVelocity.z.size() < cellCount || dU.size() < cellCount || dV.size() < cellCount ||
      pressureCorrection.size() < cellCount) {
    throw cfd::InvalidArgumentError("correctVelocityDevice: field size does not match mesh cell count");
  }
  // requireWResponse (PressureCorrectionEquation.cpp:132), reproduced: a 3D
  // mesh needs the w response; a supplied one must match the cell count.
  if (plan.threeDimensionalMesh() && dW == nullptr) {
    throw cfd::InvalidArgumentError("correctVelocityDevice: a 3D mesh needs the w response coefficient");
  }
  if (dW != nullptr && dW->size() < cellCount) {
    throw cfd::InvalidArgumentError(
        "correctVelocityDevice: w response coefficient size does not match mesh cell count");
  }

  // grad(p') with the p' boundary set the plan already built.
  if (scheme == kGradientSchemeLeastSquares) {
    leastSquaresGradientDevice(plan.leastSquares(), pressureCorrection, gradX, gradY, gradZ);
  } else {
    greenGaussGradientDevice(plan.greenGauss(), pressureCorrection,
                             cfd::discretization::kGreenGaussSkewCorrectionSweeps, gradX, gradY,
                             gradZ);
  }

  corrected.x.resize(cellCount);
  corrected.y.resize(cellCount);
  corrected.z.resize(cellCount);
  if (cellCount == 0) return;

  cfd::Timer timer;
  correctVelocityKernel<<<blockCountFor(cellCount), kThreadsPerBlock>>>(
      cellCount, predictorVelocity.x.data(), predictorVelocity.y.data(),
      predictorVelocity.z.data(), dU.data(), dV.data(), dW == nullptr ? nullptr : dW->data(),
      gradX.data(), gradY.data(), gradZ.data(), corrected.x.data(), corrected.y.data(),
      corrected.z.data());
  recordLaunch("correctVelocityKernel", timer);
}

}  // namespace cfd::gpu
