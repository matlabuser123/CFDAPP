// GPU-DISC-001D closure -- CUDA momentum convection contribution.
//
// Transcribed from physics::assembleConvectionContribution (MomentumEquation.cpp:264)
// and, for LinearUpwind's gradient, from discretization::greenGaussVelocityGradient
// (VectorGradient.cpp:51). The gate is BITWISE equality.
//
// Four things are load-bearing and easy to get subtly wrong:
//
// 1. NO FUSED MULTIPLY-ADD -- compiled with -fmad=false.
//
// 2. Central uses the VECTOR interpolateInternalFace,
//      ((uP * dNf) + (uN * dPf)) * (1.0 / (dPf + dNf))
//    -- a multiply by the reciprocal -- and then selects the component. The
//    SCALAR overload divides instead. The scalar convection operator uses the
//    divide form; this one must not.
//
// 3. The velocity gradient is NOT the scalar gradient of 001B. It has no
//    P12-GRAD-002 boundary treatment, it resolves vector boundary conditions,
//    and its cell sum is `sum * (1.0 / volume)` -- a multiply by the reciprocal,
//    where the scalar convection operator's cell sum is a divide.
//
// 4. On a 2D mesh gradW does not exist and the skew-corrected face value is
//    built as Vector2{x, y}, i.e. z is forced to exactly 0.0 rather than
//    carried through.

#include <cuda_runtime.h>

#include "cfd/core/Timer.hpp"
#include "cfd/discretization/Gradient.hpp"
#include "cfd/gpu/CudaCheck.hpp"
#include "cfd/gpu/DeviceConvectionTerms.hpp"
#include "cfd/gpu/DeviceMomentumConvection.hpp"
#include "cfd/gpu/GPUExecutionStats.hpp"

namespace cfd::gpu {

using cfd::Index;
using cfd::Real;

namespace {

constexpr int kThreadsPerBlock = 256;

int blockCountFor(Index count) {
  return static_cast<int>((count + kThreadsPerBlock - 1) / kThreadsPerBlock);
}

__device__ inline Real dotGuarded(Real ax, Real ay, Real az, Real bx, Real by, Real bz) {
  const Real inPlane = (ax * bx) + (ay * by);
  const Real normal = az * bz;
  return normal == 0.0 ? inPlane : inPlane + normal;
}

// The scheme primitives and the deferred face value now live in
// DeviceConvectionTerms.hpp so the momentum assembly calls the same code.
// Adapters below translate this file's view into that header's.

}  // namespace

struct ConvectionAssemblyView {
  const Index* faceOwner;
  const Index* faceNeighbor;
  const Real* areaX;
  const Real* areaY;
  const Real* areaZ;
  const Real* cellVolumes;
  const Index* cellFaceOffsets;
  const Index* cellFaceIds;
  const Real* dPf;
  const Real* dNf;
  const Real* offOX;
  const Real* offOY;
  const Real* offOZ;
  const Real* offNX;
  const Real* offNY;
  const Real* offNZ;
  const Index* farOValid;
  const Index* farOCell;
  const Real* farOHCU;
  const Index* farNValid;
  const Index* farNCell;
  const Real* farNHCU;
  const Index* bKind;
  const Real* bConstX;
  const Real* bConstY;
  const Real* bConstZ;
  const Real* bNormalX;
  const Real* bNormalY;
  const Real* bNormalZ;
  const Index* skewIds;
  const Real* skewT;
  const Real* skewVX;
  const Real* skewVY;
  const Real* skewVZ;
  const Index* sortedOffsets;
  const Index* sortedFaces;
  const Index* rowOffsets;
  const Index* columnIndices;
};

namespace {

using View = ConvectionAssemblyView;


// boundaryVelocity(): Wall/MovingWall/Inlet -> constant; Outlet -> the owner
// value; Symmetry -> u - (n * dot(u, n)), reproduced expression for expression.
__device__ inline DeviceVectorBoundaryView boundaryViewOf(const View& p) {
  DeviceVectorBoundaryView bc;
  bc.kind = p.bKind; bc.constX = p.bConstX; bc.constY = p.bConstY; bc.constZ = p.bConstZ;
  bc.normalX = p.bNormalX; bc.normalY = p.bNormalY; bc.normalZ = p.bNormalZ;
  return bc;
}

__device__ inline DeviceConvectionGeometry convectionGeometryOf(const View& p) {
  DeviceConvectionGeometry g;
  g.dPf = p.dPf; g.dNf = p.dNf;
  g.offOX = p.offOX; g.offOY = p.offOY; g.offOZ = p.offOZ;
  g.offNX = p.offNX; g.offNY = p.offNY; g.offNZ = p.offNZ;
  g.farOValid = p.farOValid; g.farOCell = p.farOCell; g.farOHCU = p.farOHCU;
  g.farNValid = p.farNValid; g.farNCell = p.farNCell; g.farNHCU = p.farNHCU;
  return g;
}

__device__ inline void boundaryVelocityOf(const View& p, Index f, Real ux, Real uy, Real uz,
                                          Real& outX, Real& outY, Real& outZ) {
  const DeviceVectorBoundaryView bc = boundaryViewOf(p);
  outX = deviceBoundaryVelocityComponent(bc, f, ux, uy, uz, kVelocityU);
  outY = deviceBoundaryVelocityComponent(bc, f, ux, uy, uz, kVelocityV);
  outZ = deviceBoundaryVelocityComponent(bc, f, ux, uy, uz, kVelocityW);
}

// --- velocity gradient (VectorGradient.cpp) -------------------------------

__global__ void faceVelocityKernel(Index faceCount, View p, const Real* __restrict__ ux,
                                   const Real* __restrict__ uy, const Real* __restrict__ uz,
                                   Real* __restrict__ fx, Real* __restrict__ fy,
                                   Real* __restrict__ fz) {
  const Index f = blockIdx.x * blockDim.x + threadIdx.x;
  if (f >= faceCount) return;
  const Index owner = p.faceOwner[f];
  const Index neighbor = p.faceNeighbor[f];
  if (neighbor == DeviceMesh::kNoNeighbor) {
    boundaryVelocityOf(p, f, ux[owner], uy[owner], uz[owner], fx[f], fy[f], fz[f]);
    return;
  }
  // VECTOR interpolateInternalFace: ((uP * dNf) + (uN * dPf)) * (1/(dPf + dNf))
  const Real dP = p.dPf[f];
  const Real dN = p.dNf[f];
  const Real inverse = 1.0 / (dP + dN);
  fx[f] = ((ux[owner] * dN) + (ux[neighbor] * dP)) * inverse;
  fy[f] = ((uy[owner] * dN) + (uy[neighbor] * dP)) * inverse;
  fz[f] = ((uz[owner] * dN) + (uz[neighbor] * dP)) * inverse;
}

__global__ void greenGaussSumKernel(Index cellCount, View p, bool threeDimensional,
                                    const Real* __restrict__ fx, const Real* __restrict__ fy,
                                    const Real* __restrict__ fz, Real* __restrict__ gUx,
                                    Real* __restrict__ gUy, Real* __restrict__ gUz,
                                    Real* __restrict__ gVx, Real* __restrict__ gVy,
                                    Real* __restrict__ gVz, Real* __restrict__ gWx,
                                    Real* __restrict__ gWy, Real* __restrict__ gWz) {
  const Index c = blockIdx.x * blockDim.x + threadIdx.x;
  if (c >= cellCount) return;
  Real sUx = 0.0, sUy = 0.0, sUz = 0.0;
  Real sVx = 0.0, sVy = 0.0, sVz = 0.0;
  Real sWx = 0.0, sWy = 0.0, sWz = 0.0;
  for (Index slot = p.cellFaceOffsets[c]; slot < p.cellFaceOffsets[c + 1]; ++slot) {
    const Index f = p.cellFaceIds[slot];
    Real sx = p.areaX[f], sy = p.areaY[f], sz = p.areaZ[f];
    if (p.faceOwner[f] != c) { sx = sx * -1.0; sy = sy * -1.0; sz = sz * -1.0; }
    const Real vx = fx[f], vy = fy[f], vz = fz[f];
    sUx = sUx + (sx * vx); sUy = sUy + (sy * vx); sUz = sUz + (sz * vx);
    sVx = sVx + (sx * vy); sVy = sVy + (sy * vy); sVz = sVz + (sz * vy);
    if (threeDimensional) {
      sWx = sWx + (sx * vz); sWy = sWy + (sy * vz); sWz = sWz + (sz * vz);
    }
  }
  const Real inverseVolume = 1.0 / p.cellVolumes[c];
  gUx[c] = sUx * inverseVolume; gUy[c] = sUy * inverseVolume; gUz[c] = sUz * inverseVolume;
  gVx[c] = sVx * inverseVolume; gVy[c] = sVy * inverseVolume; gVz[c] = sVz * inverseVolume;
  if (threeDimensional) {
    gWx[c] = sWx * inverseVolume; gWy[c] = sWy * inverseVolume; gWz[c] = sWz * inverseVolume;
  }
}

__global__ void skewCorrectVelocityKernel(Index skewCount, View p, bool threeDimensional,
                                          const Real* __restrict__ ux, const Real* __restrict__ uy,
                                          const Real* __restrict__ uz, const Real* __restrict__ gUx,
                                          const Real* __restrict__ gUy, const Real* __restrict__ gUz,
                                          const Real* __restrict__ gVx, const Real* __restrict__ gVy,
                                          const Real* __restrict__ gVz, const Real* __restrict__ gWx,
                                          const Real* __restrict__ gWy, const Real* __restrict__ gWz,
                                          Real* __restrict__ fx, Real* __restrict__ fy,
                                          Real* __restrict__ fz) {
  const Index i = blockIdx.x * blockDim.x + threadIdx.x;
  if (i >= skewCount) return;
  const Index f = p.skewIds[i];
  const Index o = p.faceOwner[f];
  const Index n = p.faceNeighbor[f];
  const Real t = p.skewT[i];
  const Real svx = p.skewVX[i], svy = p.skewVY[i], svz = p.skewVZ[i];

  const Real vcx = ux[o] + ((ux[n] - ux[o]) * t);
  const Real vcy = uy[o] + ((uy[n] - uy[o]) * t);
  const Real vcz = uz[o] + ((uz[n] - uz[o]) * t);

  const Real gxx = gUx[o] + ((gUx[n] - gUx[o]) * t);
  const Real gxy = gUy[o] + ((gUy[n] - gUy[o]) * t);
  const Real gxz = gUz[o] + ((gUz[n] - gUz[o]) * t);
  const Real gyx = gVx[o] + ((gVx[n] - gVx[o]) * t);
  const Real gyy = gVy[o] + ((gVy[n] - gVy[o]) * t);
  const Real gyz = gVz[o] + ((gVz[n] - gVz[o]) * t);

  fx[f] = vcx + dotGuarded(gxx, gxy, gxz, svx, svy, svz);
  fy[f] = vcy + dotGuarded(gyx, gyy, gyz, svx, svy, svz);
  if (!threeDimensional) {
    // The CPU returns Vector2{x, y} here, whose z is exactly 0.0 -- not the
    // interpolated z.
    fz[f] = 0.0;
    return;
  }
  const Real gzx = gWx[o] + ((gWx[n] - gWx[o]) * t);
  const Real gzy = gWy[o] + ((gWy[n] - gWy[o]) * t);
  const Real gzz = gWz[o] + ((gWz[n] - gWz[o]) * t);
  fz[f] = vcz + dotGuarded(gzx, gzy, gzz, svx, svy, svz);
}

// --- the convection contribution (MomentumEquation.cpp:264) ---------------

// Delegates to the shared implementation.
__device__ inline Real deferredFaceValue(const View& p, Index f, Real ownerFlux, Index scheme,
                                         Index component, const Real* __restrict__ ux,
                                         const Real* __restrict__ uy, const Real* __restrict__ uz,
                                         const Real* __restrict__ gx, const Real* __restrict__ gy,
                                         const Real* __restrict__ gz, Real& phiUpwindOut) {
  const DeviceConvectionGeometry g = convectionGeometryOf(p);
  return deviceDeferredFaceValue(g, p.faceOwner, p.faceNeighbor, f, ownerFlux, scheme, component,
                                 ux, uy, uz, gx, gy, gz, phiUpwindOut);
}

__global__ void momentumConvectionKernel(Index cellCount, View p, Index component, Index scheme,
                                         const Real* __restrict__ ux, const Real* __restrict__ uy,
                                         const Real* __restrict__ uz, const Real* __restrict__ gx,
                                         const Real* __restrict__ gy, const Real* __restrict__ gz,
                                         const Real* __restrict__ massFlux,
                                         Real* __restrict__ values, Real* __restrict__ rhs) {
  const Index c = blockIdx.x * blockDim.x + threadIdx.x;
  if (c >= cellCount) return;
  const Index faceBegin = p.sortedOffsets[c];
  const Index faceEnd = p.sortedOffsets[c + 1];

  // --- RHS, in face-id order --------------------------------------------
  Real rhsValue = 0.0;
  for (Index slot = faceBegin; slot < faceEnd; ++slot) {
    const Index f = p.sortedFaces[slot];
    const Index owner = p.faceOwner[f];
    const Index neighbor = p.faceNeighbor[f];
    const Real ownerFlux = massFlux[f];

    if (neighbor == DeviceMesh::kNoNeighbor) {
      if (ownerFlux >= 0.0) continue;  // outflow: implicit, no RHS term
      Real bx, by, bz;
      boundaryVelocityOf(p, f, ux[owner], uy[owner], uz[owner], bx, by, bz);
      const Real phiB = component == kVelocityU ? bx : (component == kVelocityV ? by : bz);
      rhsValue = rhsValue - (ownerFlux * phiB);
      continue;
    }
    if (scheme == kConvectionUpwind) continue;
    Real phiUpwind = 0.0;
    const Real phiFace = deferredFaceValue(p, f, ownerFlux, scheme, component, ux, uy, uz, gx, gy,
                                           gz, phiUpwind);
    const Real correction = ownerFlux * (phiFace - phiUpwind);
    // rhs[owner] -= correction ; rhs[neighbor] += correction
    rhsValue = (c == owner) ? (rhsValue - correction) : (rhsValue + correction);
  }
  rhs[c] = rhsValue;

  // --- matrix row, columns ascending, accumulated in face-id order -------
  for (Index k = p.rowOffsets[c]; k < p.rowOffsets[c + 1]; ++k) {
    const Index column = p.columnIndices[k];
    Real value = 0.0;
    for (Index slot = faceBegin; slot < faceEnd; ++slot) {
      const Index f = p.sortedFaces[slot];
      const Index owner = p.faceOwner[f];
      const Index neighbor = p.faceNeighbor[f];
      const Real ownerFlux = massFlux[f];
      if (neighbor == DeviceMesh::kNoNeighbor) {
        if (ownerFlux >= 0.0 && column == c) value = value + ownerFlux;
        continue;
      }
      if (ownerFlux >= 0.0) {
        // A(o,o) += Ff ; A(n,o) -= Ff
        if (c == owner && column == owner) value = value + ownerFlux;
        if (c == neighbor && column == owner) value = value - ownerFlux;
      } else {
        // A(o,n) += Ff ; A(n,n) -= Ff
        if (c == owner && column == neighbor) value = value + ownerFlux;
        if (c == neighbor && column == neighbor) value = value - ownerFlux;
      }
    }
    values[k] = value;
  }
}

void recordLaunch(const char* what, cfd::Timer& timer) {
  checkCuda(cudaGetLastError(), what);
  auto& stats = gpuExecutionStats();
  ++stats.kernelLaunches;
  stats.kernelSeconds += timer.elapsedSeconds();
}

}  // namespace

void fillConvectionAssemblyView(const DeviceMomentumConvectionPlan& plan,
                                ConvectionAssemblyView& p) {
  const DeviceMesh& mesh = plan.mesh_;
  p.faceOwner = mesh.faceOwner();
  p.faceNeighbor = mesh.faceNeighbor();
  p.areaX = mesh.faceAreaX();
  p.areaY = mesh.faceAreaY();
  p.areaZ = mesh.faceAreaZ();
  p.cellVolumes = mesh.cellVolumes();
  p.cellFaceOffsets = mesh.cellFaceOffsets();
  p.cellFaceIds = mesh.cellFaceIds();
  p.dPf = plan.faceDPf_.data();
  p.dNf = plan.faceDNf_.data();
  p.offOX = plan.offsetOwnerX_.data();
  p.offOY = plan.offsetOwnerY_.data();
  p.offOZ = plan.offsetOwnerZ_.data();
  p.offNX = plan.offsetNeighborX_.data();
  p.offNY = plan.offsetNeighborY_.data();
  p.offNZ = plan.offsetNeighborZ_.data();
  p.farOValid = plan.farOwnerValid_.data();
  p.farOCell = plan.farOwnerCell_.data();
  p.farOHCU = plan.farOwnerHCU_.data();
  p.farNValid = plan.farNeighborValid_.data();
  p.farNCell = plan.farNeighborCell_.data();
  p.farNHCU = plan.farNeighborHCU_.data();
  p.bKind = plan.bKind_.data();
  p.bConstX = plan.bConstX_.data();
  p.bConstY = plan.bConstY_.data();
  p.bConstZ = plan.bConstZ_.data();
  p.bNormalX = plan.bNormalX_.data();
  p.bNormalY = plan.bNormalY_.data();
  p.bNormalZ = plan.bNormalZ_.data();
  p.skewIds = plan.skewedFaceIds_.data();
  p.skewT = plan.skewT_.data();
  p.skewVX = plan.skewVecX_.data();
  p.skewVY = plan.skewVecY_.data();
  p.skewVZ = plan.skewVecZ_.data();
  p.sortedOffsets = plan.cellFaceOffsets_.data();
  p.sortedFaces = plan.cellFaceSorted_.data();
  p.rowOffsets = plan.rowOffsets_.data();
  p.columnIndices = plan.columnIndices_.data();
}

void computeVelocityGradientDevice(const DeviceMomentumConvectionPlan& plan,
                                   const DeviceVelocity& velocity,
                                   DeviceVelocityGradient& gradient) {
  if (!plan.usable()) {
    throw cfd::InvalidArgumentError("computeVelocityGradientDevice: plan is not usable (" +
                                    plan.unsupportedReason() + ")");
  }
  const Index nc = plan.cellCount_;
  const Index nf = plan.mesh_.faceCount();
  const bool threeD = plan.threeDimensional_;

  for (auto* b : {&gradient.gradUx, &gradient.gradUy, &gradient.gradUz, &gradient.gradVx,
                  &gradient.gradVy, &gradient.gradVz}) {
    b->resize(nc);
  }
  // VelocityGradientField leaves gradW EMPTY on a 2D mesh (there is no w), and
  // the device mirrors that rather than allocating a buffer no kernel writes.
  // Allocating it would leave it uninitialised -- compute-sanitizer's initcheck
  // caught exactly that, on the download in the differential harness.
  for (auto* b : {&gradient.gradWx, &gradient.gradWy, &gradient.gradWz}) {
    b->resize(threeD ? nc : 0);
  }
  plan.faceVelX_.resize(nf);
  plan.faceVelY_.resize(nf);
  plan.faceVelZ_.resize(nf);
  if (nc == 0) return;

  ConvectionAssemblyView p{};
  fillConvectionAssemblyView(plan, p);

  {
    cfd::Timer timer;
    faceVelocityKernel<<<blockCountFor(nf), kThreadsPerBlock>>>(
        nf, p, velocity.x.data(), velocity.y.data(), velocity.z.data(), plan.faceVelX_.data(),
        plan.faceVelY_.data(), plan.faceVelZ_.data());
    recordLaunch("faceVelocityKernel launch", timer);
  }
  const auto sum = [&]() {
    cfd::Timer timer;
    greenGaussSumKernel<<<blockCountFor(nc), kThreadsPerBlock>>>(
        nc, p, threeD, plan.faceVelX_.data(), plan.faceVelY_.data(), plan.faceVelZ_.data(),
        gradient.gradUx.data(), gradient.gradUy.data(), gradient.gradUz.data(),
        gradient.gradVx.data(), gradient.gradVy.data(), gradient.gradVz.data(),
        gradient.gradWx.data(), gradient.gradWy.data(), gradient.gradWz.data());
    recordLaunch("greenGaussSumKernel launch", timer);
  };
  sum();

  const Index nSkew = plan.skewedFaceIds_.size();
  if (nSkew == 0) return;
  for (Index sweep = 0; sweep < cfd::discretization::kGreenGaussSkewCorrectionSweeps; ++sweep) {
    cfd::Timer timer;
    skewCorrectVelocityKernel<<<blockCountFor(nSkew), kThreadsPerBlock>>>(
        nSkew, p, threeD, velocity.x.data(), velocity.y.data(), velocity.z.data(),
        gradient.gradUx.data(), gradient.gradUy.data(), gradient.gradUz.data(),
        gradient.gradVx.data(), gradient.gradVy.data(), gradient.gradVz.data(),
        gradient.gradWx.data(), gradient.gradWy.data(), gradient.gradWz.data(),
        plan.faceVelX_.data(), plan.faceVelY_.data(), plan.faceVelZ_.data());
    recordLaunch("skewCorrectVelocityKernel launch", timer);
    sum();
  }
}

void assembleMomentumConvectionDevice(const DeviceMomentumConvectionPlan& plan,
                                      const DeviceVelocity& velocity,
                                      const DeviceBuffer<cfd::Real>& massFlux, Index component,
                                      Index scheme, DeviceConvectionSystem& system) {
  if (!plan.usable()) {
    throw cfd::InvalidArgumentError("assembleMomentumConvectionDevice: plan is not usable (" +
                                    plan.unsupportedReason() + ")");
  }
  const Index nc = plan.cellCount_;
  system.rowOffsets.resize(nc + 1);
  system.columnIndices.resize(plan.entryCount_);
  system.values.resize(plan.entryCount_);
  system.rhs.resize(nc);
  if (nc + 1 > 0) {
    checkCuda(cudaMemcpy(system.rowOffsets.data(), plan.rowOffsets_.data(),
                         static_cast<std::size_t>(nc + 1) * sizeof(Index),
                         cudaMemcpyDeviceToDevice),
              "cudaMemcpy(momentum rowOffsets D2D)");
  }
  if (plan.entryCount_ > 0) {
    checkCuda(cudaMemcpy(system.columnIndices.data(), plan.columnIndices_.data(),
                         static_cast<std::size_t>(plan.entryCount_) * sizeof(Index),
                         cudaMemcpyDeviceToDevice),
              "cudaMemcpy(momentum columnIndices D2D)");
  }
  if (nc == 0) return;

  // Exactly as the CPU does: the velocity gradient is built only for
  // LinearUpwind, so the other three schemes pay nothing.
  const Real* gx = nullptr;
  const Real* gy = nullptr;
  const Real* gz = nullptr;
  if (scheme == kConvectionLinearUpwind) {
    computeVelocityGradientDevice(plan, velocity, plan.gradient_);
    if (component == kVelocityU) {
      gx = plan.gradient_.gradUx.data(); gy = plan.gradient_.gradUy.data();
      gz = plan.gradient_.gradUz.data();
    } else if (component == kVelocityV) {
      gx = plan.gradient_.gradVx.data(); gy = plan.gradient_.gradVy.data();
      gz = plan.gradient_.gradVz.data();
    } else {
      gx = plan.gradient_.gradWx.data(); gy = plan.gradient_.gradWy.data();
      gz = plan.gradient_.gradWz.data();
    }
  }

  ConvectionAssemblyView p{};
  fillConvectionAssemblyView(plan, p);
  cfd::Timer timer;
  momentumConvectionKernel<<<blockCountFor(nc), kThreadsPerBlock>>>(
      nc, p, component, scheme, velocity.x.data(), velocity.y.data(), velocity.z.data(), gx, gy,
      gz, massFlux.data(), system.values.data(), system.rhs.data());
  recordLaunch("momentumConvectionKernel launch", timer);
}

}  // namespace cfd::gpu
