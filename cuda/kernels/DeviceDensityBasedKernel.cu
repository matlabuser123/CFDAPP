// P17.10 -- the density-based compressible solver on device.
//
// WHERE THE PHYSICS IS. The Riemann solver, the reconstruction, the Euler flux, the area
// integration and the state validation are NOT in this file: they live in
// include/cfd/compressible/HllcFlux.hpp marked CFD_HLLC_HOST_DEVICE, and gcc and nvcc compile that
// same text. The thermodynamic closed forms and the three small solver helpers are in
// include/cfd/gpu/DeviceThermodynamics.hpp, also shared host/device, and are the ONLY restatements
// in the phase -- each one proved against production by a direct cross-comparison test, as
// authorization §4 requires.
//
// So this file is orchestration: memory, indexing, launch, and the gather.
//
// Compiled with --fmad=false (cuda/CMakeLists.txt), the project's established convention since
// before P17.7, so nvcc does not fuse a*b+c where the host x86-64 build does not.
//
// THE GATHER, which is the whole reason BITWISE is achievable here. The CPU assembles the residual
// by looping faces in ascending id order and doing r[owner] += F, r[neighbour] -= F, so each cell's
// residual is a sum over ITS faces in ascending face-id order. Face ids come from a monotonically
// increasing counter and Cell::addFace appends without sorting, so every cell's face list ascends.
// A per-cell gather over cellFaceIds therefore adds the same terms in the same order -- bitwise,
// not merely close -- and needs no atomics, which is separately what makes the result reproducible
// run to run. Measured before this file existed: acceptance_gate.md §0 D1, 702 cell-face
// references across five mesh construction paths, all ascending.

#include <cuda_runtime.h>

#include <cstddef>
#include <string>
#include <vector>

#include "cfd/compressible/HllcFlux.hpp"
#include "cfd/core/Exception.hpp"
#include "cfd/gpu/DeviceBuffer.hpp"
#include "cfd/gpu/DeviceDensityBasedSolver.hpp"
#include "cfd/gpu/DeviceMesh.hpp"
#include "cfd/gpu/DeviceThermodynamics.hpp"

namespace cfd::gpu {

namespace {

using cfd::Index;
using cfd::Real;
using cfd::Vector3;
using cfd::compressible::FluxVector;
using cfd::compressible::HllcResult;
using cfd::compressible::PrimitiveState;
using cfd::compressible::WaveSpeedEstimator;

constexpr Index kNoNeighborSentinel = static_cast<Index>(-1);

// A cell's five conserved components, structure-of-arrays on device.
struct StateView {
  Real* density;
  Real* momentumX;
  Real* momentumY;
  Real* momentumZ;
  Real* totalEnergy;
};

struct ConstStateView {
  const Real* density;
  const Real* momentumX;
  const Real* momentumY;
  const Real* momentumZ;
  const Real* totalEnergy;
};

// The immutable mesh slice every kernel needs, passed by value so no kernel dereferences a host
// object.
struct MeshView {
  const Real* cellVolume;
  const Index* faceOwner;
  const Index* faceNeighbor;
  const Real* faceAreaX;
  const Real* faceAreaY;
  const Real* faceAreaZ;
  const Real* faceArea;
  const Index* cellFaceOffsets;
  const Index* cellFaceIds;
  // Per FACE: the boundary treatment, or -1 for an interior face. Built host-side and validated
  // there, so a kernel never has to decide what an unmapped patch means.
  const int* faceBoundaryKind;
  Index cellCount;
  Index faceCount;
};

// The prescribed inflow state for each boundary face. Stored per face rather than per patch so the
// kernel needs no patch lookup; the duplication is a few states, and the alternative is an
// indirection that has to be right.
struct BoundaryView {
  const PrimitiveState* prescribed;  // indexed by face id; unused for wall/outflow
};

// -------------------------------------------------------------------------------------------
// K1 -- conserved -> primitive, one thread per cell
// -------------------------------------------------------------------------------------------
__global__ void recoverKernel(ConstStateView q, DeviceThermodynamics thermo, DeviceControls ctrl,
                              PrimitiveState* primitives, int* statuses, Index cellCount) {
  const Index i = static_cast<Index>(blockIdx.x) * blockDim.x + threadIdx.x;
  if (i >= cellCount) return;
  PrimitiveState s{};
  const DeviceRecoveryStatus st = deviceRecoverPrimitive(
      thermo, q.density[i], Vector3{q.momentumX[i], q.momentumY[i], q.momentumZ[i]},
      q.totalEnergy[i], &s, ctrl);
  statuses[i] = static_cast<int>(st);
  // On a refusal the state is left default-constructed rather than partially filled: a half-built
  // state that then flows into a flux is exactly how an inadmissible cell produces a plausible
  // number instead of a refusal.
  primitives[i] = (st == DeviceRecoveryStatus::Ok) ? s : PrimitiveState{};
}

// -------------------------------------------------------------------------------------------
// K2a -- the face flux, one thread per FACE. One evaluation per face, five components at once.
// -------------------------------------------------------------------------------------------
__global__ void faceFluxKernel(const PrimitiveState* primitives, MeshView mesh, BoundaryView bc,
                               DeviceThermodynamics thermo, DeviceControls ctrl, Real gamma,
                               FluxVector* faceFlux, FluxVector* faceFluxFlipped,
                               int* faceValid) {
  const Index f = static_cast<Index>(blockIdx.x) * blockDim.x + threadIdx.x;
  if (f >= mesh.faceCount) return;

  const Index owner = mesh.faceOwner[f];
  const Index neighbour = mesh.faceNeighbor[f];
  const bool isBoundary = (neighbour == kNoNeighborSentinel);
  const Vector3 areaVector{mesh.faceAreaX[f], mesh.faceAreaY[f], mesh.faceAreaZ[f]};
  // TRAP 1: the OWNER's outward normal, always. The neighbour receives the NEGATED flux, not a
  // re-evaluation from its own side -- which is the same number mathematically and not bitwise.
  const Vector3 normal = deviceOutwardNormal(areaVector, mesh.faceArea[f], /*fromOwner=*/true);

  cfd::compressible::ReconstructedFaceState face{};
  if (isBoundary) {
    const DeviceBoundaryKind kind = static_cast<DeviceBoundaryKind>(mesh.faceBoundaryKind[f]);
    const PrimitiveState ext =
        deviceExteriorState(kind, primitives[owner], bc.prescribed[f], normal, ctrl);
    face = cfd::compressible::reconstructFirstOrderBoundary(primitives[owner], ext);
  } else {
    face = cfd::compressible::reconstructFirstOrder(primitives[owner], primitives[neighbour]);
  }

  // THE SHARED IMPLEMENTATION, called verbatim. hllFluxForControlOnly is HLLC with the contact wave
  // deleted and exists so control V1 can show a GPU path is not silently a different scheme.
  const HllcResult r =
      ctrl.useHllInsteadOfHllc
          ? cfd::compressible::hllFluxForControlOnly(face.left, face.right, normal, gamma,
                                                     WaveSpeedEstimator::ToroPressureBased)
          : cfd::compressible::hllcFlux(face.left, face.right, normal, gamma,
                                        WaveSpeedEstimator::ToroPressureBased);
  faceValid[f] = r.valid ? 1 : static_cast<int>(r.validity) + 1000;
  faceFlux[f] = cfd::compressible::integrateOverArea(r.flux, mesh.faceArea[f]);

  // V4 -- trap 1, made runnable. The neighbour's side re-evaluated with ITS outward normal and the
  // states swapped. In exact arithmetic this is exactly -faceFlux[f]; in floating point it is a
  // different rounding of the same quantity, which is the entire point of the control.
  if (ctrl.neighbourUsesOwnNormal && !isBoundary) {
    const Vector3 flipped = deviceOutwardNormal(areaVector, mesh.faceArea[f], /*fromOwner=*/false);
    const cfd::compressible::ReconstructedFaceState swapped =
        cfd::compressible::reconstructFirstOrder(primitives[neighbour], primitives[owner]);
    const HllcResult rf = cfd::compressible::hllcFlux(swapped.left, swapped.right, flipped, gamma,
                                                      WaveSpeedEstimator::ToroPressureBased);
    faceFluxFlipped[f] = cfd::compressible::integrateOverArea(rf.flux, mesh.faceArea[f]);
  }
}

// -------------------------------------------------------------------------------------------
// K2b -- the residual, one thread per CELL, gathering over that cell's ascending face list
// -------------------------------------------------------------------------------------------
__global__ void gatherResidualKernel(const FluxVector* faceFlux, const FluxVector* faceFluxFlipped,
                                     MeshView mesh, DeviceControls ctrl, StateView residual) {
  const Index c = static_cast<Index>(blockIdx.x) * blockDim.x + threadIdx.x;
  if (c >= mesh.cellCount) return;

  const Index begin = mesh.cellFaceOffsets[c];
  const Index end = mesh.cellFaceOffsets[c + 1];
  Real rho = 0.0;
  Real mx = 0.0;
  Real my = 0.0;
  Real mz = 0.0;
  Real en = 0.0;

  for (Index k = begin; k < end; ++k) {
    // V3: the control walks the same faces in the opposite order. Every term is identical and only
    // the ORDER changes, which is exactly the hypothesis "the ascending order is load-bearing".
    const Index slot = ctrl.reverseGatherOrder ? (end - 1 - (k - begin)) : k;
    const Index f = mesh.cellFaceIds[slot];
    const bool iAmOwner = (mesh.faceOwner[f] == c);
    const bool interior = (mesh.faceNeighbor[f] != kNoNeighborSentinel);
    // V4: the neighbour adds ITS OWN evaluation with a + sign, instead of negating the owner's.
    const bool useFlipped = ctrl.neighbourUsesOwnNormal && !iAmOwner && interior;
    const FluxVector g = useFlipped ? faceFluxFlipped[f] : faceFlux[f];
    const Real sign = (iAmOwner || useFlipped) ? 1.0 : -1.0;
    rho += sign * g.mass;
    mx += sign * g.momentum.x;
    my += sign * g.momentum.y;
    mz += sign * g.momentum.z;
    en += sign * g.energy;
  }

  residual.density[c] = rho;
  residual.momentumX[c] = mx;
  residual.momentumY[c] = my;
  residual.momentumZ[c] = mz;
  residual.totalEnergy[c] = en;
}

// -------------------------------------------------------------------------------------------
// K3 -- the per-cell dt, one thread per cell, gathering over the same ascending list
// -------------------------------------------------------------------------------------------
__global__ void localTimeStepKernel(const PrimitiveState* primitives, MeshView mesh,
                                    DeviceThermodynamics thermo, DeviceControls ctrl, Real cfl,
                                    Real* dt, int* ok) {
  const Index c = static_cast<Index>(blockIdx.x) * blockDim.x + threadIdx.x;
  if (c >= mesh.cellCount) return;

  const Index begin = mesh.cellFaceOffsets[c];
  const Index end = mesh.cellFaceOffsets[c + 1];
  Real speedSum = 0.0;
  for (Index k = begin; k < end; ++k) {
    const Index f = mesh.cellFaceIds[k];
    const bool iAmOwner = (mesh.faceOwner[f] == c);
    // TRAP 2: unlike the residual, THIS normal does flip per cell. The two kernels deliberately do
    // not share a normal helper on autopilot.
    const Vector3 normal = deviceOutwardNormal(
        Vector3{mesh.faceAreaX[f], mesh.faceAreaY[f], mesh.faceAreaZ[f]}, mesh.faceArea[f],
        iAmOwner);
    PrimitiveState faceValue = primitives[c];
    const Index neighbour = mesh.faceNeighbor[f];
    if (neighbour != kNoNeighborSentinel) {
      const Index other = iAmOwner ? neighbour : mesh.faceOwner[f];
      faceValue = deviceMeanForCfl(thermo, primitives[c], primitives[other], ctrl);
    }
    const Real un = cfd::compressible::hllc_detail::dotHD(faceValue.velocity, normal);
    speedSum += (cfd::compressible::hllc_detail::absR(un) + faceValue.speedOfSound) *
                mesh.faceArea[f];
  }

  if (!(speedSum > 0.0) || !cfd::compressible::hllc_detail::finiteR(speedSum)) {
    dt[c] = 0.0;
    ok[c] = 0;
    return;
  }
  const Real value = cfl * mesh.cellVolume[c] / speedSum;
  if (!cfd::compressible::hllc_detail::finiteR(value) || !(value > 0.0)) {
    dt[c] = 0.0;
    ok[c] = 0;
    return;
  }
  dt[c] = value;
  ok[c] = 1;
}

// -------------------------------------------------------------------------------------------
// K4 -- the global dt: a MIN reduction.
//
// min is associative and commutative in IEEE-754 for non-NaN operands, so any reduction tree gives
// the CPU's sequential answer BITWISE. That is why the time-step control can stay on device while
// the residual NORMS -- which are sums -- deliberately do not.
// -------------------------------------------------------------------------------------------
__global__ void minReduceKernel(const Real* values, const int* ok, Real* partial, Index count) {
  // ONE raw extern __shared__ array, cast per kernel: two `extern __shared__` declarations with
  // different element types in the same translation unit are incompatible under nvcc.
  extern __shared__ char sharedRaw[];
  Real* shared = reinterpret_cast<Real*>(sharedRaw);
  const unsigned tid = threadIdx.x;
  const Index i = static_cast<Index>(blockIdx.x) * blockDim.x + threadIdx.x;

  Real v = INFINITY;
  if (i < count && ok[i] != 0) v = values[i];
  shared[tid] = v;
  __syncthreads();

  for (unsigned s = blockDim.x / 2; s > 0; s >>= 1) {
    if (tid < s) {
      const Real other = shared[tid + s];
      if (other < shared[tid]) shared[tid] = other;
    }
    __syncthreads();
  }
  if (tid == 0) partial[blockIdx.x] = shared[0];
}

// The first inadmissible cell in ASCENDING id order -- a min-index reduction, order-independent for
// the same reason the dt reduction is. A reduction that returned ANY offender would compare equal
// on a one-offender case and diverge on a two-offender case, which is the kind of instrument that
// passes until it matters.
__global__ void minIndexReduceKernel(const int* statuses, Index* partial, Index count) {
  extern __shared__ char sharedRaw[];
  Index* sharedIdx = reinterpret_cast<Index*>(sharedRaw);
  const unsigned tid = threadIdx.x;
  const Index i = static_cast<Index>(blockIdx.x) * blockDim.x + threadIdx.x;

  Index v = count;  // "none", and larger than every real id
  if (i < count && statuses[i] != 0) v = i;
  sharedIdx[tid] = v;
  __syncthreads();

  for (unsigned s = blockDim.x / 2; s > 0; s >>= 1) {
    if (tid < s) {
      const Index other = sharedIdx[tid + s];
      if (other < sharedIdx[tid]) sharedIdx[tid] = other;
    }
    __syncthreads();
  }
  if (tid == 0) partial[blockIdx.x] = sharedIdx[0];
}

// -------------------------------------------------------------------------------------------
// K5 -- the update, one thread per cell
// -------------------------------------------------------------------------------------------
__global__ void applyUpdateKernel(ConstStateView q, const Real* residualDensity,
                                  const Real* residualMomentumX, const Real* residualMomentumY,
                                  const Real* residualMomentumZ, const Real* residualEnergy,
                                  const Real* cellVolume, const Real* dtPerCell, StateView out,
                                  Index cellCount) {
  const Index i = static_cast<Index>(blockIdx.x) * blockDim.x + threadIdx.x;
  if (i >= cellCount) return;
  const Real scale = dtPerCell[i] / cellVolume[i];
  out.density[i] = q.density[i] - scale * residualDensity[i];
  out.momentumX[i] = q.momentumX[i] - scale * residualMomentumX[i];
  out.momentumY[i] = q.momentumY[i] - scale * residualMomentumY[i];
  out.momentumZ[i] = q.momentumZ[i] - scale * residualMomentumZ[i];
  out.totalEnergy[i] = q.totalEnergy[i] - scale * residualEnergy[i];
}

// Fills a per-cell array with one value, so GlobalPhysical mode reuses exactly the same update
// kernel as LocalPseudoTime instead of having a second code path that could drift.
__global__ void fillKernel(Real* values, Real value, Index count) {
  const Index i = static_cast<Index>(blockIdx.x) * blockDim.x + threadIdx.x;
  if (i < count) values[i] = value;
}

bool fail(std::string* message, const char* what, cudaError_t status) {
  if (message != nullptr) *message = std::string(what) + ": " + cudaGetErrorString(status);
  return false;
}

bool failText(std::string* message, const std::string& what) {
  if (message != nullptr) *message = what;
  return false;
}

}  // namespace

// ===============================================================================================
// The host side
// ===============================================================================================

struct DeviceDensityBasedSolver::Impl {
  DeviceMesh mesh;
  DeviceThermodynamics thermo{};
  DeviceControls controls{};
  Real cfl{0.4};
  Real gamma{};
  int blockSize{128};
  bool uploaded{false};
  Index cellCount{};
  Index faceCount{};
  DeviceSolveStats stats{};

  DeviceBuffer<Real> qDensity, qMomentumX, qMomentumY, qMomentumZ, qEnergy;
  DeviceBuffer<Real> nextDensity, nextMomentumX, nextMomentumY, nextMomentumZ, nextEnergy;
  DeviceBuffer<Real> rDensity, rMomentumX, rMomentumY, rMomentumZ, rEnergy;
  DeviceBuffer<PrimitiveState> primitives;
  DeviceBuffer<int> statuses;
  DeviceBuffer<FluxVector> faceFlux;
  DeviceBuffer<FluxVector> faceFluxFlipped;  // V4 only; allocated always, filled only under control
  DeviceBuffer<int> faceValid;
  DeviceBuffer<int> faceBoundaryKind;
  DeviceBuffer<PrimitiveState> facePrescribed;
  DeviceBuffer<Real> dtPerCell;
  DeviceBuffer<int> dtOk;
  DeviceBuffer<Real> dtPartial;
  DeviceBuffer<Index> idxPartial;

  // The live state alternates between the two buffer sets rather than being copied back after
  // each step. DeviceBuffer has no swap(), and a device-to-device copy per step would be five
  // extra kernel-equivalent operations for no numerical purpose.
  bool parity{false};

  [[nodiscard]] ConstStateView currentState() const {
    return parity ? ConstStateView{nextDensity.data(), nextMomentumX.data(), nextMomentumY.data(),
                                   nextMomentumZ.data(), nextEnergy.data()}
                  : ConstStateView{qDensity.data(), qMomentumX.data(), qMomentumY.data(),
                                   qMomentumZ.data(), qEnergy.data()};
  }
  [[nodiscard]] StateView writeState() {
    return parity ? StateView{qDensity.data(), qMomentumX.data(), qMomentumY.data(),
                              qMomentumZ.data(), qEnergy.data()}
                  : StateView{nextDensity.data(), nextMomentumX.data(), nextMomentumY.data(),
                              nextMomentumZ.data(), nextEnergy.data()};
  }
  [[nodiscard]] DeviceBuffer<Real>& liveDensity() { return parity ? nextDensity : qDensity; }
  [[nodiscard]] const DeviceBuffer<Real>& liveDensity() const {
    return parity ? nextDensity : qDensity;
  }
  [[nodiscard]] DeviceBuffer<Real>& liveMomentumX() { return parity ? nextMomentumX : qMomentumX; }
  [[nodiscard]] const DeviceBuffer<Real>& liveMomentumX() const {
    return parity ? nextMomentumX : qMomentumX;
  }
  [[nodiscard]] DeviceBuffer<Real>& liveMomentumY() { return parity ? nextMomentumY : qMomentumY; }
  [[nodiscard]] const DeviceBuffer<Real>& liveMomentumY() const {
    return parity ? nextMomentumY : qMomentumY;
  }
  [[nodiscard]] DeviceBuffer<Real>& liveMomentumZ() { return parity ? nextMomentumZ : qMomentumZ; }
  [[nodiscard]] const DeviceBuffer<Real>& liveMomentumZ() const {
    return parity ? nextMomentumZ : qMomentumZ;
  }
  [[nodiscard]] DeviceBuffer<Real>& liveEnergy() { return parity ? nextEnergy : qEnergy; }
  [[nodiscard]] const DeviceBuffer<Real>& liveEnergy() const {
    return parity ? nextEnergy : qEnergy;
  }
  [[nodiscard]] StateView residualView() {
    return StateView{rDensity.data(), rMomentumX.data(), rMomentumY.data(), rMomentumZ.data(),
                     rEnergy.data()};
  }
  [[nodiscard]] MeshView meshView() const {
    return MeshView{mesh.cellVolumes(),     mesh.faceOwner(),        mesh.faceNeighbor(),
                    mesh.faceAreaX(),       mesh.faceAreaY(),        mesh.faceAreaZ(),
                    mesh.faceArea(),        mesh.cellFaceOffsets(),  mesh.cellFaceIds(),
                    faceBoundaryKind.data(), cellCount,              faceCount};
  }

  [[nodiscard]] int blocksFor(Index n) const {
    return static_cast<int>((n + blockSize - 1) / blockSize);
  }

  // recover -> face flux -> gather. Shared by assembleResidual(), step() and advance() so there is
  // exactly one residual path and a resident step cannot drift from a compared one.
  bool residualPipeline(std::string* message) {
    const int cellBlocks = blocksFor(cellCount);
    const int faceBlocks = blocksFor(faceCount);

    recoverKernel<<<cellBlocks, blockSize>>>(currentState(), thermo, controls, primitives.data(),
                                             statuses.data(), cellCount);
    faceFluxKernel<<<faceBlocks, blockSize>>>(primitives.data(), meshView(),
                                              BoundaryView{facePrescribed.data()}, thermo, controls,
                                              gamma, faceFlux.data(), faceFluxFlipped.data(),
                                              faceValid.data());
    gatherResidualKernel<<<cellBlocks, blockSize>>>(faceFlux.data(), faceFluxFlipped.data(),
                                                    meshView(), controls, residualView());
    stats.kernelLaunches += 3;
    cudaError_t status = cudaGetLastError();
    if (status == cudaSuccess) status = cudaDeviceSynchronize();
    if (status != cudaSuccess) return fail(message, "residual pipeline", status);
    return true;
  }

  // The recovery alone, with nothing downloaded. localTimeSteps() and globalTimeStep() need the
  // primitives but not the residual, and running the whole residual pipeline for them would make
  // the CFL comparison depend on a stage it does not use.
  bool oneStep(Real* dt, bool* admissible, Index* offendingCell, int* statusCode,
               std::string* message);

  bool recoverPrimitivesOnly(std::string* message) {
    recoverKernel<<<blocksFor(cellCount), blockSize>>>(currentState(), thermo, controls,
                                                       primitives.data(), statuses.data(),
                                                       cellCount);
    stats.kernelLaunches += 1;
    cudaError_t status = cudaGetLastError();
    if (status == cudaSuccess) status = cudaDeviceSynchronize();
    if (status != cudaSuccess) return fail(message, "recoverPrimitives", status);
    return true;
  }

  bool timeStepPipeline(std::string* message) {
    const int cellBlocks = blocksFor(cellCount);
    localTimeStepKernel<<<cellBlocks, blockSize>>>(primitives.data(), meshView(), thermo, controls,
                                                   cfl, dtPerCell.data(), dtOk.data());
    stats.kernelLaunches += 1;
    cudaError_t status = cudaGetLastError();
    if (status == cudaSuccess) status = cudaDeviceSynchronize();
    if (status != cudaSuccess) return fail(message, "local time step", status);
    return true;
  }

  // The min reduction, run to completion on device and brought back as ONE scalar.
  bool reduceGlobalDt(Real* out, std::string* message) {
    int blocks = blocksFor(cellCount);
    dtPartial.resize(blocks > 0 ? blocks : 1);
    const size_t sharedBytes = static_cast<size_t>(blockSize) * sizeof(Real);
    minReduceKernel<<<blocks, blockSize, sharedBytes>>>(dtPerCell.data(), dtOk.data(),
                                                        dtPartial.data(), cellCount);
    stats.kernelLaunches += 1;
    cudaError_t status = cudaGetLastError();
    if (status == cudaSuccess) status = cudaDeviceSynchronize();
    if (status != cudaSuccess) return fail(message, "dt reduction", status);

    std::vector<Real> partial(static_cast<size_t>(blocks));
    dtPartial.downloadTo(partial.data(), blocks);
    stats.deviceToHostTransfers += 1;
    stats.deviceToHostBytes += static_cast<Index>(partial.size() * sizeof(Real));

    Real smallest = partial.empty() ? 0.0 : partial[0];
    for (size_t i = 1; i < partial.size(); ++i) {
      if (partial[i] < smallest) smallest = partial[i];
    }
    if (!cfd::compressible::hllc_detail::finiteR(smallest) || !(smallest > 0.0)) {
      return failText(message,
                      "no cell yielded a finite positive dt -- the same refusal "
                      "DensityBasedSolver::globalTimeStep makes, and for the same reason");
    }
    *out = smallest;
    return true;
  }
};

// ONE STEP, in the CPU's order: primitives -> residual -> dt from THOSE SAME primitives ->
// update. The order is not incidental. DensityBasedSolver::solve computes dt from the primitives it
// already recovered, so a device loop that recovered them a second time would be computing dt from
// the same numbers by a different route -- still bitwise here, but a difference in structure that a
// later change could turn into a difference in result.
//
// The retry/rejection control loop deliberately stays on the HOST (audit.md §6): what this reports
// is the DECISION -- admissible or not, and which cell -- and the equivalence suite compares those
// decisions rather than re-deriving them on device.
bool DeviceDensityBasedSolver::Impl::oneStep(Real* dt, bool* admissible, Index* offendingCell,
                                             int* statusCode, std::string* message) {
  if (!residualPipeline(message)) return false;
  if (!timeStepPipeline(message)) return false;

  Real global = 0.0;
  if (!reduceGlobalDt(&global, message)) return false;

  const int cellBlocks = blocksFor(cellCount);
  fillKernel<<<cellBlocks, blockSize>>>(dtPerCell.data(), global, cellCount);
  applyUpdateKernel<<<cellBlocks, blockSize>>>(
      currentState(), rDensity.data(), rMomentumX.data(), rMomentumY.data(), rMomentumZ.data(),
      rEnergy.data(), mesh.cellVolumes(), dtPerCell.data(), writeState(), cellCount);
  stats.kernelLaunches += 2;
  cudaError_t status = cudaGetLastError();
  if (status == cudaSuccess) status = cudaDeviceSynchronize();
  if (status != cudaSuccess) return fail(message, "applyUpdate", status);

  parity = !parity;  // the candidate is now the live state

  // K6: validate the candidate with the SAME recovery the CPU's toPrimitive() applies at the top of
  // the next iteration, and reduce to the FIRST offender in ascending id order.
  if (!recoverPrimitivesOnly(message)) return false;
  Index firstOffender = cellCount;
  {
    const int blocks = cellBlocks > 0 ? cellBlocks : 1;
    idxPartial.resize(blocks);
    const size_t sharedBytes = static_cast<size_t>(blockSize) * sizeof(Index);
    minIndexReduceKernel<<<blocks, blockSize, sharedBytes>>>(statuses.data(), idxPartial.data(),
                                                             cellCount);
    stats.kernelLaunches += 1;
    status = cudaGetLastError();
    if (status == cudaSuccess) status = cudaDeviceSynchronize();
    if (status != cudaSuccess) return fail(message, "offending-cell reduction", status);
    std::vector<Index> partial(static_cast<size_t>(blocks));
    idxPartial.downloadTo(partial.data(), blocks);
    stats.deviceToHostTransfers += 1;
    for (const Index v : partial) {
      if (v < firstOffender) firstOffender = v;
    }
  }

  int reason = 0;
  if (firstOffender < cellCount) {
    std::vector<int> all(static_cast<size_t>(cellCount));
    statuses.downloadTo(all.data(), cellCount);
    stats.deviceToHostTransfers += 1;
    reason = all[static_cast<size_t>(firstOffender)];
  }

  if (dt != nullptr) *dt = global;
  if (admissible != nullptr) *admissible = (firstOffender >= cellCount);
  if (offendingCell != nullptr) *offendingCell = (firstOffender < cellCount) ? firstOffender : 0;
  if (statusCode != nullptr) *statusCode = reason;
  if (message != nullptr) message->clear();
  return true;
}

// --- the sanitizer controls (see the header for why they exist) ---------------------------------

namespace {

// Reads well past the end of its own allocation. memcheck must report an invalid read naming this
// kernel; if it does not, memcheck is not instrumenting this library and its silence elsewhere is
// worthless.
__global__ void sanitizerOobKernel(const Real* data, Index count, Real* sink) {
  const Index i = static_cast<Index>(blockIdx.x) * blockDim.x + threadIdx.x;
  if (i >= count) return;
  *sink = data[count + 4096];  // DELIBERATE, and unreachable from any production path
}

// Two threads write and read the same shared slot with no __syncthreads() between them. racecheck
// must report a hazard.
__global__ void sanitizerRaceKernel(Real* sink) {
  __shared__ Real slot[32];
  const unsigned t = threadIdx.x;
  slot[t] = static_cast<Real>(t);
  // DELIBERATELY NO __syncthreads() HERE.
  if (t < 31) *sink = slot[t + 1];
}

}  // namespace

bool sanitizerControlOutOfBoundsRead(std::string* message) {
  if (!densityBasedDeviceAvailable()) return failText(message, "no CUDA device available");
  DeviceBuffer<Real> data;
  DeviceBuffer<Real> sink;
  data.resize(64);
  sink.resize(1);
  sanitizerOobKernel<<<1, 64>>>(data.data(), 64, sink.data());
  cudaError_t status = cudaGetLastError();
  if (status == cudaSuccess) status = cudaDeviceSynchronize();
  // The kernel is EXPECTED to misbehave; what matters is that the tool saw it. Outside the
  // sanitizer this usually returns cudaSuccess, because an in-bounds-of-the-heap read of unowned
  // memory is not detected by the runtime -- which is precisely why the sanitizer is the
  // instrument and not the return code.
  (void)status;
  cudaGetLastError();  // clear any sticky error so later tests are unaffected
  if (message != nullptr) message->clear();
  return true;
}

bool sanitizerControlSharedMemoryRace(std::string* message) {
  if (!densityBasedDeviceAvailable()) return failText(message, "no CUDA device available");
  DeviceBuffer<Real> sink;
  sink.resize(1);
  sanitizerRaceKernel<<<1, 32>>>(sink.data());
  cudaError_t status = cudaGetLastError();
  if (status == cudaSuccess) status = cudaDeviceSynchronize();
  (void)status;
  cudaGetLastError();
  if (message != nullptr) message->clear();
  return true;
}

bool densityBasedDeviceAvailable() {
  int deviceCount = 0;
  const cudaError_t status = cudaGetDeviceCount(&deviceCount);
  return status == cudaSuccess && deviceCount > 0;
}

DeviceDensityBasedSolver::DeviceDensityBasedSolver() : impl_(std::make_unique<Impl>()) {}
DeviceDensityBasedSolver::~DeviceDensityBasedSolver() = default;

void DeviceDensityBasedSolver::setControls(const DeviceControls& controls) {
  impl_->controls = controls;
}
const DeviceControls& DeviceDensityBasedSolver::controls() const { return impl_->controls; }

void DeviceDensityBasedSolver::setBlockSize(int threads) {
  if (threads > 0 && threads <= 1024) impl_->blockSize = threads;
}
int DeviceDensityBasedSolver::blockSize() const { return impl_->blockSize; }

const DeviceSolveStats& DeviceDensityBasedSolver::stats() const { return impl_->stats; }
void DeviceDensityBasedSolver::resetStats() { impl_->stats = DeviceSolveStats{}; }

std::size_t DeviceDensityBasedSolver::residentBytes() const {
  const auto bytes = [](const auto& b) {
    return static_cast<std::size_t>(b.size()) * sizeof(*b.data());
  };
  const Impl& d = *impl_;
  return bytes(d.qDensity) + bytes(d.qMomentumX) + bytes(d.qMomentumY) + bytes(d.qMomentumZ) +
         bytes(d.qEnergy) + bytes(d.nextDensity) + bytes(d.nextMomentumX) + bytes(d.nextMomentumY) +
         bytes(d.nextMomentumZ) + bytes(d.nextEnergy) + bytes(d.rDensity) + bytes(d.rMomentumX) +
         bytes(d.rMomentumY) + bytes(d.rMomentumZ) + bytes(d.rEnergy) + bytes(d.primitives) +
         bytes(d.statuses) + bytes(d.faceFlux) + bytes(d.faceFluxFlipped) + bytes(d.faceValid) +
         bytes(d.faceBoundaryKind) +
         bytes(d.facePrescribed) + bytes(d.dtPerCell) + bytes(d.dtOk) + bytes(d.dtPartial) +
         bytes(d.idxPartial);
}

bool DeviceDensityBasedSolver::upload(const cfd::mesh::Mesh& mesh,
                                      const cfd::compressible::TotalEnergyState& thermo,
                                      const cfd::compressible::DensityBasedBoundaries& boundaries,
                                      cfd::Real cfl, std::string* message) {
  if (!densityBasedDeviceAvailable()) return failText(message, "no CUDA device available");
  if (!(cfl > 0.0) || !cfd::compressible::hllc_detail::finiteR(cfl)) {
    return failText(message, "cfl must be finite and > 0");
  }

  Impl& d = *impl_;
  d.cellCount = static_cast<Index>(mesh.numberOfCells());
  d.faceCount = static_cast<Index>(mesh.numberOfFaces());
  if (d.cellCount == 0 || d.faceCount == 0) return failText(message, "the mesh is empty");
  d.cfl = cfl;

  // THE ASCENDING-FACE-LIST INVARIANT, checked rather than assumed. Everything in this file that
  // claims bitwise equality with the CPU depends on it, and the cost of verifying it is one pass
  // over the connectivity at upload. If a future mesh construction path ever broke it, this REFUSES
  // instead of silently producing a residual that no longer matches the CPU's accumulation order.
  for (Index c = 0; c < d.cellCount; ++c) {
    const auto& ids = mesh.cell(c).faceIds();
    for (std::size_t k = 1; k < ids.size(); ++k) {
      if (!(ids[k] > ids[k - 1])) {
        return failText(message,
                        "cell " + std::to_string(c) +
                            "'s face list is not ascending, so a per-cell gather cannot reproduce "
                            "the CPU's ascending face-loop accumulation order. Refusing rather "
                            "than producing a residual that silently differs.");
      }
    }
  }

  // The nine thermodynamic scalars. eFloor comes from the PRODUCTION function, evaluated on the
  // host, so a constant is shipped rather than a polynomial restated.
  const auto& properties = thermo.thermodynamics();
  const auto& enthalpy = thermo.enthalpyModel();
  const auto& cpModel = enthalpy.specificHeatModel();
  d.thermo.gasConstant = properties.gasConstant();
  d.thermo.specificHeatP = properties.specificHeatPressure();
  d.thermo.specificHeatV = properties.specificHeatVolume();
  d.thermo.specificHeatRatio = properties.specificHeatRatio();
  d.thermo.referenceT = enthalpy.referenceTemperature();
  d.thermo.referenceH = enthalpy.referenceEnthalpy();
  d.thermo.minimumT = cpModel.minimumTemperature();
  d.thermo.maximumT = cpModel.maximumTemperature();
  try {
    d.thermo.internalEnergyFloor = thermo.specificInternalEnergy(d.thermo.minimumT);
  } catch (const cfd::Error& e) {
    return failText(message, std::string("could not evaluate the internal-energy floor: ") +
                                 e.what());
  }
  d.gamma = d.thermo.specificHeatRatio;

  // Per-face boundary treatment. REFUSES exactly where the CPU does: a patch with no treatment, and
  // a boundary face in no patch. An unmapped patch silently treated as a wall is the kind of
  // default that makes a wrong answer look plausible.
  //
  // NOTE, recorded rather than glossed: the CPU's boundaryByFace() has internal linkage in
  // src/compressible/DensityBasedSolver.cpp, so this is a second implementation of that mapping
  // rather than a call to it. The two refusals are therefore proved EQUIVALENT by test instead of
  // shared by construction -- see implementation.md.
  std::vector<int> kinds(static_cast<size_t>(d.faceCount), -1);
  std::vector<PrimitiveState> prescribed(static_cast<size_t>(d.faceCount));
  for (const auto& patch : mesh.boundaryPatches()) {
    const auto it = boundaries.find(std::string(patch.name()));
    if (it == boundaries.end()) {
      return failText(message, "boundary patch \"" + std::string(patch.name()) +
                                   "\" has no density-based boundary treatment; every patch must "
                                   "be given one explicitly (an unmapped patch is not assumed to "
                                   "be a wall)");
    }
    for (const Index f : patch.faceIds()) {
      kinds[static_cast<size_t>(f)] = static_cast<int>(it->second.kind);
      prescribed[static_cast<size_t>(f)] = it->second.prescribed;
    }
  }
  for (Index f = 0; f < d.faceCount; ++f) {
    if (mesh.face(f).isBoundary() && kinds[static_cast<size_t>(f)] < 0) {
      return failText(message, "boundary face " + std::to_string(f) +
                                   " belongs to no boundary patch, so it has no treatment");
    }
  }

  try {
    d.mesh.upload(mesh);
    const Index n = d.cellCount;
    const Index m = d.faceCount;
    for (auto* b : {&d.qDensity, &d.qMomentumX, &d.qMomentumY, &d.qMomentumZ, &d.qEnergy,
                    &d.nextDensity, &d.nextMomentumX, &d.nextMomentumY, &d.nextMomentumZ,
                    &d.nextEnergy, &d.rDensity, &d.rMomentumX, &d.rMomentumY, &d.rMomentumZ,
                    &d.rEnergy, &d.dtPerCell}) {
      b->resize(n);
    }
    d.primitives.resize(n);
    d.statuses.resize(n);
    d.dtOk.resize(n);
    d.faceFlux.resize(m);
    d.faceFluxFlipped.resize(m);
    d.faceValid.resize(m);
    d.faceBoundaryKind.resize(m);
    d.facePrescribed.resize(m);
    d.faceBoundaryKind.uploadFrom(kinds.data(), m);
    d.facePrescribed.uploadFrom(prescribed.data(), m);
    d.stats.hostToDeviceTransfers += 2;
    d.stats.hostToDeviceBytes +=
        static_cast<Index>(m * sizeof(int) + m * sizeof(PrimitiveState));
  } catch (const cfd::Error& e) {
    return failText(message, std::string("device allocation failed: ") + e.what());
  }

  d.uploaded = true;
  if (message != nullptr) message->clear();
  return true;
}

bool DeviceDensityBasedSolver::setState(const cfd::compressible::ConservedField& q,
                                        std::string* message) {
  Impl& d = *impl_;
  if (!d.uploaded) return failText(message, "upload() has not been called");
  if (q.size() != d.cellCount) return failText(message, "state size does not match the mesh");

  std::vector<Real> mx(static_cast<size_t>(d.cellCount));
  std::vector<Real> my(static_cast<size_t>(d.cellCount));
  std::vector<Real> mz(static_cast<size_t>(d.cellCount));
  for (Index i = 0; i < d.cellCount; ++i) {
    mx[static_cast<size_t>(i)] = q.momentumDensity[i].x;
    my[static_cast<size_t>(i)] = q.momentumDensity[i].y;
    mz[static_cast<size_t>(i)] = q.momentumDensity[i].z;
  }
  try {
    d.liveDensity().uploadFrom(q.density.data(), d.cellCount);
    d.liveMomentumX().uploadFrom(mx.data(), d.cellCount);
    d.liveMomentumY().uploadFrom(my.data(), d.cellCount);
    d.liveMomentumZ().uploadFrom(mz.data(), d.cellCount);
    d.liveEnergy().uploadFrom(q.totalEnergyDensity.data(), d.cellCount);
  } catch (const cfd::Error& e) {
    return failText(message, std::string("state upload failed: ") + e.what());
  }
  d.stats.hostToDeviceTransfers += 5;
  d.stats.hostToDeviceBytes += static_cast<Index>(5 * d.cellCount * sizeof(Real));
  if (message != nullptr) message->clear();
  return true;
}

bool DeviceDensityBasedSolver::getState(cfd::compressible::ConservedField* q,
                                        std::string* message) const {
  Impl& d = *impl_;
  if (!d.uploaded) return failText(message, "upload() has not been called");
  if (q == nullptr) return failText(message, "output state is null");

  const size_t n = static_cast<size_t>(d.cellCount);
  std::vector<Real> rho(n), mx(n), my(n), mz(n), en(n);
  try {
    d.liveDensity().downloadTo(rho.data(), d.cellCount);
    d.liveMomentumX().downloadTo(mx.data(), d.cellCount);
    d.liveMomentumY().downloadTo(my.data(), d.cellCount);
    d.liveMomentumZ().downloadTo(mz.data(), d.cellCount);
    d.liveEnergy().downloadTo(en.data(), d.cellCount);
  } catch (const cfd::Error& e) {
    return failText(message, std::string("state download failed: ") + e.what());
  }
  q->density = cfd::fields::ScalarField(d.cellCount);
  q->momentumDensity = cfd::fields::VectorField(d.cellCount);
  q->totalEnergyDensity = cfd::fields::ScalarField(d.cellCount);
  for (Index i = 0; i < d.cellCount; ++i) {
    const size_t k = static_cast<size_t>(i);
    q->density[i] = rho[k];
    q->momentumDensity[i] = Vector3{mx[k], my[k], mz[k]};
    q->totalEnergyDensity[i] = en[k];
  }
  d.stats.deviceToHostTransfers += 5;
  d.stats.deviceToHostBytes += static_cast<Index>(5 * d.cellCount * sizeof(Real));
  if (message != nullptr) message->clear();
  return true;
}

bool DeviceDensityBasedSolver::recoverPrimitives(
    std::vector<cfd::compressible::PrimitiveState>* primitives, std::vector<int>* statuses,
    std::string* message) {
  Impl& d = *impl_;
  if (!d.uploaded) return failText(message, "upload() has not been called");
  recoverKernel<<<d.blocksFor(d.cellCount), d.blockSize>>>(
      d.currentState(), d.thermo, d.controls, d.primitives.data(), d.statuses.data(), d.cellCount);
  d.stats.kernelLaunches += 1;
  cudaError_t status = cudaGetLastError();
  if (status == cudaSuccess) status = cudaDeviceSynchronize();
  if (status != cudaSuccess) return fail(message, "recoverPrimitives", status);

  if (primitives != nullptr) {
    primitives->assign(static_cast<size_t>(d.cellCount), PrimitiveState{});
    d.primitives.downloadTo(primitives->data(), d.cellCount);
    d.stats.deviceToHostTransfers += 1;
  }
  if (statuses != nullptr) {
    statuses->assign(static_cast<size_t>(d.cellCount), 0);
    d.statuses.downloadTo(statuses->data(), d.cellCount);
    d.stats.deviceToHostTransfers += 1;
  }
  if (message != nullptr) message->clear();
  return true;
}

bool DeviceDensityBasedSolver::faceFluxes(std::vector<cfd::compressible::FluxVector>* fluxes,
                                          std::string* message) {
  Impl& d = *impl_;
  if (!d.uploaded) return failText(message, "upload() has not been called");
  if (!d.residualPipeline(message)) return false;
  if (fluxes != nullptr) {
    fluxes->assign(static_cast<size_t>(d.faceCount), FluxVector{});
    d.faceFlux.downloadTo(fluxes->data(), d.faceCount);
    d.stats.deviceToHostTransfers += 1;
  }
  if (message != nullptr) message->clear();
  return true;
}

bool DeviceDensityBasedSolver::assembleResidual(cfd::compressible::ResidualField* residual,
                                                std::string* message) {
  Impl& d = *impl_;
  if (!d.uploaded) return failText(message, "upload() has not been called");
  if (!d.residualPipeline(message)) return false;
  if (residual == nullptr) return true;

  const size_t n = static_cast<size_t>(d.cellCount);
  std::vector<Real> rho(n), mx(n), my(n), mz(n), en(n);
  d.rDensity.downloadTo(rho.data(), d.cellCount);
  d.rMomentumX.downloadTo(mx.data(), d.cellCount);
  d.rMomentumY.downloadTo(my.data(), d.cellCount);
  d.rMomentumZ.downloadTo(mz.data(), d.cellCount);
  d.rEnergy.downloadTo(en.data(), d.cellCount);
  d.stats.deviceToHostTransfers += 5;

  residual->density = cfd::fields::ScalarField(d.cellCount);
  residual->momentum = cfd::fields::VectorField(d.cellCount);
  residual->totalEnergy = cfd::fields::ScalarField(d.cellCount);
  for (Index i = 0; i < d.cellCount; ++i) {
    const size_t k = static_cast<size_t>(i);
    residual->density[i] = rho[k];
    residual->momentum[i] = Vector3{mx[k], my[k], mz[k]};
    residual->totalEnergy[i] = en[k];
  }
  if (message != nullptr) message->clear();
  return true;
}

bool DeviceDensityBasedSolver::localTimeSteps(std::vector<cfd::Real>* dt, std::string* message) {
  Impl& d = *impl_;
  if (!d.uploaded) return failText(message, "upload() has not been called");
  if (!d.recoverPrimitivesOnly(message)) return false;
  if (!d.timeStepPipeline(message)) return false;

  std::vector<int> ok(static_cast<size_t>(d.cellCount));
  d.dtOk.downloadTo(ok.data(), d.cellCount);
  if (dt != nullptr) {
    dt->assign(static_cast<size_t>(d.cellCount), 0.0);
    d.dtPerCell.downloadTo(dt->data(), d.cellCount);
  }
  d.stats.deviceToHostTransfers += 2;
  for (Index i = 0; i < d.cellCount; ++i) {
    if (ok[static_cast<size_t>(i)] == 0) {
      return failText(message, "cell " + std::to_string(i) +
                                   " yielded a non-positive or non-finite dt -- the same refusal "
                                   "DensityBasedSolver::localTimeSteps makes");
    }
  }
  if (message != nullptr) message->clear();
  return true;
}

bool DeviceDensityBasedSolver::globalTimeStep(cfd::Real* dt, std::string* message) {
  Impl& d = *impl_;
  if (!d.uploaded) return failText(message, "upload() has not been called");
  if (!d.recoverPrimitivesOnly(message)) return false;
  if (!d.timeStepPipeline(message)) return false;
  if (dt == nullptr) return failText(message, "output dt is null");
  return d.reduceGlobalDt(dt, message);
}

bool DeviceDensityBasedSolver::step(cfd::Real* dtUsed, bool* admissible, cfd::Index* offendingCell,
                                    int* status, std::string* message) {
  Impl& d = *impl_;
  if (!d.uploaded) return failText(message, "upload() has not been called");
  Real dt = 0.0;
  if (!d.oneStep(&dt, admissible, offendingCell, status, message)) return false;
  if (dtUsed != nullptr) *dtUsed = dt;
  return true;
}

bool DeviceDensityBasedSolver::advance(cfd::Index steps, std::vector<cfd::Real>* dtPerStep,
                                       cfd::Index* stepsTaken, std::string* message) {
  Impl& d = *impl_;
  if (!d.uploaded) return failText(message, "upload() has not been called");
  if (dtPerStep != nullptr) dtPerStep->clear();
  Index taken = 0;
  for (Index s = 0; s < steps; ++s) {
    Real dt = 0.0;
    bool admissible = false;
    Index offending = 0;
    int statusCode = 0;
    if (!d.oneStep(&dt, &admissible, &offending, &statusCode, message)) {
      if (stepsTaken != nullptr) *stepsTaken = taken;
      return false;
    }
    if (dtPerStep != nullptr) dtPerStep->push_back(dt);
    ++taken;
    if (!admissible) {
      if (stepsTaken != nullptr) *stepsTaken = taken;
      return failText(message, "step " + std::to_string(s) + " produced an inadmissible state at "
                                                             "cell " +
                                   std::to_string(offending));
    }
  }
  if (stepsTaken != nullptr) *stepsTaken = taken;
  if (message != nullptr) message->clear();
  return true;
}

}  // namespace cfd::gpu
