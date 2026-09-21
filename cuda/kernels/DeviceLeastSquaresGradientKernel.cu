// GPU-DISC-001J -- the CUDA least-squares scalar gradient.
//
// Transcribed from leastSquaresGradient (Gradient.cpp:361),
// solveLeastSquaresGradient (:297) and solveLeastSquaresGradient3D (:240).
//
// Four things are load-bearing:
//
// 1. NO FUSED MULTIPLY-ADD -- compiled with -fmad=false.
//
// 2. The Green-Gauss fallback is computed for the WHOLE mesh FIRST, exactly as
//    the CPU does, and this kernel overwrites only the cells whose local system
//    is well conditioned. An ill-conditioned cell therefore keeps the
//    Green-Gauss value without this kernel having to reproduce it.
//
// 3. `weight * d * dphi` is evaluated as ((weight * d) * dphi). The plan stores
//    the inner product already rounded, so multiplying it by dphi here is
//    bitwise what the CPU computes -- and the same rounded value built S on the
//    host.
//
// 4. The 2x2 solve reads its matrix entries from a PACKED layout -- the plan
//    stores the 2D system's Syy, Sxy, Sxx in c11, c12, c22 so the kernel needs
//    only one set of arrays for both dimensions:
//        gx = (bx*Syy - by*Sxy)/det      gy = (Sxx*by - Sxy*bx)/det
//    Getting that packing backwards (gx reading Sxx, gy reading Syy) is a real
//    defect and negative control H9b catches it. The factor order WITHIN each
//    product is not: IEEE multiplication is commutative, so `Sxx*by` and
//    `by*Sxx` are bitwise identical. The source's asymmetric spelling is
//    cosmetic, and control H9 is kept as the documented proof of that.

#include <cuda_runtime.h>

#include "cfd/core/Timer.hpp"
#include "cfd/discretization/Gradient.hpp"
#include "cfd/gpu/CudaCheck.hpp"
#include "cfd/gpu/DeviceLeastSquaresGradient.hpp"
#include "cfd/gpu/GPUExecutionStats.hpp"

namespace cfd::gpu {

using cfd::Index;
using cfd::Real;

struct LeastSquaresGradientView {
  const Index* entryOffsets;
  const Index* entryKind;
  const Index* entryNeighbor;
  const Real* entryA;
  const Real* entryB;
  const Index* entryEncoding;
  const Real* wdX;
  const Real* wdY;
  const Real* wdZ;
  const Index* cellThreeD;
  const Index* cellConditioned;
  const Real* c11;
  const Real* c12;
  const Real* c13;
  const Real* c22;
  const Real* c23;
  const Real* c33;
  const Real* det;
};

namespace {

constexpr int kThreadsPerBlock = 256;

int blockCountFor(Index count) {
  return static_cast<int>((count + kThreadsPerBlock - 1) / kThreadsPerBlock);
}

// boundaryValue(phiP, d), in the form the host verified bitwise.
__device__ inline Real lsBoundaryValue(Index encoding, Real a, Real b, Real phiP) {
  if (encoding == kBoundaryEncodingConstant) return a;
  if (encoding == kBoundaryEncodingShift) return phiP + a;
  return a + (b * phiP);
}

__global__ void leastSquaresKernel(Index cellCount, LeastSquaresGradientView p,
                                   const Real* __restrict__ phi, Real* __restrict__ gradX,
                                   Real* __restrict__ gradY, Real* __restrict__ gradZ) {
  const Index c = blockIdx.x * blockDim.x + threadIdx.x;
  if (c >= cellCount) return;
  // Ill conditioned on the host's field-independent test: the Green-Gauss
  // value already in gradX/Y/Z is the answer, untouched.
  if (p.cellConditioned[c] == 0) return;

  const Real phiP = phi[c];
  const bool threeD = p.cellThreeD[c] != 0;

  Real bx = 0.0, by = 0.0, bz = 0.0;
  for (Index k = p.entryOffsets[c]; k < p.entryOffsets[c + 1]; ++k) {
    const Index kind = p.entryKind[k];
    if (kind == kLsEntrySkipped) continue;  // the CPU's zero-distance `continue`
    Real dphi;
    if (kind == kLsEntryInterior) {
      dphi = phi[p.entryNeighbor[k]] - phiP;
    } else {
      dphi = lsBoundaryValue(p.entryEncoding[k], p.entryA[k], p.entryB[k], phiP) - phiP;
    }
    bx = bx + (p.wdX[k] * dphi);
    by = by + (p.wdY[k] * dphi);
    if (threeD) bz = bz + (p.wdZ[k] * dphi);
  }

  const Real determinant = p.det[c];
  Real gx, gy, gz;
  if (threeD) {
    const Real k11 = p.c11[c], k12 = p.c12[c], k13 = p.c13[c];
    const Real k22 = p.c22[c], k23 = p.c23[c], k33 = p.c33[c];
    gx = ((k11 * bx) + (k12 * by) + (k13 * bz)) / determinant;
    gy = ((k12 * bx) + (k22 * by) + (k23 * bz)) / determinant;
    gz = ((k13 * bx) + (k23 * by) + (k33 * bz)) / determinant;
    if (!isfinite(gx) || !isfinite(gy) || !isfinite(gz)) return;  // fallback stays
  } else {
    // c11 = Syy, c12 = Sxy, c22 = Sxx (see the plan).
    const Real syy = p.c11[c], sxy = p.c12[c], sxx = p.c22[c];
    gx = ((bx * syy) - (by * sxy)) / determinant;
    gy = ((sxx * by) - (sxy * bx)) / determinant;
    if (!isfinite(gx) || !isfinite(gy)) return;  // fallback stays
    // The CPU returns Vector2{gx, gy}: z is value-initialised to exactly +0.0,
    // it is NOT the Green-Gauss fallback's z.
    gz = 0.0;
  }
  gradX[c] = gx;
  gradY[c] = gy;
  gradZ[c] = gz;
}

void recordLaunch(const char* what, cfd::Timer& timer) {
  checkCuda(cudaGetLastError(), what);
  auto& stats = gpuExecutionStats();
  ++stats.kernelLaunches;
  stats.kernelSeconds += timer.elapsedSeconds();
}

}  // namespace

void fillLeastSquaresGradientView(const DeviceLeastSquaresGradientPlan& plan,
                                  LeastSquaresGradientView& p) {
  p.entryOffsets = plan.entryOffsets_.data();
  p.entryKind = plan.entryKind_.data();
  p.entryNeighbor = plan.entryNeighbor_.data();
  p.entryA = plan.entryA_.data();
  p.entryB = plan.entryB_.data();
  p.entryEncoding = plan.entryEncoding_.data();
  p.wdX = plan.entryWdX_.data();
  p.wdY = plan.entryWdY_.data();
  p.wdZ = plan.entryWdZ_.data();
  p.cellThreeD = plan.cellThreeD_.data();
  p.cellConditioned = plan.cellConditioned_.data();
  p.c11 = plan.c11_.data();
  p.c12 = plan.c12_.data();
  p.c13 = plan.c13_.data();
  p.c22 = plan.c22_.data();
  p.c23 = plan.c23_.data();
  p.c33 = plan.c33_.data();
  p.det = plan.det_.data();
}

void leastSquaresGradientDevice(const DeviceLeastSquaresGradientPlan& plan,
                                const DeviceBuffer<Real>& phi, DeviceBuffer<Real>& gradX,
                                DeviceBuffer<Real>& gradY, DeviceBuffer<Real>& gradZ) {
  if (!plan.usable()) {
    throw cfd::InvalidArgumentError("leastSquaresGradientDevice: plan is not usable");
  }
  const Index cellCount = plan.cellCount();
  if (phi.size() < cellCount) {
    throw cfd::InvalidArgumentError("leastSquaresGradientDevice: field smaller than the mesh");
  }

  // The fallback first, for the WHOLE mesh -- the CPU computes it
  // unconditionally too, and this is what an ill-conditioned cell keeps.
  greenGaussGradientDevice(plan.greenGauss(), phi,
                           cfd::discretization::kGreenGaussSkewCorrectionSweeps, gradX, gradY,
                           gradZ);
  if (cellCount == 0) return;

  cfd::Timer timer;
  LeastSquaresGradientView view{};
  fillLeastSquaresGradientView(plan, view);
  leastSquaresKernel<<<blockCountFor(cellCount), kThreadsPerBlock>>>(
      cellCount, view, phi.data(), gradX.data(), gradY.data(), gradZ.data());
  recordLaunch("leastSquaresKernel", timer);
}

}  // namespace cfd::gpu
