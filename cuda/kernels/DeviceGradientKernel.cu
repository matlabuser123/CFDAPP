// GPU-DISC-001B -- the CUDA Green-Gauss gradient.
//
// Every expression below is transcribed from the CPU reference, in the same
// association order, because the gate for this phase is BITWISE equality:
//
//   interior face value   Interpolation.cpp:29   ((dNf*phiP) + (dPf*phiN)) / (dPf + dNf)
//   boundary face value   Interpolation.cpp:112  bc.boundaryValue(phiP, d)  -> a + b*phiP
//   skew-corrected value  Interpolation.cpp:59   phiCrossing + dot(gradCrossing, skewVector)
//   oblique Neumann value Gradient.cpp:585       boundaryValue(phiP, d_n) + dot(grad_P, d_t)
//   claim correction      Gradient.cpp:145       -0.5 w (1-w) L^2 * secondDerivative
//   cell sum              Gradient.cpp:515       sum += Sf_cell * value; result = sum * (1/V)
//
// Two things are load-bearing and easy to lose:
//
// 1. NO FUSED MULTIPLY-ADD. The CPU library is built for baseline x86-64, which
//    has no FMA instruction, so `a*b + c` there is a rounded multiply followed
//    by a rounded add. nvcc contracts that into a single FMA by default, which
//    is MORE accurate and therefore NOT bitwise equal. This translation unit is
//    compiled with -fmad=false (see cuda/CMakeLists.txt). If that flag is ever
//    lost, the differential test fails -- it is not a silent divergence.
//
// 2. The cell sum is a GATHER over the cell's own faces, in the CSR row order,
//    which is exactly `cell.faceIds()` order. No atomics, no scatter, no
//    reordering: floating-point addition is not associative, so the summation
//    order is part of the answer.

#include <cuda_runtime.h>

#include "cfd/core/Timer.hpp"
#include "cfd/gpu/CudaCheck.hpp"
#include "cfd/gpu/DeviceGradient.hpp"
#include "cfd/gpu/GPUExecutionStats.hpp"

namespace cfd::gpu {

using cfd::Index;
using cfd::Real;

namespace {

constexpr int kThreadsPerBlock = 256;

int blockCountFor(Index count) {
  return static_cast<int>((count + kThreadsPerBlock - 1) / kThreadsPerBlock);
}

// cfd::dot (Vector3.hpp:78) is NOT the plain three-term sum: the z term is
// dropped entirely when it is exactly zero, so that a 2D field never turns an
// exact +0.0 into -0.0. Reproduced exactly -- on a 2D mesh the two forms differ
// in the sign of zero, which is a bitwise difference.
__device__ inline Real dotGuarded(Real ax, Real ay, Real az, Real bx, Real by, Real bz) {
  const Real inPlane = (ax * bx) + (ay * by);
  const Real normal = az * bz;
  return normal == 0.0 ? inPlane : inPlane + normal;
}

__global__ void interiorFaceValuesKernel(Index faceCount, const Index* __restrict__ faceOwner,
                                         const Index* __restrict__ faceNeighbor,
                                         const Real* __restrict__ dPf,
                                         const Real* __restrict__ dNf, const Real* __restrict__ phi,
                                         Real* __restrict__ faceValues) {
  const Index f = blockIdx.x * blockDim.x + threadIdx.x;
  if (f >= faceCount) return;
  const Index neighbor = faceNeighbor[f];
  if (neighbor == DeviceMesh::kNoNeighbor) return;  // boundary: written by the boundary kernel
  const Real dP = dPf[f];
  const Real dN = dNf[f];
  const Real phiP = phi[faceOwner[f]];
  const Real phiN = phi[neighbor];
  faceValues[f] = ((dN * phiP) + (dP * phiN)) / (dP + dN);
}

// The three encodings the host may have chosen. `shift` is not folded into
// `affine` with b = 1: a Neumann condition's own arithmetic is
// `ownerValue + (gradient * normalDistance)`, and a + 1.0*phi is not bitwise
// equal to phi + a for every input. See DeviceGradientPlan.cpp's encodeBoundary.
__device__ inline Real boundaryValueOf(Index kind, Real a, Real b, Real phiP) {
  if (kind == DeviceGradientPlan::kBoundaryConstant) return a;
  if (kind == DeviceGradientPlan::kBoundaryShift) return phiP + a;
  return a + (b * phiP);
}

__global__ void boundaryFaceValuesKernel(Index boundaryCount,
                                         const Index* __restrict__ boundaryFaceIds,
                                         const Index* __restrict__ faceOwner,
                                         const Real* __restrict__ a, const Real* __restrict__ b,
                                         const Index* __restrict__ kind,
                                         const Real* __restrict__ phi,
                                         Real* __restrict__ faceValues) {
  const Index i = blockIdx.x * blockDim.x + threadIdx.x;
  if (i >= boundaryCount) return;
  const Index f = boundaryFaceIds[i];
  faceValues[f] = boundaryValueOf(kind[f], a[f], b[f], phi[faceOwner[f]]);
}

// P12-NUM-003. Only faces the host found to have a non-zero skew vector appear
// in this list, which is why there is no fallback branch here: the CPU's
// fallback inside interpolateInternalFaceSkewCorrected is unreachable for them.
__global__ void skewCorrectKernel(Index skewCount, const Index* __restrict__ skewFaceIds,
                                  const Index* __restrict__ faceOwner,
                                  const Index* __restrict__ faceNeighbor,
                                  const Real* __restrict__ tOfFace, const Real* __restrict__ svX,
                                  const Real* __restrict__ svY, const Real* __restrict__ svZ,
                                  const Real* __restrict__ phi, const Real* __restrict__ gradX,
                                  const Real* __restrict__ gradY, const Real* __restrict__ gradZ,
                                  Real* __restrict__ faceValues) {
  const Index i = blockIdx.x * blockDim.x + threadIdx.x;
  if (i >= skewCount) return;
  const Index f = skewFaceIds[i];
  const Index o = faceOwner[f];
  const Index n = faceNeighbor[f];
  const Real t = tOfFace[i];
  const Real phiCrossing = phi[o] + (t * (phi[n] - phi[o]));
  const Real gx = gradX[o] + ((gradX[n] - gradX[o]) * t);
  const Real gy = gradY[o] + ((gradY[n] - gradY[o]) * t);
  const Real gz = gradZ[o] + ((gradZ[n] - gradZ[o]) * t);
  faceValues[f] = phiCrossing + dotGuarded(gx, gy, gz, svX[i], svY[i], svZ[i]);
}

// P12-MESH-001: re-evaluated inside every sweep with the latest owner gradient.
// The affine pair here is encoded at the NORMAL distance d.n, not the
// straight-line distance the initial boundary value used.
__global__ void obliqueNeumannKernel(Index obliqueCount, const Index* __restrict__ obliqueFaceIds,
                                     const Index* __restrict__ faceOwner,
                                     const Real* __restrict__ a, const Real* __restrict__ b,
                                     const Index* __restrict__ kind,
                                     const Real* __restrict__ tX, const Real* __restrict__ tY,
                                     const Real* __restrict__ tZ, const Real* __restrict__ phi,
                                     const Real* __restrict__ gradX, const Real* __restrict__ gradY,
                                     const Real* __restrict__ gradZ,
                                     Real* __restrict__ faceValues) {
  const Index i = blockIdx.x * blockDim.x + threadIdx.x;
  if (i >= obliqueCount) return;
  const Index f = obliqueFaceIds[i];
  const Index o = faceOwner[f];
  faceValues[f] = boundaryValueOf(kind[i], a[i], b[i], phi[o]) +
                  dotGuarded(gradX[o], gradY[o], gradZ[o], tX[i], tY[i], tZ[i]);
}

// P12-GRAD-002. The host already chose the winning claim per (cell, target
// face); what is left is the part that depends on the field and on the previous
// sweep's gradient.
__global__ void claimValuesKernel(Index claimCount, const Index* __restrict__ claimCell,
                                  const Index* __restrict__ claimTargetFace,
                                  const Index* __restrict__ claimBoundaryFace,
                                  const Index* __restrict__ claimFarCell,
                                  const Real* __restrict__ farDistance,
                                  const Real* __restrict__ backDistance,
                                  const Real* __restrict__ w, const Real* __restrict__ offX,
                                  const Real* __restrict__ offY, const Real* __restrict__ offZ,
                                  const Real* __restrict__ phi,
                                  const Real* __restrict__ faceValues,
                                  const Real* __restrict__ gradX, const Real* __restrict__ gradY,
                                  const Real* __restrict__ gradZ, Real* __restrict__ claimValue) {
  const Index k = blockIdx.x * blockDim.x + threadIdx.x;
  if (k >= claimCount) return;
  const Index c = claimCell[k];
  const Real phiBoundary =
      faceValues[claimBoundaryFace[k]] +
      dotGuarded(gradX[c], gradY[c], gradZ[c], offX[k], offY[k], offZ[k]);
  const Real phiP = phi[c];
  const Real phiFar = phi[claimFarCell[k]];
  const Real back = backDistance[k];
  const Real far = farDistance[k];
  const Real secondDerivative =
      2.0 * (((phiBoundary - phiP) / back) + ((phiFar - phiP) / far)) / (back + far);
  const Real ww = w[k];
  const Real correction = -0.5 * ww * (1.0 - ww) * far * far * secondDerivative;
  claimValue[k] = faceValues[claimTargetFace[k]] + correction;
}

__global__ void greenGaussSumKernel(Index cellCount, const Index* __restrict__ cellFaceOffsets,
                                    const Index* __restrict__ cellFaceIds,
                                    const Index* __restrict__ faceOwner,
                                    const Real* __restrict__ areaX, const Real* __restrict__ areaY,
                                    const Real* __restrict__ areaZ,
                                    const Real* __restrict__ faceValues,
                                    const Index* __restrict__ slotClaim,
                                    const Real* __restrict__ claimValue,
                                    const Real* __restrict__ cellVolumes, Real* __restrict__ gradX,
                                    Real* __restrict__ gradY, Real* __restrict__ gradZ) {
  const Index c = blockIdx.x * blockDim.x + threadIdx.x;
  if (c >= cellCount) return;
  Real sumX = 0.0;
  Real sumY = 0.0;
  Real sumZ = 0.0;
  const Index begin = cellFaceOffsets[c];
  const Index end = cellFaceOffsets[c + 1];
  for (Index slot = begin; slot < end; ++slot) {
    const Index f = cellFaceIds[slot];
    Real sx = areaX[f];
    Real sy = areaY[f];
    Real sz = areaZ[f];
    if (faceOwner[f] != c) {
      // The CPU multiplies the area vector by -1.0 rather than negating it, so
      // the sign of a zero component follows the same rule.
      sx = sx * -1.0;
      sy = sy * -1.0;
      sz = sz * -1.0;
    }
    const Index claim = slotClaim[slot];
    const Real value = (claim == DeviceGradientPlan::kNoClaim) ? faceValues[f] : claimValue[claim];
    sumX = sumX + (sx * value);
    sumY = sumY + (sy * value);
    sumZ = sumZ + (sz * value);
  }
  // `sum * (1.0 / cell.volume())`, not `sum / cell.volume()`. These differ.
  const Real inverseVolume = 1.0 / cellVolumes[c];
  gradX[c] = sumX * inverseVolume;
  gradY[c] = sumY * inverseVolume;
  gradZ[c] = sumZ * inverseVolume;
}

// Every launch site records the same counters the rest of the GPU path does,
// and none of them synchronizes: the only consumer of each kernel's output is
// another kernel on the default stream (GPU-PIPE-001 Phase 3).
void recordLaunch(const char* what, cfd::Timer& timer) {
  checkCuda(cudaGetLastError(), what);
  auto& stats = gpuExecutionStats();
  ++stats.kernelLaunches;
  stats.kernelSeconds += timer.elapsedSeconds();
}

}  // namespace

void greenGaussGradientDevice(const DeviceGradientPlan& plan, const DeviceBuffer<cfd::Real>& phi,
                              Index sweeps, DeviceBuffer<cfd::Real>& gradX,
                              DeviceBuffer<cfd::Real>& gradY, DeviceBuffer<cfd::Real>& gradZ) {
  if (!plan.usable()) {
    throw cfd::InvalidArgumentError(
        "greenGaussGradientDevice: plan is not usable (" + plan.unsupportedReason() + ")");
  }
  const DeviceMesh& mesh = plan.mesh_;
  const Index nc = mesh.cellCount();
  const Index nf = mesh.faceCount();
  if (phi.size() != nc) {
    throw cfd::InvalidArgumentError(
        "greenGaussGradientDevice: field size does not match mesh cell count");
  }
  gradX.resize(nc);
  gradY.resize(nc);
  gradZ.resize(nc);
  if (nc == 0) return;

  plan.faceValues_.resize(nf);
  plan.claimValues_.resize(plan.claimCount_);

  // greenGaussSweep's first call receives previousGradient == nullptr and uses
  // Vector2{0,0} as the owner gradient. cudaMemset gives +0.0, the same bits.
  checkCuda(cudaMemset(gradX.data(), 0, static_cast<std::size_t>(nc) * sizeof(Real)),
            "cudaMemset(gradX)");
  checkCuda(cudaMemset(gradY.data(), 0, static_cast<std::size_t>(nc) * sizeof(Real)),
            "cudaMemset(gradY)");
  checkCuda(cudaMemset(gradZ.data(), 0, static_cast<std::size_t>(nc) * sizeof(Real)),
            "cudaMemset(gradZ)");

  // --- faceValues = interpolate(mesh, field, boundaries) -------------------
  if (nf > 0) {
    cfd::Timer timer;
    interiorFaceValuesKernel<<<blockCountFor(nf), kThreadsPerBlock>>>(
        nf, mesh.faceOwner(), mesh.faceNeighbor(), plan.faceDPf_.data(), plan.faceDNf_.data(),
        phi.data(), plan.faceValues_.data());
    recordLaunch("interiorFaceValuesKernel launch", timer);
  }
  const Index nBoundary = mesh.boundaryFaceCount();
  if (nBoundary > 0) {
    cfd::Timer timer;
    boundaryFaceValuesKernel<<<blockCountFor(nBoundary), kThreadsPerBlock>>>(
        nBoundary, mesh.boundaryFaceIds(), mesh.faceOwner(), plan.boundaryA_.data(),
        plan.boundaryB_.data(), plan.boundaryKind_.data(), phi.data(), plan.faceValues_.data());
    recordLaunch("boundaryFaceValuesKernel launch", timer);
  }

  const Index nClaims = plan.claimCount_;
  const Index nSkew = plan.skewedFaceIds_.size();
  const Index nOblique = plan.obliqueFaceIds_.size();

  const auto claimsAndSum = [&]() {
    if (nClaims > 0) {
      cfd::Timer timer;
      claimValuesKernel<<<blockCountFor(nClaims), kThreadsPerBlock>>>(
          nClaims, plan.claimCell_.data(), plan.claimTargetFace_.data(),
          plan.claimBoundaryFace_.data(), plan.claimFarCell_.data(), plan.claimFarDistance_.data(),
          plan.claimBackDistance_.data(), plan.claimW_.data(), plan.claimOffsetX_.data(),
          plan.claimOffsetY_.data(), plan.claimOffsetZ_.data(), phi.data(),
          plan.faceValues_.data(), gradX.data(), gradY.data(), gradZ.data(),
          plan.claimValues_.data());
      recordLaunch("claimValuesKernel launch", timer);
    }
    cfd::Timer timer;
    greenGaussSumKernel<<<blockCountFor(nc), kThreadsPerBlock>>>(
        nc, mesh.cellFaceOffsets(), mesh.cellFaceIds(), mesh.faceOwner(), mesh.faceAreaX(),
        mesh.faceAreaY(), mesh.faceAreaZ(), plan.faceValues_.data(), plan.slotClaim_.data(),
        plan.claimValues_.data(), mesh.cellVolumes(), gradX.data(), gradY.data(), gradZ.data());
    recordLaunch("greenGaussSumKernel launch", timer);
  };

  // result = greenGaussSweep(mesh, field, faceValues, nullptr)
  claimsAndSum();

  if (!plan.sweepsNeeded_) return;

  for (Index sweep = 0; sweep < sweeps; ++sweep) {
    if (nSkew > 0) {
      cfd::Timer timer;
      skewCorrectKernel<<<blockCountFor(nSkew), kThreadsPerBlock>>>(
          nSkew, plan.skewedFaceIds_.data(), mesh.faceOwner(), mesh.faceNeighbor(),
          plan.skewTOfFace_.data(), plan.skewVecX_.data(), plan.skewVecY_.data(),
          plan.skewVecZ_.data(), phi.data(), gradX.data(), gradY.data(), gradZ.data(),
          plan.faceValues_.data());
      recordLaunch("skewCorrectKernel launch", timer);
    }
    if (nOblique > 0) {
      cfd::Timer timer;
      obliqueNeumannKernel<<<blockCountFor(nOblique), kThreadsPerBlock>>>(
          nOblique, plan.obliqueFaceIds_.data(), mesh.faceOwner(), plan.obliqueA_.data(),
          plan.obliqueB_.data(), plan.obliqueKind_.data(), plan.obliqueTangentX_.data(),
          plan.obliqueTangentY_.data(),
          plan.obliqueTangentZ_.data(), phi.data(), gradX.data(), gradY.data(), gradZ.data(),
          plan.faceValues_.data());
      recordLaunch("obliqueNeumannKernel launch", timer);
    }
    // greenGaussSweep(mesh, field, faceValues, &result): the claim kernel reads
    // the previous sweep's gradient and completes before the sum kernel
    // overwrites it, guaranteed by stream ordering on the default stream.
    claimsAndSum();
  }
}

}  // namespace cfd::gpu
