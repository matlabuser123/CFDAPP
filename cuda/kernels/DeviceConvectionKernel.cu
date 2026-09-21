// GPU-DISC-001D -- the CUDA convection schemes.
//
// Transcribed from the CPU reference in the same association order, because the
// gate is BITWISE equality:
//
//   upwind selection      Convection.cpp:54   Ff >= 0 ? owner : neighbour
//   ghost boundary value  Convection.cpp:101  (2.0 * phiB) - phiOwner   (inflow only)
//   quickFaceValue        Convection.cpp:116  Lagrange weights, three true positions
//   linearUpwindFaceValue Convection.cpp:124  phiU + dot(grad, x_f - x_U)
//   smoothnessRatio       Convection.cpp:129  local == 0.0 -> nullopt
//   vanLeerLimiter        Convection.cpp:139  r <= 0 or nullopt -> 0
//   blend + clamp         Convection.cpp      phiU + psi*(hi - phiU), clamped to [min,max]
//   cell sum              Convection.cpp      sum / cell.volume()   <-- DIVIDE, not *(1/V)
//   scalar assembly       EnergyEquation.cpp:266
//
// Three things are load-bearing:
//
// 1. NO FUSED MULTIPLY-ADD -- compiled with -fmad=false (cuda/CMakeLists.txt).
//
// 2. The upwind test is `>= 0.0`, so a flux of -0.0 selects the OWNER. Writing
//    `> 0.0` or testing a sign bit would flip the stencil on a zero face.
//
// 3. `smoothnessRatio` returns "no value" only when the local gradient is
//    EXACTLY zero. std::optional is represented here as a NaN sentinel, and the
//    limiter treats NaN exactly as the CPU treats nullopt -- note `r <= 0.0` is
//    false for NaN, so the nullopt case must be tested explicitly first, as the
//    CPU does with has_value().

#include <cuda_runtime.h>

#include "cfd/core/Timer.hpp"
#include "cfd/discretization/Gradient.hpp"
#include "cfd/gpu/CudaCheck.hpp"
#include "cfd/gpu/DeviceConvection.hpp"
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

__device__ inline Real boundaryValueOf(Index kind, Real a, Real b, Real phiP) {
  if (kind == kBoundaryEncodingConstant) return a;
  if (kind == kBoundaryEncodingShift) return phiP + a;
  return a + (b * phiP);
}

// --- the production primitives, transcribed -------------------------------

__device__ inline Real quickFaceValueDevice(Real phiC, Real hCU, Real phiU, Real hUf, Real phiD,
                                            Real hfD) {
  const Real weightC = (-hUf * hfD) / (hCU * (hUf + hCU + hfD));
  const Real weightU = ((hUf + hCU) * hfD) / (hCU * (hUf + hfD));
  const Real weightD = (hUf * (hUf + hCU)) / ((hUf + hCU + hfD) * (hUf + hfD));
  return (weightC * phiC) + (weightU * phiU) + (weightD * phiD);
}

__device__ inline Real linearUpwindFaceValueDevice(Real phiUpwind, Real gx, Real gy, Real gz,
                                                   Real ox, Real oy, Real oz) {
  return phiUpwind + dotGuarded(gx, gy, gz, ox, oy, oz);
}

// std::optional<Real> as a NaN sentinel: NaN == "nullopt".
__device__ inline Real smoothnessRatioDevice(Real phiFarUpstream, Real phiUpwind,
                                             Real phiDownwind) {
  const Real localGradient = phiDownwind - phiUpwind;
  if (localGradient == 0.0) return nan("");
  return (phiUpwind - phiFarUpstream) / localGradient;
}

__device__ inline Real vanLeerLimiterDevice(Real r, bool hasValue) {
  // The CPU tests has_value() first, then r <= 0. `r <= 0.0` is FALSE for NaN,
  // so collapsing these two tests would let a nullopt through as a full blend.
  if (!hasValue || isnan(r) || r <= 0.0) return 0.0;
  return (r + fabs(r)) / (1.0 + fabs(r));
}

__device__ inline Real clampDevice(Real v, Real lo, Real hi) {
  // std::clamp(v, lo, hi) is `v < lo ? lo : (hi < v ? hi : v)`.
  return v < lo ? lo : (hi < v ? hi : v);
}

}  // namespace

struct ConvectionPlanView {
  const Index* faceOwner;
  const Index* faceNeighbor;
  const Real* cellVolumes;
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
  const Real* bValueA;
  const Real* bValueB;
  const Index* bValueKind;
  const Index* cellFaceOffsets;
  const Index* cellFaceIds;
  const Index* sortedOffsets;
  const Index* sortedFaces;
  const Index* rowOffsets;
  const Index* columnIndices;
};

namespace {

using PlanView = ConvectionPlanView;

// The face value cfd::discretization::convection() would use.
__device__ Real faceValueFor(const PlanView& p, const Real* __restrict__ phi,
                             const Real* __restrict__ gradX, const Real* __restrict__ gradY,
                             const Real* __restrict__ gradZ, Index f, Real ownerFlux,
                             Index scheme) {
  const Index owner = p.faceOwner[f];
  const Index neighbor = p.faceNeighbor[f];

  if (neighbor == DeviceMesh::kNoNeighbor) {
    // Boundary faces always use the ghost-value upwind treatment, whatever the
    // scheme (P12-NUM-001 explicit scope limit).
    const Real phiOwner = phi[owner];
    if (ownerFlux >= 0.0) return phiOwner;  // outflow carries the interior value
    const Real phiB = boundaryValueOf(p.bValueKind[f], p.bValueA[f], p.bValueB[f], phiOwner);
    return (2.0 * phiB) - phiOwner;
  }

  const bool ownerIsUpwind = ownerFlux >= 0.0;
  const Index upwind = ownerIsUpwind ? owner : neighbor;
  const Index downwind = ownerIsUpwind ? neighbor : owner;
  const Real phiUpwind = phi[upwind];
  const Real phiDownwind = phi[downwind];

  if (scheme == kConvectionUpwind) return phiUpwind;

  const bool farValid = (ownerIsUpwind ? p.farOValid[f] : p.farNValid[f]) != 0;
  const Index farCell = ownerIsUpwind ? p.farOCell[f] : p.farNCell[f];
  const Real hCU = ownerIsUpwind ? p.farOHCU[f] : p.farNHCU[f];

  Real phiHighOrder = phiUpwind;
  if (scheme == kConvectionCentral) {
    // interpolateInternalFace, SCALAR overload: ((dNf*phiP)+(dPf*phiN))/(dPf+dNf)
    const Real dP = p.dPf[f];
    const Real dN = p.dNf[f];
    phiHighOrder = ((dN * phi[owner]) + (dP * phi[neighbor])) / (dP + dN);
  } else if (scheme == kConvectionLinearUpwind) {
    const Real ox = ownerIsUpwind ? p.offOX[f] : p.offNX[f];
    const Real oy = ownerIsUpwind ? p.offOY[f] : p.offNY[f];
    const Real oz = ownerIsUpwind ? p.offOZ[f] : p.offNZ[f];
    phiHighOrder = linearUpwindFaceValueDevice(phiUpwind, gradX[upwind], gradY[upwind],
                                               gradZ[upwind], ox, oy, oz);
  } else if (scheme == kConvectionQUICK) {
    if (farValid) {
      // With the owner upwind, hUf is the owner-to-face distance and hfD the
      // face-to-neighbour distance; with the neighbour upwind they swap.
      const Real hUf = ownerIsUpwind ? p.dPf[f] : p.dNf[f];
      const Real hfD = ownerIsUpwind ? p.dNf[f] : p.dPf[f];
      phiHighOrder = quickFaceValueDevice(phi[farCell], hCU, phiUpwind, hUf, phiDownwind, hfD);
    }
    // else: documented deterministic degradation to phiUpwind.
  }

  const Real r = farValid ? smoothnessRatioDevice(phi[farCell], phiUpwind, phiDownwind) : nan("");
  const Real psi = vanLeerLimiterDevice(r, farValid);
  const Real blended = phiUpwind + (psi * (phiHighOrder - phiUpwind));
  const Real lo = phiUpwind < phiDownwind ? phiUpwind : phiDownwind;
  const Real hi = phiUpwind < phiDownwind ? phiDownwind : phiUpwind;
  return clampDevice(blended, lo, hi);
}

__global__ void convectionKernel(Index cellCount, PlanView p, const Real* __restrict__ phi,
                                 const Real* __restrict__ massFlux,
                                 const Real* __restrict__ gradX, const Real* __restrict__ gradY,
                                 const Real* __restrict__ gradZ, Index scheme,
                                 Real* __restrict__ result) {
  const Index c = blockIdx.x * blockDim.x + threadIdx.x;
  if (c >= cellCount) return;
  Real sum = 0.0;
  // cell.faceIds() order, exactly as the CPU loop walks it.
  for (Index slot = p.cellFaceOffsets[c]; slot < p.cellFaceOffsets[c + 1]; ++slot) {
    const Index f = p.cellFaceIds[slot];
    const Real ownerFlux = massFlux[f];
    const Real phiFace = faceValueFor(p, phi, gradX, gradY, gradZ, f, ownerFlux, scheme);
    const Real cellFlux = (p.faceOwner[f] == c) ? ownerFlux : -ownerFlux;
    sum = sum + (cellFlux * phiFace);
  }
  result[c] = sum / p.cellVolumes[c];
}

// EnergyEquation.cpp:266. Gather per row over face-id-sorted incident faces, the
// order SparseMatrixBuilder's stable sort preserves.
__global__ void assembleKernel(Index cellCount, PlanView p, const Real* __restrict__ phi,
                               const Real* __restrict__ massFlux,
                               const Real* __restrict__ cpField, Real cpScalar,
                               Real* __restrict__ values, Real* __restrict__ rhs) {
  const Index c = blockIdx.x * blockDim.x + threadIdx.x;
  if (c >= cellCount) return;

  const Index faceBegin = p.sortedOffsets[c];
  const Index faceEnd = p.sortedOffsets[c + 1];

  Real rhsValue = 0.0;
  for (Index slot = faceBegin; slot < faceEnd; ++slot) {
    const Index f = p.sortedFaces[slot];
    const Index owner = p.faceOwner[f];
    const Index neighbor = p.faceNeighbor[f];
    const Real ownerFlux = massFlux[f];
    if (neighbor != DeviceMesh::kNoNeighbor) continue;  // only boundary faces touch the RHS
    if (ownerFlux >= 0.0) continue;                     // outflow is implicit, no RHS term
    // The per-cell cp overload evaluates cp at the UPWIND cell; on an inflow
    // boundary face the upwind side is the boundary, and the CPU uses the
    // owner's cp there.
    const Real cp = (cpField == nullptr) ? cpScalar : cpField[owner];
    const Real effectiveFlux = cp * ownerFlux;
    const Real phiB = boundaryValueOf(p.bValueKind[f], p.bValueA[f], p.bValueB[f], phi[owner]);
    rhsValue = rhsValue - (effectiveFlux * phiB);
  }
  rhs[c] = rhsValue;

  for (Index k = p.rowOffsets[c]; k < p.rowOffsets[c + 1]; ++k) {
    const Index column = p.columnIndices[k];
    Real value = 0.0;
    for (Index slot = faceBegin; slot < faceEnd; ++slot) {
      const Index f = p.sortedFaces[slot];
      const Index owner = p.faceOwner[f];
      const Index neighbor = p.faceNeighbor[f];
      const Real ownerFlux = massFlux[f];
      const bool ownerIsUpwind = ownerFlux >= 0.0;
      const Index upwind = (neighbor == DeviceMesh::kNoNeighbor)
                               ? owner
                               : (ownerIsUpwind ? owner : neighbor);
      const Real cp = (cpField == nullptr) ? cpScalar : cpField[upwind];
      const Real effectiveFlux = cp * ownerFlux;

      if (neighbor == DeviceMesh::kNoNeighbor) {
        if (ownerIsUpwind && column == c) value = value + effectiveFlux;  // A(o,o) += ef
        continue;
      }
      if (ownerIsUpwind) {
        // A(o,o) += ef ; A(n,o) -= ef
        if (c == owner && column == owner) value = value + effectiveFlux;
        if (c == neighbor && column == owner) value = value - effectiveFlux;
      } else {
        // A(o,n) += ef ; A(n,n) -= ef
        if (c == owner && column == neighbor) value = value + effectiveFlux;
        if (c == neighbor && column == neighbor) value = value - effectiveFlux;
      }
    }
    values[k] = value;
  }
}

__global__ void primitiveKernel(Index count, Index which, const Real* __restrict__ a,
                                const Real* __restrict__ b, const Real* __restrict__ c,
                                const Real* __restrict__ d, const Real* __restrict__ e,
                                const Real* __restrict__ f, Real* __restrict__ out) {
  const Index i = blockIdx.x * blockDim.x + threadIdx.x;
  if (i >= count) return;
  if (which == 0) {
    out[i] = quickFaceValueDevice(a[i], b[i], c[i], d[i], e[i], f[i]);
  } else if (which == 1) {
    // a = phiUpwind, (b,c,d) = grad at the upwind cell, (e,f,0) = x_f - x_U.
    out[i] = linearUpwindFaceValueDevice(a[i], b[i], c[i], d[i], e[i], f[i], 0.0);
  } else if (which == 2) {
    out[i] = smoothnessRatioDevice(a[i], b[i], c[i]);
  } else {
    out[i] = vanLeerLimiterDevice(a[i], !isnan(a[i]));
  }
}

}  // namespace

void fillConvectionPlanView(const DeviceConvectionPlan& plan, ConvectionPlanView& p) {
  const DeviceMesh& mesh = plan.mesh();
  p.faceOwner = mesh.faceOwner();
  p.faceNeighbor = mesh.faceNeighbor();
  p.cellVolumes = mesh.cellVolumes();
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
  p.bValueA = plan.bValueA_.data();
  p.bValueB = plan.bValueB_.data();
  p.bValueKind = plan.bValueKind_.data();
  p.cellFaceOffsets = mesh.cellFaceOffsets();
  p.cellFaceIds = mesh.cellFaceIds();
  p.sortedOffsets = plan.cellFaceOffsets_.data();
  p.sortedFaces = plan.cellFaceSorted_.data();
  p.rowOffsets = plan.rowOffsets_.data();
  p.columnIndices = plan.columnIndices_.data();
}

namespace {

void recordLaunch(const char* what, cfd::Timer& timer) {
  checkCuda(cudaGetLastError(), what);
  auto& stats = gpuExecutionStats();
  ++stats.kernelLaunches;
  stats.kernelSeconds += timer.elapsedSeconds();
}

}  // namespace

void convectionDevice(const DeviceConvectionPlan& plan, const DeviceBuffer<cfd::Real>& phi,
                      const DeviceBuffer<cfd::Real>& massFlux, Index scheme,
                      DeviceBuffer<cfd::Real>& result) {
  if (!plan.usable()) {
    throw cfd::InvalidArgumentError("convectionDevice: plan is not usable (" +
                                    plan.unsupportedReason() + ")");
  }
  const Index nc = plan.cellCount_;
  if (phi.size() != nc) {
    throw cfd::InvalidArgumentError("convectionDevice: field size does not match mesh cell count");
  }
  if (massFlux.size() != plan.mesh().faceCount()) {
    throw cfd::InvalidArgumentError("convectionDevice: massFlux size does not match face count");
  }
  result.resize(nc);
  if (nc == 0) return;

  // Only LinearUpwind builds a gradient -- exactly as the CPU does, so the other
  // three schemes pay nothing.
  const Real* gx = nullptr;
  const Real* gy = nullptr;
  const Real* gz = nullptr;
  if (scheme == kConvectionLinearUpwind) {
    greenGaussGradientDevice(plan.gradient_, phi,
                             cfd::discretization::kGreenGaussSkewCorrectionSweeps, plan.gradX_,
                             plan.gradY_, plan.gradZ_);
    gx = plan.gradX_.data();
    gy = plan.gradY_.data();
    gz = plan.gradZ_.data();
  }

  PlanView p{};
  fillConvectionPlanView(plan, p);
  cfd::Timer timer;
  convectionKernel<<<blockCountFor(nc), kThreadsPerBlock>>>(
      nc, p, phi.data(), massFlux.data(), gx, gy, gz, scheme, result.data());
  recordLaunch("convectionKernel launch", timer);
}

void assembleScalarConvectionDevice(const DeviceConvectionPlan& plan,
                                    const DeviceBuffer<cfd::Real>& phi,
                                    const DeviceBuffer<cfd::Real>& massFlux,
                                    const DeviceBuffer<cfd::Real>* specificHeatField,
                                    cfd::Real specificHeat, DeviceConvectionSystem& system) {
  if (!plan.usable()) {
    throw cfd::InvalidArgumentError("assembleScalarConvectionDevice: plan is not usable (" +
                                    plan.unsupportedReason() + ")");
  }
  const Index nc = plan.cellCount_;
  if (phi.size() != nc) {
    throw cfd::InvalidArgumentError(
        "assembleScalarConvectionDevice: field size does not match mesh cell count");
  }
  system.rowOffsets.resize(nc + 1);
  system.columnIndices.resize(plan.entryCount_);
  system.values.resize(plan.entryCount_);
  system.rhs.resize(nc);
  if (nc + 1 > 0) {
    checkCuda(cudaMemcpy(system.rowOffsets.data(), plan.rowOffsets_.data(),
                         static_cast<std::size_t>(nc + 1) * sizeof(Index),
                         cudaMemcpyDeviceToDevice),
              "cudaMemcpy(convection rowOffsets D2D)");
  }
  if (plan.entryCount_ > 0) {
    checkCuda(cudaMemcpy(system.columnIndices.data(), plan.columnIndices_.data(),
                         static_cast<std::size_t>(plan.entryCount_) * sizeof(Index),
                         cudaMemcpyDeviceToDevice),
              "cudaMemcpy(convection columnIndices D2D)");
  }
  if (nc == 0) return;

  PlanView p{};
  fillConvectionPlanView(plan, p);
  cfd::Timer timer;
  assembleKernel<<<blockCountFor(nc), kThreadsPerBlock>>>(
      nc, p, phi.data(), massFlux.data(),
      specificHeatField == nullptr ? nullptr : specificHeatField->data(), specificHeat,
      system.values.data(), system.rhs.data());
  recordLaunch("convection assembleKernel launch", timer);
}

void evaluateConvectionPrimitives(Index which, const DeviceBuffer<cfd::Real>& a,
                                  const DeviceBuffer<cfd::Real>& b,
                                  const DeviceBuffer<cfd::Real>& c,
                                  const DeviceBuffer<cfd::Real>& d,
                                  const DeviceBuffer<cfd::Real>& e,
                                  const DeviceBuffer<cfd::Real>& f,
                                  DeviceBuffer<cfd::Real>& out) {
  const Index count = a.size();
  out.resize(count);
  if (count == 0) return;
  cfd::Timer timer;
  primitiveKernel<<<blockCountFor(count), kThreadsPerBlock>>>(
      count, which, a.data(), b.data(), c.data(), d.data(), e.data(), f.data(), out.data());
  recordLaunch("primitiveKernel launch", timer);
}

}  // namespace cfd::gpu
