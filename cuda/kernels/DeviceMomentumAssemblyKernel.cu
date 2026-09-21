// GPU-DISC-001F -- the CUDA SIMPLE momentum assembly.
//
// Transcribed from cfd::pressure_velocity::assembleRelaxedMomentumComponent
// (RelaxedMomentum.cpp:30). The gate is BITWISE equality with the CPU system.
//
// The accumulation ORDER is the whole difficulty. SparseMatrixBuilder stable-
// sorts by (row, column) and sums repeated entries in insertion order, so every
// matrix entry accumulates:
//
//     all DIFFUSION contributions (face-id order)
//  -> all CONVECTION contributions (face-id order)
//  -> the single RELAXATION term (diagonal only)
//
// and the RHS accumulates diffusion -> convection -> pressure source
// [-> momentum source] -> relaxation. The kernel walks each row's incident faces
// twice, once per contribution, rather than once doing both -- which would be
// faster and wrong.
//
// Three details that are easy to lose:
//
// 1. alpha == 1.0 is an EARLY RETURN on the CPU, not a multiply by one. No
//    relaxation triplet is appended at all, so no `+0.0` lands on the diagonal.
// 2. factor = (1.0 / alpha) - 1.0. Rewriting it as (1 - alpha)/alpha is
//    algebraically identical and not bitwise identical.
// 3. The pressure source is `-cell.volume() * gradComponent`: the negation is
//    applied to the volume, then multiplied.
//
// Compiled with -fmad=false.

#include <cuda_runtime.h>

#include "cfd/core/Timer.hpp"
#include "cfd/discretization/Gradient.hpp"
#include "cfd/gpu/CudaCheck.hpp"
#include "cfd/gpu/DeviceConvectionTerms.hpp"
#include "cfd/gpu/DeviceMomentumAssembly.hpp"
#include "cfd/gpu/GPUExecutionStats.hpp"

namespace cfd::gpu {

using cfd::Index;
using cfd::Real;

namespace {

constexpr int kThreadsPerBlock = 256;

int blockCountFor(Index count) {
  return static_cast<int>((count + kThreadsPerBlock - 1) / kThreadsPerBlock);
}

}  // namespace

struct MomentumAssemblyView {
  DeviceDiffusionGeometry geometry;
  DeviceConvectionGeometry convection;
  DeviceVectorBoundaryView boundary;
  const Index* faceOwner;
  const Index* faceNeighbor;
  const Real* cellVolumes;
  const Index* sortedOffsets;
  const Index* sortedFaces;
  const Index* rowOffsets;
  const Index* columnIndices;
  // Vector boundary encoding, reused from the convection plan.
  const Index* bcKind;
  const Real* bcConstX;
  const Real* bcConstY;
  const Real* bcConstZ;
  const Real* bcNormalX;
  const Real* bcNormalY;
  const Real* bcNormalZ;
};

namespace {

using View = MomentumAssemblyView;

// boundaryVelocity() + selectComponent(), from the shared header.
__device__ inline Real boundaryComponent(const View& p, Index f, Real ux, Real uy, Real uz,
                                         Index component) {
  return deviceBoundaryVelocityComponent(p.boundary, f, ux, uy, uz, component);
}

// The diffusion contribution to one matrix entry and to the RHS, walked in
// face-id order. Returns the accumulated value for `column`, and adds this
// row's RHS terms into `rhsValue` when `collectRhs` is set.
__device__ void diffusionPass(const View& p, Index c, Index column, bool collectRhs,
                              bool correctInternal, Index component, const Real* __restrict__ mu,
                              const Real* __restrict__ ux, const Real* __restrict__ uy,
                              const Real* __restrict__ uz, const Real* __restrict__ gx,
                              const Real* __restrict__ gy, const Real* __restrict__ gz,
                              Real& value, Real& rhsValue) {
  for (Index slot = p.sortedOffsets[c]; slot < p.sortedOffsets[c + 1]; ++slot) {
    const Index f = p.sortedFaces[slot];
    const Index owner = p.faceOwner[f];
    const Index neighbor = p.faceNeighbor[f];

    if (neighbor == DeviceMesh::kNoNeighbor) {
      const DeviceFaceDiffusionTerms terms =
          deviceBoundaryFaceDiffusionTerms(p.geometry, p.faceOwner, mu, gx, gy, gz, f);
      if (collectRhs) {
        const Real phiB =
            boundaryComponent(p, f, ux[owner], uy[owner], uz[owner], component);
        // Production order: explicitFlux, then boundaryValueCoefficient * phiB.
        rhsValue = rhsValue + terms.explicitFlux;
        rhsValue = rhsValue + (terms.boundaryValueCoefficient * phiB);
      } else {
        if (column == c) value = value + terms.coefficient;
        if (terms.farCellCoefficient != 0.0 && column == terms.farCell) {
          value = value - terms.farCellCoefficient;
        }
      }
      continue;
    }

    const DeviceFaceDiffusionTerms terms = deviceInternalFaceDiffusionTerms(
        p.geometry, p.faceOwner, p.faceNeighbor, mu, gx, gy, gz, f, correctInternal);
    if (collectRhs) {
      if (correctInternal) {
        rhsValue = (c == owner) ? (rhsValue + terms.explicitFlux)
                                : (rhsValue - terms.explicitFlux);
      }
    } else {
      const Index other = (owner == c) ? neighbor : owner;
      if (column == c) value = value + terms.coefficient;
      else if (column == other) value = value - terms.coefficient;
    }
  }
}

// The convection contribution, in face-id order, added into the SAME running
// value the diffusion pass produced -- see this file's header comment for why a
// separate subtotal would not be bitwise equal. The face values come from the
// shared implementation 001D qualified.
__device__ void convectionPass(const View& p, Index c, Index column, bool collectRhs, Index scheme,
                               Index component, const Real* __restrict__ ux,
                               const Real* __restrict__ uy, const Real* __restrict__ uz,
                               const Real* __restrict__ vgx, const Real* __restrict__ vgy,
                               const Real* __restrict__ vgz, const Real* __restrict__ massFlux,
                               Real& value, Real& rhsValue) {
  for (Index slot = p.sortedOffsets[c]; slot < p.sortedOffsets[c + 1]; ++slot) {
    const Index f = p.sortedFaces[slot];
    const Index owner = p.faceOwner[f];
    const Index neighbor = p.faceNeighbor[f];
    const Real ownerFlux = massFlux[f];

    if (neighbor == DeviceMesh::kNoNeighbor) {
      if (collectRhs) {
        if (ownerFlux >= 0.0) continue;  // outflow is implicit
        const Real phiB = boundaryComponent(p, f, ux[owner], uy[owner], uz[owner], component);
        rhsValue = rhsValue - (ownerFlux * phiB);
      } else {
        if (ownerFlux >= 0.0 && column == c) value = value + ownerFlux;
      }
      continue;
    }

    if (!collectRhs) {
      if (ownerFlux >= 0.0) {
        if (c == owner && column == owner) value = value + ownerFlux;
        if (c == neighbor && column == owner) value = value - ownerFlux;
      } else {
        if (c == owner && column == neighbor) value = value + ownerFlux;
        if (c == neighbor && column == neighbor) value = value - ownerFlux;
      }
      continue;
    }
    if (scheme == kConvectionUpwind) continue;
    Real phiUpwind = 0.0;
    const Real phiFace = deviceDeferredFaceValue(p.convection, p.faceOwner, p.faceNeighbor, f,
                                                 ownerFlux, scheme, component, ux, uy, uz, vgx,
                                                 vgy, vgz, phiUpwind);
    const Real correction = ownerFlux * (phiFace - phiUpwind);
    rhsValue = (c == owner) ? (rhsValue - correction) : (rhsValue + correction);
  }
}

__global__ void momentumAssemblyKernel(
    Index cellCount, View p, Index component, Index scheme, bool correctInternal, Real alpha,
    bool relax, const Real* __restrict__ mu, const Real* __restrict__ ux,
    const Real* __restrict__ uy, const Real* __restrict__ uz, const Real* __restrict__ dgx,
    const Real* __restrict__ dgy, const Real* __restrict__ dgz, const Real* __restrict__ massFlux,
    const Real* __restrict__ vgx, const Real* __restrict__ vgy, const Real* __restrict__ vgz,
    const Real* __restrict__ pressureGrad, const Real* __restrict__ momentumSource,
    const Real* __restrict__ previous, Real* __restrict__ values, Real* __restrict__ rhs,
    Real* __restrict__ diagonal) {
  const Index c = blockIdx.x * blockDim.x + threadIdx.x;
  if (c >= cellCount) return;

  // --- RHS -----------------------------------------------------------------
  Real rhsValue = 0.0;
  Real unused = 0.0;
  diffusionPass(p, c, c, /*collectRhs=*/true, correctInternal, component, mu, ux, uy, uz, dgx, dgy,
                dgz, unused, rhsValue);
  convectionPass(p, c, c, /*collectRhs=*/true, scheme, component, ux, uy, uz, vgx, vgy, vgz,
                 massFlux, unused, rhsValue);
  // assemblePressureSourceContribution: rhs += -volume * gradComponent
  rhsValue = rhsValue + (-p.cellVolumes[c] * pressureGrad[c]);
  if (momentumSource != nullptr) {
    rhsValue = rhsValue + momentumSource[c];
  }

  // --- matrix row ----------------------------------------------------------
  Real diagonalValue = 0.0;
  bool sawDiagonal = false;
  for (Index k = p.rowOffsets[c]; k < p.rowOffsets[c + 1]; ++k) {
    const Index column = p.columnIndices[k];
    Real value = 0.0;
    Real ignored = 0.0;
    diffusionPass(p, c, column, /*collectRhs=*/false, correctInternal, component, mu, ux, uy, uz,
                  dgx, dgy, dgz, value, ignored);
    convectionPass(p, c, column, /*collectRhs=*/false, scheme, component, ux, uy, uz, vgx, vgy,
                   vgz, massFlux, value, ignored);
    if (column == c) {
      diagonalValue = value;
      sawDiagonal = true;
    }
    values[k] = value;
  }

  // --- under-relaxation ----------------------------------------------------
  // applyImplicitUnderRelaxation: alpha == 1.0 returns early, so nothing is
  // appended. Otherwise one extra diagonal triplet and one RHS term, both
  // derived from the UNRELAXED diagonal.
  if (relax && sawDiagonal) {
    const Real factor = (1.0 / alpha) - 1.0;
    const Real extra = diagonalValue * factor;
    for (Index k = p.rowOffsets[c]; k < p.rowOffsets[c + 1]; ++k) {
      if (p.columnIndices[k] == c) {
        values[k] = values[k] + extra;
        diagonalValue = values[k];
        break;
      }
    }
    rhsValue = rhsValue + (extra * previous[c]);
  }

  rhs[c] = rhsValue;
  diagonal[c] = diagonalValue;
}

void recordLaunch(const char* what, cfd::Timer& timer) {
  checkCuda(cudaGetLastError(), what);
  auto& stats = gpuExecutionStats();
  ++stats.kernelLaunches;
  stats.kernelSeconds += timer.elapsedSeconds();
}

}  // namespace

void fillMomentumAssemblyView(const DeviceMomentumAssemblyPlan& plan, MomentumAssemblyView& p) {
  const DeviceMesh& mesh = plan.convection_.mesh();
  p.geometry.faceArea = mesh.faceArea();
  p.geometry.dPf = plan.faceDPf_.data();
  p.geometry.dNf = plan.faceDNf_.data();
  p.geometry.dPN = plan.faceDPN_.data();
  p.geometry.decompValid = plan.decompValid_.data();
  p.geometry.orthMag = plan.orthMag_.data();
  p.geometry.nonOrthX = plan.nonOrthX_.data();
  p.geometry.nonOrthY = plan.nonOrthY_.data();
  p.geometry.nonOrthZ = plan.nonOrthZ_.data();
  p.geometry.bDistance = plan.bDistance_.data();
  p.geometry.bPrescribed = plan.bPrescribed_.data();
  p.geometry.bStencilValid = plan.bStencilValid_.data();
  p.geometry.bFarCell = plan.bFarCell_.data();
  p.geometry.bCP = plan.bCP_.data();
  p.geometry.bCF = plan.bCF_.data();
  p.geometry.bCB = plan.bCB_.data();
  p.geometry.bDeltaPX = plan.bDeltaPX_.data();
  p.geometry.bDeltaPY = plan.bDeltaPY_.data();
  p.geometry.bDeltaPZ = plan.bDeltaPZ_.data();
  p.geometry.bDeltaFX = plan.bDeltaFX_.data();
  p.geometry.bDeltaFY = plan.bDeltaFY_.data();
  p.geometry.bDeltaFZ = plan.bDeltaFZ_.data();
  p.geometry.bDecompValid = plan.bDecompValid_.data();
  p.geometry.bOrthMag = plan.bOrthMag_.data();
  p.geometry.bNonOrthX = plan.bNonOrthX_.data();
  p.geometry.bNonOrthY = plan.bNonOrthY_.data();
  p.geometry.bNonOrthZ = plan.bNonOrthZ_.data();

  p.convection = plan.convection_.geometry();
  p.boundary = plan.convection_.boundaryView();

  p.faceOwner = mesh.faceOwner();
  p.faceNeighbor = mesh.faceNeighbor();
  p.cellVolumes = mesh.cellVolumes();
  p.sortedOffsets = plan.convection_.sortedFaceOffsets();
  p.sortedFaces = plan.convection_.sortedFaces();
  p.rowOffsets = plan.convection_.rowOffsets();
  p.columnIndices = plan.convection_.columnIndices();
  p.bcKind = plan.convection_.boundaryKind();
  p.bcConstX = plan.convection_.boundaryConstX();
  p.bcConstY = plan.convection_.boundaryConstY();
  p.bcConstZ = plan.convection_.boundaryConstZ();
  p.bcNormalX = plan.convection_.boundaryNormalX();
  p.bcNormalY = plan.convection_.boundaryNormalY();
  p.bcNormalZ = plan.convection_.boundaryNormalZ();
}

void assembleRelaxedMomentumDevice(const DeviceMomentumAssemblyPlan& plan,
                                   const DeviceVelocity& velocity,
                                   const DeviceBuffer<cfd::Real>& pressure,
                                   const DeviceBuffer<cfd::Real>& effectiveViscosity,
                                   const DeviceBuffer<cfd::Real>& massFlux,
                                   const DeviceBuffer<cfd::Real>& previousComponent,
                                   const DeviceBuffer<cfd::Real>* momentumSource,
                                   const MomentumAssemblyOptions& options,
                                   DeviceMomentumSystem& system) {
  if (!plan.usable()) {
    throw cfd::InvalidArgumentError("assembleRelaxedMomentumDevice: plan is not usable (" +
                                    plan.unsupportedReason() + ")");
  }
  const Index nc = plan.cellCount_;
  const Index entries = plan.entryCount_;
  system.rowOffsets.resize(nc + 1);
  system.columnIndices.resize(entries);
  system.values.resize(entries);
  system.rhs.resize(nc);
  system.diagonal.resize(nc);
  if (nc == 0) return;

  // --- reused qualified operators -----------------------------------------
  // The convection face terms come from the shared implementation 001D
  // qualified (DeviceConvectionTerms.hpp), evaluated inline so its
  // contributions interleave with diffusion's in the CPU's accumulation order.
  //
  // 1. The velocity gradient (001D) -- ALWAYS built, exactly as the CPU does
  //    (P12-DIFF-002 A2), because the Dirichlet wall-flux reconstruction needs
  //    it whether or not the internal-face correction is enabled.
  computeVelocityGradientDevice(plan.convection_, velocity, plan.velocityGradient_);
  const Real* dgx = nullptr;
  const Real* dgy = nullptr;
  const Real* dgz = nullptr;
  if (options.component == kVelocityU) {
    dgx = plan.velocityGradient_.gradUx.data();
    dgy = plan.velocityGradient_.gradUy.data();
    dgz = plan.velocityGradient_.gradUz.data();
  } else if (options.component == kVelocityV) {
    dgx = plan.velocityGradient_.gradVx.data();
    dgy = plan.velocityGradient_.gradVy.data();
    dgz = plan.velocityGradient_.gradVz.data();
  } else {
    dgx = plan.velocityGradient_.gradWx.data();
    dgy = plan.velocityGradient_.gradWy.data();
    dgz = plan.velocityGradient_.gradWz.data();
  }

  // 2. The scalar pressure gradient (001B).
  greenGaussGradientDevice(plan.pressureGradient_, pressure,
                           cfd::discretization::kGreenGaussSkewCorrectionSweeps,
                           plan.pressureGradX_, plan.pressureGradY_, plan.pressureGradZ_);
  const Real* pressureGrad = options.component == kVelocityU  ? plan.pressureGradX_.data()
                             : options.component == kVelocityV ? plan.pressureGradY_.data()
                                                              : plan.pressureGradZ_.data();

  checkCuda(cudaMemcpy(system.rowOffsets.data(), plan.convection_.rowOffsets(),
                       static_cast<std::size_t>(nc + 1) * sizeof(Index), cudaMemcpyDeviceToDevice),
            "cudaMemcpy(momentum rowOffsets D2D)");
  if (entries > 0) {
    checkCuda(cudaMemcpy(system.columnIndices.data(), plan.convection_.columnIndices(),
                         static_cast<std::size_t>(entries) * sizeof(Index),
                         cudaMemcpyDeviceToDevice),
              "cudaMemcpy(momentum columnIndices D2D)");
  }

  MomentumAssemblyView p{};
  fillMomentumAssemblyView(plan, p);

  cfd::Timer timer;
  momentumAssemblyKernel<<<blockCountFor(nc), kThreadsPerBlock>>>(
      nc, p, options.component, options.convectionScheme, options.applyNonOrthogonalCorrection,
      options.relaxationAlpha, options.relaxationAlpha != 1.0, effectiveViscosity.data(),
      velocity.x.data(), velocity.y.data(), velocity.z.data(), dgx, dgy, dgz, massFlux.data(),
      dgx, dgy, dgz, pressureGrad,
      momentumSource == nullptr ? nullptr : momentumSource->data(), previousComponent.data(),
      system.values.data(), system.rhs.data(), system.diagonal.data());
  recordLaunch("momentumAssemblyKernel launch", timer);
}

}  // namespace cfd::gpu
