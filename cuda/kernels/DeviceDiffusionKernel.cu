// GPU-DISC-001C -- the CUDA implicit diffusion assembly.
//
// Transcribed from the CPU reference in the same association order, because the
// gate is BITWISE equality:
//
//   internal gammaFace   Interpolation.cpp:29   ((dNf*gP)+(dPf*gN)) / (dPf+dNf)
//   internal gradFace    Interpolation.cpp:38   ((uP*dNf)+(uN*dPf)) * (1/(dPf+dNf))   <-- NOT the
//                                                                                    scalar form
//   internal terms       NonOrthogonalDiffusion.cpp:51
//   boundary terms       NonOrthogonalDiffusion.cpp:64
//   assembly             KEpsilonEquation.cpp:104
//   accumulation order   SparseMatrix.cpp:143   stable_sort -> face-id order per (row, column)
//
// Three things are load-bearing:
//
// 1. NO FUSED MULTIPLY-ADD. Same reason as the gradient kernel; this file is
//    compiled with -fmad=false (cuda/CMakeLists.txt). Removing the flag makes
//    the differential fail rather than diverge silently.
//
// 2. The two interpolateInternalFace overloads are DIFFERENT expressions. The
//    scalar one divides by (dPf+dNf); the vector one multiplies by its
//    reciprocal. They are not bitwise equal, and diffusion uses both -- the
//    scalar form for gammaFace, the vector form for the face gradient.
//
// 3. Assembly is a GATHER over each row's incident faces in ASCENDING FACE ID.
//    That is the order the production face loop writes triplets, and
//    SparseMatrixBuilder's stable sort preserves it for repeated (row, column)
//    pairs -- which really happens here, because a boundary face's far-cell
//    entry targets a column an internal face also writes. No atomics.

#include <cuda_runtime.h>

#include "cfd/core/Timer.hpp"
#include "cfd/gpu/CudaCheck.hpp"
#include "cfd/discretization/Gradient.hpp"
#include "cfd/gpu/DeviceDiffusion.hpp"
#include "cfd/gpu/DeviceDiffusionTerms.hpp"
#include "cfd/gpu/GPUExecutionStats.hpp"

namespace cfd::gpu {

using cfd::Index;
using cfd::Real;

namespace {

constexpr int kThreadsPerBlock = 256;

int blockCountFor(Index count) {
  return static_cast<int>((count + kThreadsPerBlock - 1) / kThreadsPerBlock);
}

// cfd::dot (Vector3.hpp:78): the z term is dropped when it is exactly zero.
// The guarded dot (cfd::dot's z rule) lives in DeviceDiffusionTerms.hpp now,
// next to the face terms that use it.
__device__ inline Real boundaryValueOf(Index kind, Real a, Real b, Real phiP) {
  if (kind == kBoundaryEncodingConstant) return a;
  if (kind == kBoundaryEncodingShift) return phiP + a;
  return a + (b * phiP);
}

// Pointers the kernels need. Passed as one struct because the parameter list is
// otherwise long enough to be a transcription hazard in itself.
struct PlanView {
  const Index* faceOwner;
  const Index* faceNeighbor;
  const Real* faceArea;
  const Real* dPf;
  const Real* dNf;
  const Real* dPN;
  const Index* decompValid;
  const Real* orthMag;
  const Real* nonOrthX;
  const Real* nonOrthY;
  const Real* nonOrthZ;
  const Real* bDistance;
  const Index* bPrescribed;
  const Real* bValueA;
  const Real* bValueB;
  const Index* bValueKind;
  const Index* bStencilValid;
  const Index* bFarCell;
  const Real* bCP;
  const Real* bCF;
  const Real* bCB;
  const Real* bDeltaPX;
  const Real* bDeltaPY;
  const Real* bDeltaPZ;
  const Real* bDeltaFX;
  const Real* bDeltaFY;
  const Real* bDeltaFZ;
  const Index* bDecompValid;
  const Real* bOrthMag;
  const Real* bNonOrthX;
  const Real* bNonOrthY;
  const Real* bNonOrthZ;
  const Index* cellFaceOffsets;
  const Index* cellFaceSorted;
  const Index* rowOffsets;
  const Index* columnIndices;
};

struct FieldView {
  const Real* phi;
  const Real* diffusivity;
  const Real* gradX;
  const Real* gradY;
  const Real* gradZ;
};


// Adapters onto the shared implementation (DeviceDiffusionTerms.hpp). This
// kernel keeps its own PlanView for the arrays the shared header does not need
// (connectivity, the CSR pattern); the face terms themselves come from the one
// shared implementation the momentum assembly also calls.
__device__ inline DeviceDiffusionGeometry geometryOf(const PlanView& p) {
  DeviceDiffusionGeometry g;
  g.faceArea = p.faceArea; g.dPf = p.dPf; g.dNf = p.dNf; g.dPN = p.dPN;
  g.decompValid = p.decompValid; g.orthMag = p.orthMag;
  g.nonOrthX = p.nonOrthX; g.nonOrthY = p.nonOrthY; g.nonOrthZ = p.nonOrthZ;
  g.bDistance = p.bDistance; g.bPrescribed = p.bPrescribed;
  g.bStencilValid = p.bStencilValid; g.bFarCell = p.bFarCell;
  g.bCP = p.bCP; g.bCF = p.bCF; g.bCB = p.bCB;
  g.bDeltaPX = p.bDeltaPX; g.bDeltaPY = p.bDeltaPY; g.bDeltaPZ = p.bDeltaPZ;
  g.bDeltaFX = p.bDeltaFX; g.bDeltaFY = p.bDeltaFY; g.bDeltaFZ = p.bDeltaFZ;
  g.bDecompValid = p.bDecompValid; g.bOrthMag = p.bOrthMag;
  g.bNonOrthX = p.bNonOrthX; g.bNonOrthY = p.bNonOrthY; g.bNonOrthZ = p.bNonOrthZ;
  return g;
}

__device__ inline DeviceFaceDiffusionTerms internalTermsOf(const PlanView& p, const FieldView& v,
                                                           Index f, bool correct) {
  const DeviceDiffusionGeometry g = geometryOf(p);
  return deviceInternalFaceDiffusionTerms(g, p.faceOwner, p.faceNeighbor, v.diffusivity, v.gradX,
                                          v.gradY, v.gradZ, f, correct);
}

__device__ inline DeviceFaceDiffusionTerms boundaryTermsOf(const PlanView& p, const FieldView& v,
                                                           Index f) {
  const DeviceDiffusionGeometry g = geometryOf(p);
  return deviceBoundaryFaceDiffusionTerms(g, p.faceOwner, v.diffusivity, v.gradX, v.gradY, v.gradZ,
                                          f);
}

__global__ void assembleKernel(Index cellCount, PlanView p, FieldView v, bool correctInternal,
                               Real* __restrict__ values, Real* __restrict__ rhs) {
  const Index c = blockIdx.x * blockDim.x + threadIdx.x;
  if (c >= cellCount) return;

  const Index faceBegin = p.cellFaceOffsets[c];
  const Index faceEnd = p.cellFaceOffsets[c + 1];

  // --- RHS, in face-id order, matching the production loop -----------------
  Real rhsValue = 0.0;
  for (Index slot = faceBegin; slot < faceEnd; ++slot) {
    const Index f = p.cellFaceSorted[slot];
    if (p.faceNeighbor[f] == DeviceMesh::kNoNeighbor) {
      const DeviceFaceDiffusionTerms terms = boundaryTermsOf(p, v, f);
      const Real phiB = boundaryValueOf(p.bValueKind[f], p.bValueA[f], p.bValueB[f],
                                        v.phi[p.faceOwner[f]]);
      // Production order: explicitFlux first, then boundaryValueCoefficient*phiB
      // (the matrix write sits between them and does not touch the RHS).
      rhsValue = rhsValue + terms.explicitFlux;
      rhsValue = rhsValue + (terms.boundaryValueCoefficient * phiB);
    } else if (correctInternal) {
      const DeviceFaceDiffusionTerms terms = internalTermsOf(p, v, f, correctInternal);
      rhsValue = (p.faceOwner[f] == c) ? (rhsValue + terms.explicitFlux)
                                       : (rhsValue - terms.explicitFlux);
    }
  }
  rhs[c] = rhsValue;

  // --- matrix row, columns ascending, each accumulated in face-id order ----
  const Index rowBegin = p.rowOffsets[c];
  const Index rowEnd = p.rowOffsets[c + 1];
  for (Index k = rowBegin; k < rowEnd; ++k) {
    const Index column = p.columnIndices[k];
    Real value = 0.0;
    for (Index slot = faceBegin; slot < faceEnd; ++slot) {
      const Index f = p.cellFaceSorted[slot];
      if (p.faceNeighbor[f] == DeviceMesh::kNoNeighbor) {
        const DeviceFaceDiffusionTerms terms = boundaryTermsOf(p, v, f);
        if (column == c) value = value + terms.coefficient;
        if (terms.farCellCoefficient != 0.0 && column == terms.farCell) {
          value = value - terms.farCellCoefficient;
        }
      } else {
        const DeviceFaceDiffusionTerms terms = internalTermsOf(p, v, f, correctInternal);
        const Index other = (p.faceOwner[f] == c) ? p.faceNeighbor[f] : p.faceOwner[f];
        if (column == c) {
          value = value + terms.coefficient;
        } else if (column == other) {
          value = value - terms.coefficient;
        }
      }
    }
    values[k] = value;
  }
}

}  // namespace

void assembleScalarDiffusionDevice(const DeviceDiffusionPlan& plan,
                                   const DeviceBuffer<cfd::Real>& phi,
                                   const DeviceBuffer<cfd::Real>& diffusivity,
                                   bool nonOrthogonalEnabled, DeviceDiffusionSystem& system) {
  if (!plan.usable()) {
    throw cfd::InvalidArgumentError("assembleScalarDiffusionDevice: plan is not usable (" +
                                    plan.unsupportedReason() + ")");
  }
  const DeviceMesh& mesh = plan.mesh();
  const Index nc = plan.cellCount_;
  if (phi.size() != nc || diffusivity.size() != nc) {
    throw cfd::InvalidArgumentError(
        "assembleScalarDiffusionDevice: field size does not match mesh cell count");
  }

  // The production assembler ALWAYS builds the gradient (P12-DIFF-002 A2): the
  // Dirichlet wall-flux scheme must not depend on the non-orthogonal flag. This
  // is the 001B operator, on the device-resident field, result left on device.
  greenGaussGradientDevice(plan.gradient_, phi, cfd::discretization::kGreenGaussSkewCorrectionSweeps,
                           plan.gradX_, plan.gradY_, plan.gradZ_);

  system.rowOffsets.resize(nc + 1);
  system.columnIndices.resize(plan.entryCount_);
  system.values.resize(plan.entryCount_);
  system.rhs.resize(nc);
  // The pattern is immutable; copy it device-to-device rather than re-uploading.
  if (nc + 1 > 0) {
    checkCuda(cudaMemcpy(system.rowOffsets.data(), plan.rowOffsets_.data(),
                         static_cast<std::size_t>(nc + 1) * sizeof(Index),
                         cudaMemcpyDeviceToDevice),
              "cudaMemcpy(rowOffsets D2D)");
  }
  if (plan.entryCount_ > 0) {
    checkCuda(cudaMemcpy(system.columnIndices.data(), plan.columnIndices_.data(),
                         static_cast<std::size_t>(plan.entryCount_) * sizeof(Index),
                         cudaMemcpyDeviceToDevice),
              "cudaMemcpy(columnIndices D2D)");
  }
  if (nc == 0) return;

  PlanView p{};
  p.faceOwner = mesh.faceOwner();
  p.faceNeighbor = mesh.faceNeighbor();
  p.faceArea = mesh.faceArea();
  p.dPf = plan.faceDPf_.data();
  p.dNf = plan.faceDNf_.data();
  p.dPN = plan.faceDPN_.data();
  p.decompValid = plan.faceDecompValid_.data();
  p.orthMag = plan.faceOrthMag_.data();
  p.nonOrthX = plan.faceNonOrthX_.data();
  p.nonOrthY = plan.faceNonOrthY_.data();
  p.nonOrthZ = plan.faceNonOrthZ_.data();
  p.bDistance = plan.bDistance_.data();
  p.bPrescribed = plan.bPrescribed_.data();
  p.bValueA = plan.bValueA_.data();
  p.bValueB = plan.bValueB_.data();
  p.bValueKind = plan.bValueKind_.data();
  p.bStencilValid = plan.bStencilValid_.data();
  p.bFarCell = plan.bFarCell_.data();
  p.bCP = plan.bCP_.data();
  p.bCF = plan.bCF_.data();
  p.bCB = plan.bCB_.data();
  p.bDeltaPX = plan.bDeltaPX_.data();
  p.bDeltaPY = plan.bDeltaPY_.data();
  p.bDeltaPZ = plan.bDeltaPZ_.data();
  p.bDeltaFX = plan.bDeltaFX_.data();
  p.bDeltaFY = plan.bDeltaFY_.data();
  p.bDeltaFZ = plan.bDeltaFZ_.data();
  p.bDecompValid = plan.bDecompValid_.data();
  p.bOrthMag = plan.bOrthMag_.data();
  p.bNonOrthX = plan.bNonOrthX_.data();
  p.bNonOrthY = plan.bNonOrthY_.data();
  p.bNonOrthZ = plan.bNonOrthZ_.data();
  p.cellFaceOffsets = plan.cellFaceOffsets_.data();
  p.cellFaceSorted = plan.cellFaceSorted_.data();
  p.rowOffsets = plan.rowOffsets_.data();
  p.columnIndices = plan.columnIndices_.data();

  FieldView v{};
  v.phi = phi.data();
  v.diffusivity = diffusivity.data();
  v.gradX = plan.gradX_.data();
  v.gradY = plan.gradY_.data();
  v.gradZ = plan.gradZ_.data();

  cfd::Timer timer;
  assembleKernel<<<blockCountFor(nc), kThreadsPerBlock>>>(
      nc, p, v, nonOrthogonalEnabled, system.values.data(), system.rhs.data());
  checkCuda(cudaGetLastError(), "assembleKernel launch");
  auto& stats = gpuExecutionStats();
  ++stats.kernelLaunches;
  stats.kernelSeconds += timer.elapsedSeconds();
  // No synchronize: the only consumers are later kernels on the default stream
  // (GPU-PIPE-001 Phase 3), and the result is left device-resident.
}

}  // namespace cfd::gpu
