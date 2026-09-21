// GPU-DISC-001I -- the CUDA pressure-correction assembly.
//
// Transcribed from assembleGeometricPressureCorrection (PressureCorrectionEquation.cpp:181),
// pressureCorrectionFaceCoupling/coupling (:146, :74), evaluateContinuity
// (ContinuityEquation.cpp:14) and MeshGeometry::decomposeAreaVector (MeshGeometry.cpp:144).
//
// Five things are load-bearing:
//
// 1. NO FUSED MULTIPLY-ADD -- compiled with -fmad=false.
//
// 2. The reference pin is SUBTRACTIVE, not additive. When pinReferenceCell is
//    set, the CPU's face loop never writes the reference row at all (each of
//    the four adds is independently guarded), and only then is 1.0 added. So
//    A(ref,ref) is EXACTLY 1.0, and every off-diagonal of that row is exactly
//    0.0 -- which the CPU builder then drops. Reproducing only the final
//    `+= 1.0` would leave the accumulated coefficients underneath it.
//
// 3. Those guards are INDEPENDENT per row. Pinning suppresses one endpoint's
//    pair, never the face: the opposite row still receives its contribution.
//    The per-row gather gives this for free, which is why it is structured
//    that way.
//
// 4. decomposeAreaVector is evaluated ON DEVICE, not precomputed: its second
//    argument is the RESPONSE VECTOR (du*Sx, dv*Sy, dw*Sz), which depends on
//    the response-coefficient fields. Its exact `cross(d,sf) == 0`
//    short-circuit and its `dot(d,sf) > 1e-6*|d|*|sf|` guard are reproduced.
//
// 5. The continuity imbalance gathers in cell.faceIds() order, while the matrix
//    gathers in face-id order -- the two orders the CPU actually uses.

#include <cuda_runtime.h>

#include "cfd/core/Timer.hpp"
#include "cfd/discretization/Gradient.hpp"
#include "cfd/gpu/CudaCheck.hpp"
#include "cfd/gpu/DevicePressureCorrection.hpp"
#include "cfd/gpu/GPUExecutionStats.hpp"

namespace cfd::gpu {

using cfd::Index;
using cfd::Real;

namespace {

constexpr int kThreadsPerBlock = 256;

int blockCountFor(Index count) {
  return static_cast<int>((count + kThreadsPerBlock - 1) / kThreadsPerBlock);
}

__device__ inline Real pcDot(Real ax, Real ay, Real az, Real bx, Real by, Real bz) {
  const Real inPlane = (ax * bx) + (ay * by);
  const Real normal = az * bz;
  return normal == 0.0 ? inPlane : inPlane + normal;
}

__device__ inline Real pcMagnitude(Real x, Real y, Real z) {
  return sqrt(pcDot(x, y, z, x, y, z));
}

}  // namespace

struct PressureCorrectionView {
  const Index* faceOwner;
  const Index* faceNeighbor;
  const Real* areaX;
  const Real* areaY;
  const Real* areaZ;
  const Real* faceArea;
  const Real* dX;
  const Real* dY;
  const Real* dZ;
  const Real* distance;
  const Real* dPf;
  const Real* dNf;
  const Index* axisAligned;
  const Index* axisIndex;
  const Index* areaZIsZero;
  const Index* isFixedValue;
  const Index* cellFaceOffsets;
  const Index* cellFaceIds;
  const Index* sortedOffsets;
  const Index* sortedFaces;
  const Index* rowOffsets;
  const Index* columnIndices;
};

namespace {

using View = PressureCorrectionView;

// MeshGeometry::decomposeAreaVector, evaluated per face per assembly because
// `sf` here is the field-dependent response vector.
__device__ inline bool decomposeAreaVectorDevice(Real dx, Real dy, Real dz, Real sx, Real sy,
                                                 Real sz, Real& ox, Real& oy, Real& oz, Real& nx,
                                                 Real& ny, Real& nz) {
  const Real dDotSf = pcDot(dx, dy, dz, sx, sy, sz);
  const Real dMag = pcMagnitude(dx, dy, dz);
  const Real sfMag = pcMagnitude(sx, sy, sz);
  if (!(dMag > 0.0) || !(sfMag > 0.0)) return false;
  if (!(dDotSf > (1e-6 * dMag * sfMag))) return false;
  // cross(d, sf) == Vector3{} -- the EXACT short-circuit that makes the
  // correction bit-identical to the uncorrected operator on an orthogonal face.
  const Real cx = (dy * sz) - (dz * sy);
  const Real cy = (dz * sx) - (dx * sz);
  const Real cz = (dx * sy) - (dy * sx);
  if (cx == 0.0 && cy == 0.0 && cz == 0.0) {
    ox = sx; oy = sy; oz = sz;
    nx = 0.0; ny = 0.0; nz = 0.0;
    return true;
  }
  const Real sfDotSf = pcDot(sx, sy, sz, sx, sy, sz);
  const Real scale = sfDotSf / dDotSf;
  ox = dx * scale; oy = dy * scale; oz = dz * scale;
  nx = sx - ox; ny = sy - oy; nz = sz - oz;
  return true;
}

// coupling(face, d, rho, du, dv, dw, nonOrthogonal) -- PressureCorrectionEquation.cpp:74
__device__ inline void couplingOf(const View& p, Index f, Real du, Real dv, Real dw, Real rho,
                                  bool nonOrthogonal, Real& coefficient, Real& nonOrthX,
                                  Real& nonOrthY, Real& nonOrthZ) {
  const Real distance = p.distance[f];
  nonOrthX = 0.0; nonOrthY = 0.0; nonOrthZ = 0.0;
  if (p.axisAligned[f] != 0) {
    const Index axis = p.axisIndex[f];
    const Real dComponent = axis == 0 ? du : (axis == 1 ? dv : dw);
    coefficient = rho * p.faceArea[f] * dComponent / distance;
    return;
  }
  const Real rx = du * p.areaX[f];
  const Real ry = dv * p.areaY[f];
  const Real rz = p.areaZIsZero[f] != 0 ? 0.0 : dw * p.areaZ[f];
  if (nonOrthogonal) {
    Real ox, oy, oz, nx, ny, nz;
    if (decomposeAreaVectorDevice(p.dX[f], p.dY[f], p.dZ[f], rx, ry, rz, ox, oy, oz, nx, ny, nz)) {
      coefficient = rho * pcMagnitude(ox, oy, oz) / distance;
      nonOrthX = nx * rho; nonOrthY = ny * rho; nonOrthZ = nz * rho;
      return;
    }
  }
  coefficient = rho * pcMagnitude(rx, ry, rz) / distance;
}

// pressureCorrectionFaceCoupling -- picks WHICH responses get interpolated.
__device__ void faceCouplingOf(const View& p, Index f, const Real* __restrict__ dU,
                               const Real* __restrict__ dV, const Real* __restrict__ dW, Real rho,
                               bool nonOrthogonal, Real& coefficient, Real& nx, Real& ny,
                               Real& nz) {
  const Index owner = p.faceOwner[f];
  const Index neighbor = p.faceNeighbor[f];
  if (neighbor == DeviceMesh::kNoNeighbor) {
    // Boundary: the OWNER's own responses, no interpolation.
    const Real dw = dW == nullptr ? 0.0 : dW[owner];
    couplingOf(p, f, dU[owner], dV[owner], dw, rho, nonOrthogonal, coefficient, nx, ny, nz);
    return;
  }
  const Real dP = p.dPf[f];
  const Real dN = p.dNf[f];
  if (p.axisAligned[f] != 0) {
    const Index axis = p.axisIndex[f];
    const Real* response = axis == 0 ? dU : (axis == 1 ? dV : dW);
    // SCALAR interpolateInternalFace: divide.
    const Real dFace = ((dN * response[owner]) + (dP * response[neighbor])) / (dP + dN);
    couplingOf(p, f, dFace, dFace, dFace, rho, nonOrthogonal, coefficient, nx, ny, nz);
    return;
  }
  const Real du = ((dN * dU[owner]) + (dP * dU[neighbor])) / (dP + dN);
  const Real dv = ((dN * dV[owner]) + (dP * dV[neighbor])) / (dP + dN);
  const Real dw = dW == nullptr ? 0.0 : ((dN * dW[owner]) + (dP * dW[neighbor])) / (dP + dN);
  couplingOf(p, f, du, dv, dw, rho, nonOrthogonal, coefficient, nx, ny, nz);
}

// Per face: the coefficient the matrix uses, and the explicit flux when a
// previous p' gradient is available.
__global__ void faceTermsKernel(Index faceCount, View p, const Real* __restrict__ dU,
                                const Real* __restrict__ dV, const Real* __restrict__ dW, Real rho,
                                bool nonOrthogonal, const Real* __restrict__ gpx,
                                const Real* __restrict__ gpy, const Real* __restrict__ gpz,
                                Real* __restrict__ faceCoefficient,
                                Real* __restrict__ explicitFaceFlux) {
  const Index f = blockIdx.x * blockDim.x + threadIdx.x;
  if (f >= faceCount) return;
  const Index owner = p.faceOwner[f];
  const Index neighbor = p.faceNeighbor[f];

  // A non-FixedValue boundary face contributes nothing at all: the CPU
  // `continue`s before computing anything, and both SurfaceFields keep their
  // value-initialised 0.0.
  if (neighbor == DeviceMesh::kNoNeighbor && p.isFixedValue[f] == 0) {
    faceCoefficient[f] = 0.0;
    explicitFaceFlux[f] = 0.0;
    return;
  }

  Real coefficient, nx, ny, nz;
  faceCouplingOf(p, f, dU, dV, dW, rho, nonOrthogonal, coefficient, nx, ny, nz);
  faceCoefficient[f] = coefficient;

  if (gpx == nullptr) {
    explicitFaceFlux[f] = 0.0;
    return;
  }
  if (neighbor == DeviceMesh::kNoNeighbor) {
    explicitFaceFlux[f] = -pcDot(nx, ny, nz, gpx[owner], gpy[owner], gpz[owner]);
    return;
  }
  // interpolateInternalFace, VECTOR overload: multiply by the reciprocal.
  const Real dP = p.dPf[f];
  const Real dN = p.dNf[f];
  const Real inverse = 1.0 / (dP + dN);
  const Real gx = ((gpx[owner] * dN) + (gpx[neighbor] * dP)) * inverse;
  const Real gy = ((gpy[owner] * dN) + (gpy[neighbor] * dP)) * inverse;
  const Real gz = ((gpz[owner] * dN) + (gpz[neighbor] * dP)) * inverse;
  explicitFaceFlux[f] = -pcDot(nx, ny, nz, gx, gy, gz);
}

__global__ void assembleKernel(Index cellCount, View p, const Real* __restrict__ predictorMassFlux,
                               const Real* __restrict__ faceCoefficient,
                               const Real* __restrict__ explicitFaceFlux, bool hasExplicit,
                               bool pinReferenceCell, Index referenceCell,
                               Real* __restrict__ values, Real* __restrict__ rhs) {
  const Index c = blockIdx.x * blockDim.x + threadIdx.x;
  if (c >= cellCount) return;
  const bool isReference = pinReferenceCell && c == referenceCell;

  // --- RHS ---------------------------------------------------------------
  // evaluateContinuity gathers in cell.faceIds() order.
  Real imbalance = 0.0;
  for (Index slot = p.cellFaceOffsets[c]; slot < p.cellFaceOffsets[c + 1]; ++slot) {
    const Index f = p.cellFaceIds[slot];
    const Real flux = predictorMassFlux[f];
    imbalance = imbalance + ((p.faceOwner[f] == c) ? flux : -flux);
  }
  Real rhsValue = -imbalance;
  if (hasExplicit) {
    // The CPU loops faces in FACE-ID order, applying -e to the owner row and
    // +e to the neighbour row.
    for (Index slot = p.sortedOffsets[c]; slot < p.sortedOffsets[c + 1]; ++slot) {
      const Index f = p.sortedFaces[slot];
      if (p.faceOwner[f] == c) rhsValue = rhsValue - explicitFaceFlux[f];
      else rhsValue = rhsValue + explicitFaceFlux[f];
    }
  }
  // The pin ASSIGNS zero, after everything else.
  rhs[c] = isReference ? 0.0 : rhsValue;

  // --- matrix row --------------------------------------------------------
  for (Index k = p.rowOffsets[c]; k < p.rowOffsets[c + 1]; ++k) {
    const Index column = p.columnIndices[k];
    if (isReference) {
      // The face loop never writes this row, so the diagonal is EXACTLY the
      // 1.0 added afterwards and every off-diagonal is exactly 0.0.
      values[k] = (column == c) ? 1.0 : 0.0;
      continue;
    }
    Real value = 0.0;
    for (Index slot = p.sortedOffsets[c]; slot < p.sortedOffsets[c + 1]; ++slot) {
      const Index f = p.sortedFaces[slot];
      const Index owner = p.faceOwner[f];
      const Index neighbor = p.faceNeighbor[f];
      if (neighbor == DeviceMesh::kNoNeighbor) {
        if (p.isFixedValue[f] == 0) continue;  // Neumann-like: no entry at all
        if (column == c) value = value + faceCoefficient[f];
        continue;
      }
      const Real d = faceCoefficient[f];
      const Index other = (owner == c) ? neighbor : owner;
      if (column == c) value = value + d;
      else if (column == other) value = value - d;
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

void fillPressureCorrectionView(const DevicePressureCorrectionPlan& plan,
                                PressureCorrectionView& p) {
  const DeviceMesh& mesh = plan.mesh_;
  p.faceOwner = mesh.faceOwner();
  p.faceNeighbor = mesh.faceNeighbor();
  p.areaX = mesh.faceAreaX();
  p.areaY = mesh.faceAreaY();
  p.areaZ = mesh.faceAreaZ();
  p.faceArea = mesh.faceArea();
  p.dX = plan.dX_.data();
  p.dY = plan.dY_.data();
  p.dZ = plan.dZ_.data();
  p.distance = plan.distance_.data();
  p.dPf = plan.faceDPf_.data();
  p.dNf = plan.faceDNf_.data();
  p.axisAligned = plan.axisAligned_.data();
  p.axisIndex = plan.axisIndex_.data();
  p.areaZIsZero = plan.areaZIsZero_.data();
  p.isFixedValue = plan.isFixedValue_.data();
  p.cellFaceOffsets = mesh.cellFaceOffsets();
  p.cellFaceIds = mesh.cellFaceIds();
  p.sortedOffsets = plan.cellFaceOffsets_.data();
  p.sortedFaces = plan.cellFaceSorted_.data();
  p.rowOffsets = plan.rowOffsets_.data();
  p.columnIndices = plan.columnIndices_.data();
}

void assemblePressureCorrectionDevice(const DevicePressureCorrectionPlan& plan,
                                      const DeviceBuffer<cfd::Real>& predictorMassFlux,
                                      const DeviceBuffer<cfd::Real>& dU,
                                      const DeviceBuffer<cfd::Real>& dV,
                                      const DeviceBuffer<cfd::Real>* dW,
                                      const DeviceBuffer<cfd::Real>* previousPressureCorrection,
                                      const PressureCorrectionOptionsDevice& options,
                                      DevicePressureCorrectionSystem& system) {
  if (!plan.usable()) {
    throw cfd::InvalidArgumentError("assemblePressureCorrectionDevice: plan is not usable (" +
                                    plan.unsupportedReason() + ")");
  }
  if (plan.threeDimensional_ && dW == nullptr) {
    throw cfd::InvalidArgumentError(
        "assemblePressureCorrectionDevice: a 3D mesh needs the w response coefficient");
  }
  if (!(options.density > 0.0)) {
    throw cfd::InvalidArgumentError(
        "assemblePressureCorrectionDevice: density must be finite and > 0");
  }
  if (options.referenceCell >= plan.cellCount_) {
    throw cfd::InvalidArgumentError("assemblePressureCorrectionDevice: referenceCell out of range");
  }
  // GPU-PIPE-001: the CSR structure depends on which cell is pinned, because
  // the pinned row carries only its diagonal (see the plan's own comment).
  // Keyed, so production builds it once for the whole solve.
  plan.ensureStructure(options.referenceCell);

  const Index nc = plan.cellCount_;
  const Index nf = plan.faceCount_;
  system.rowOffsets.resize(nc + 1);
  system.columnIndices.resize(plan.entryCount_);
  system.values.resize(plan.entryCount_);
  system.rhs.resize(nc);
  system.faceCoefficient.resize(nf);
  system.explicitFaceFlux.resize(nf);
  checkCuda(cudaMemcpy(system.rowOffsets.data(), plan.rowOffsets_.data(),
                       static_cast<std::size_t>(nc + 1) * sizeof(Index), cudaMemcpyDeviceToDevice),
            "cudaMemcpy(pcorr rowOffsets D2D)");
  if (plan.entryCount_ > 0) {
    checkCuda(cudaMemcpy(system.columnIndices.data(), plan.columnIndices_.data(),
                         static_cast<std::size_t>(plan.entryCount_) * sizeof(Index),
                         cudaMemcpyDeviceToDevice),
              "cudaMemcpy(pcorr columnIndices D2D)");
  }
  if (nc == 0) return;

  // The explicit term exists only when the correction is enabled AND a previous
  // p' was supplied -- exactly the CPU's condition.
  const bool hasExplicit = options.nonOrthogonal && previousPressureCorrection != nullptr;
  const Real* gpx = nullptr;
  const Real* gpy = nullptr;
  const Real* gpz = nullptr;
  if (hasExplicit) {
    greenGaussGradientDevice(plan.previousGradient_, *previousPressureCorrection,
                             cfd::discretization::kGreenGaussSkewCorrectionSweeps, plan.gradPrevX_,
                             plan.gradPrevY_, plan.gradPrevZ_);
    gpx = plan.gradPrevX_.data();
    gpy = plan.gradPrevY_.data();
    gpz = plan.gradPrevZ_.data();
  }

  PressureCorrectionView p{};
  fillPressureCorrectionView(plan, p);

  {
    cfd::Timer timer;
    faceTermsKernel<<<blockCountFor(nf), kThreadsPerBlock>>>(
        nf, p, dU.data(), dV.data(), dW == nullptr ? nullptr : dW->data(), options.density,
        options.nonOrthogonal, gpx, gpy, gpz, system.faceCoefficient.data(),
        system.explicitFaceFlux.data());
    recordLaunch("pcorr faceTermsKernel launch", timer);
  }
  {
    cfd::Timer timer;
    assembleKernel<<<blockCountFor(nc), kThreadsPerBlock>>>(
        nc, p, predictorMassFlux.data(), system.faceCoefficient.data(),
        system.explicitFaceFlux.data(), hasExplicit, plan.pinReferenceCell_,
        options.referenceCell, system.values.data(), system.rhs.data());
    recordLaunch("pcorr assembleKernel launch", timer);
  }
}

}  // namespace cfd::gpu
