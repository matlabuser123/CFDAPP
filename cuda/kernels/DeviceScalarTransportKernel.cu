// P14 Phase I -- device assembly of the generic scalar transport equation.
//
// See DeviceScalarTransport.hpp for why only three things are new here: the
// Patankar source, the conservative transient term, and the composition. The
// diffusion and convection halves are the already-qualified ports
// assembleScalarDiffusionDevice / assembleScalarConvectionDevice, which
// reproduce the exact production assemblers the host path composes.

#include <cuda_runtime.h>

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <utility>
#include <string>
#include <vector>

#include "cfd/core/Timer.hpp"
#include "cfd/gpu/CudaCheck.hpp"
#include "cfd/gpu/DeviceScalarTransport.hpp"
#include "cfd/gpu/GPUExecutionStats.hpp"

namespace cfd::gpu {

using cfd::Index;
using cfd::Real;

namespace {

constexpr int kThreadsPerBlock = 256;

int blockCountFor(Index count) {
  return static_cast<int>((count + kThreadsPerBlock - 1) / kThreadsPerBlock);
}

// values += other, elementwise. The two systems share the candidate sparsity
// pattern (checked by the caller before this runs), so adding the value arrays
// is exactly adding the matrices.
__global__ void addValuesKernel(Index count, const Real* __restrict__ other,
                                Real* __restrict__ values) {
  const Index i = blockIdx.x * blockDim.x + threadIdx.x;
  if (i >= count) return;
  values[i] += other[i];
}

// Patankar: a_P -= Sp_P V_P (diagonal), b_P += Su_P V_P.
//
// The diagonal is located by scanning the row for column == row rather than
// assuming a position. The candidate pattern stores columns ascending, so the
// diagonal's index within a row depends on how many neighbours precede it --
// assuming "first entry" would be wrong for every cell except those on the
// low-index boundary, and wrong in a way that still produces a full matrix.
__global__ void scalarSourceKernel(Index cellCount, const Index* __restrict__ rowOffsets,
                                   const Index* __restrict__ columnIndices,
                                   const Real* __restrict__ cellVolumes,
                                   const Real* __restrict__ Su, const Real* __restrict__ Sp,
                                   Real* __restrict__ values, Real* __restrict__ rhs) {
  const Index cell = blockIdx.x * blockDim.x + threadIdx.x;
  if (cell >= cellCount) return;
  const Real volume = cellVolumes[cell];
  for (Index k = rowOffsets[cell]; k < rowOffsets[cell + 1]; ++k) {
    if (columnIndices[k] == cell) {
      values[k] += -Sp[cell] * volume;
      break;
    }
  }
  rhs[cell] += Su[cell] * volume;
}

// Conservative implicit Euler:
//   a_P += rho^{n+1} V / dt        (NEW density)
//   b_P += rho^n phi^n V / dt      (OLD density, old phi)
__global__ void transientKernel(Index cellCount, const Index* __restrict__ rowOffsets,
                                const Index* __restrict__ columnIndices,
                                const Real* __restrict__ cellVolumes,
                                const Real* __restrict__ phiOld, const Real* __restrict__ rhoOld,
                                const Real* __restrict__ rhoNew, Real dt, Real* __restrict__ values,
                                Real* __restrict__ rhs) {
  const Index cell = blockIdx.x * blockDim.x + threadIdx.x;
  if (cell >= cellCount) return;
  const Real volume = cellVolumes[cell];
  for (Index k = rowOffsets[cell]; k < rowOffsets[cell + 1]; ++k) {
    if (columnIndices[k] == cell) {
      values[k] += rhoNew[cell] * volume / dt;
      break;
    }
  }
  rhs[cell] += rhoOld[cell] * phiOld[cell] * volume / dt;
}

// Patankar implicit under-relaxation, row-local.
//
// `extra` is read from values[k] BEFORE values[k] is written, so both the
// diagonal scaling and the right-hand side correction use the PRE-RELAXATION
// a_P. That ordering is the whole subtlety of this kernel: swapping the two
// lines still produces a convergent, plausible, wrong system whose error
// depends on alpha and looks like a physics difference.
//
// The arithmetic is the host's, term for term
// (pressure_velocity/UnderRelaxation.cpp), because the target is bitwise
// agreement and `a_P/alpha` is not the same double as `a_P + a_P*(1/alpha-1)`.
__global__ void underRelaxKernel(Index cellCount, const Index* __restrict__ rowOffsets,
                                 const Index* __restrict__ columnIndices,
                                 const Real* __restrict__ phiOld, Real factor,
                                 Real* __restrict__ values, Real* __restrict__ rhs) {
  const Index cell = blockIdx.x * blockDim.x + threadIdx.x;
  if (cell >= cellCount) return;
  for (Index k = rowOffsets[cell]; k < rowOffsets[cell + 1]; ++k) {
    if (columnIndices[k] == cell) {
      const Real extra = values[k] * factor;  // PRE-relaxation diagonal
      values[k] += extra;
      rhs[cell] += extra * phiOld[cell];
      break;
    }
  }
}

// fixRow on device: discard the row, unit diagonal, overwrite the RHS.
//
// One thread per IMPOSED CELL, not per cell -- the imposed list is tiny (wall
// cells) and a full-mesh launch that tests membership would need the list
// searched by every thread. `missingDiagonal` fails the call closed when the
// candidate pattern has no diagonal slot for a row; see the header for why a
// silently all-zero row is worse than an exception.
__global__ void fixRowsKernel(Index imposedCount, const Index* __restrict__ cells,
                              const Real* __restrict__ imposedValues,
                              const Index* __restrict__ rowOffsets,
                              const Index* __restrict__ columnIndices, Real* __restrict__ values,
                              Real* __restrict__ rhs, int* __restrict__ missingDiagonal) {
  const Index slot = blockIdx.x * blockDim.x + threadIdx.x;
  if (slot >= imposedCount) return;
  const Index row = cells[slot];
  bool foundDiagonal = false;
  for (Index k = rowOffsets[row]; k < rowOffsets[row + 1]; ++k) {
    if (columnIndices[k] == row) {
      values[k] = 1.0;
      foundDiagonal = true;
    } else {
      values[k] = 0.0;
    }
  }
  if (!foundDiagonal) *missingDiagonal = 1;
  rhs[row] = imposedValues[slot];
}

// Fail-closed structural check, performed ON THE DEVICE.
//
// The first version downloaded both index arrays and compared them on the host.
// It was correct but expensive, and the Phase B12 residency audit measured what
// it cost: 4 D2H calls and 609 KB PER ASSEMBLY, to re-verify something that
// cannot change between two assemblies built from the same plans. B12's rule is
// that per-iteration transfers need a documented blocker; there was no blocker,
// only a lazy implementation.
//
// Now a kernel compares the arrays and sets one flag, and only that flag comes
// back: 4 bytes instead of 609 KB, with the guard fully intact. A silent
// pattern mismatch would add coefficients into the wrong columns and still
// leave a plausible matrix, so the check is worth keeping -- it just is not
// worth a megabyte.
__global__ void comparePatternKernel(Index count, const Index* __restrict__ a,
                                     const Index* __restrict__ b, int* __restrict__ mismatch) {
  const Index i = blockIdx.x * blockDim.x + threadIdx.x;
  if (i >= count) return;
  if (a[i] != b[i]) *mismatch = 1;
}

void requireSamePattern(const DeviceBuffer<Index>& aOffsets, const DeviceBuffer<Index>& aColumns,
                        const DeviceBuffer<Index>& bOffsets, const DeviceBuffer<Index>& bColumns) {
  if (aOffsets.size() != bOffsets.size() || aColumns.size() != bColumns.size()) {
    throw std::runtime_error(
        "assembleScalarTransportDevice: diffusion and convection systems have different sparsity "
        "sizes");
  }
  // P16 Step 4: reused rather than reallocated. A one-int buffer cost one
  // device allocation per call, and this is called several times per
  // turbulence correction -- part of the 30 allocations the production
  // residency ledger measured. Same function-local-static pattern
  // reduceToHost's partialSumsCache already uses. resize(1) after the first
  // call is a no-op, and uploadFrom still clears it to zero every time.
  static DeviceBuffer<int> mismatch;
  const int zero = 0;
  mismatch.uploadFrom(&zero, 1);
  if (aOffsets.size() > 0) {
    comparePatternKernel<<<blockCountFor(aOffsets.size()), kThreadsPerBlock>>>(
        aOffsets.size(), aOffsets.data(), bOffsets.data(), mismatch.data());
    checkCuda(cudaGetLastError(), "comparePatternKernel(offsets)");
  }
  if (aColumns.size() > 0) {
    comparePatternKernel<<<blockCountFor(aColumns.size()), kThreadsPerBlock>>>(
        aColumns.size(), aColumns.data(), bColumns.data(), mismatch.data());
    checkCuda(cudaGetLastError(), "comparePatternKernel(columns)");
  }
  checkCuda(cudaDeviceSynchronize(), "comparePatternKernel sync");
  int flag = 0;
  mismatch.downloadTo(&flag, 1);
  if (flag != 0) {
    throw std::runtime_error(
        "assembleScalarTransportDevice: diffusion and convection sparsity patterns differ");
  }
}

}  // namespace

void applyScalarSourceDevice(const DeviceBuffer<Real>& cellVolumes, const DeviceBuffer<Real>& Su,
                             const DeviceBuffer<Real>& Sp, DeviceScalarTransportSystem& system) {
  const Index cellCount = cellVolumes.size();
  if (cellCount == 0) return;
  cfd::Timer timer;
  scalarSourceKernel<<<blockCountFor(cellCount), kThreadsPerBlock>>>(
      cellCount, system.rowOffsets.data(), system.columnIndices.data(), cellVolumes.data(),
      Su.data(), Sp.data(), system.values.data(), system.rhs.data());
  checkCuda(cudaGetLastError(), "scalarSourceKernel");
  checkCuda(cudaDeviceSynchronize(), "scalarSourceKernel sync");
  auto& stats = gpuExecutionStats();
  ++stats.kernelLaunches;
  stats.kernelSeconds += timer.elapsedSeconds();
}

void applyTransientDevice(const DeviceBuffer<Real>& cellVolumes,
                          const DeviceTransportTimeState& time,
                          DeviceScalarTransportSystem& system) {
  if (!time.transient) return;  // no launch at all, not a zero-valued one
  if (time.phiOld == nullptr || time.rhoOld == nullptr || time.rhoNew == nullptr) {
    throw std::runtime_error("applyTransientDevice: a transient state needs phiOld/rhoOld/rhoNew");
  }
  if (!(time.dt > 0.0)) {
    throw std::runtime_error("applyTransientDevice: dt must be > 0");
  }
  const Index cellCount = cellVolumes.size();
  if (cellCount == 0) return;
  cfd::Timer timer;
  transientKernel<<<blockCountFor(cellCount), kThreadsPerBlock>>>(
      cellCount, system.rowOffsets.data(), system.columnIndices.data(), cellVolumes.data(),
      time.phiOld->data(), time.rhoOld->data(), time.rhoNew->data(), time.dt, system.values.data(),
      system.rhs.data());
  checkCuda(cudaGetLastError(), "transientKernel");
  checkCuda(cudaDeviceSynchronize(), "transientKernel sync");
  auto& stats = gpuExecutionStats();
  ++stats.kernelLaunches;
  stats.kernelSeconds += timer.elapsedSeconds();
}

void applyImplicitUnderRelaxationDevice(const DeviceBuffer<Real>& phiOld, Real alpha,
                                        DeviceScalarTransportSystem& system) {
  if (!std::isfinite(alpha) || !(alpha > 0.0) || alpha > 1.0) {
    throw std::runtime_error(
        "applyImplicitUnderRelaxationDevice: alpha must be finite and in (0, 1]");
  }
  if (alpha == 1.0) {
    return;  // exact no-op, and no launch -- same as the host early return
  }
  const Index cellCount = system.rhs.size();
  if (cellCount == 0) return;
  if (phiOld.size() != cellCount) {
    throw std::runtime_error(
        "applyImplicitUnderRelaxationDevice: phiOld size does not match the system row count");
  }
  const Real factor = (1.0 / alpha) - 1.0;  // the host's expression, unchanged
  cfd::Timer timer;
  underRelaxKernel<<<blockCountFor(cellCount), kThreadsPerBlock>>>(
      cellCount, system.rowOffsets.data(), system.columnIndices.data(), phiOld.data(), factor,
      system.values.data(), system.rhs.data());
  checkCuda(cudaGetLastError(), "underRelaxKernel");
  checkCuda(cudaDeviceSynchronize(), "underRelaxKernel sync");
  auto& stats = gpuExecutionStats();
  ++stats.kernelLaunches;
  stats.kernelSeconds += timer.elapsedSeconds();
}

void uploadImposedCellsDevice(const std::vector<Index>& cells, const std::vector<Real>& values,
                              Index cellCount, DeviceImposedCells& imposed) {
  if (cells.size() != values.size()) {
    throw std::runtime_error("uploadImposedCellsDevice: cell and value lists differ in length");
  }
  for (std::size_t i = 0; i < cells.size(); ++i) {
    if (cells[i] >= cellCount) {
      throw std::runtime_error("uploadImposedCellsDevice: imposed cell index is outside the mesh");
    }
    if (!std::isfinite(values[i])) {
      throw std::runtime_error("uploadImposedCellsDevice: imposed value must be finite");
    }
    for (std::size_t j = i + 1; j < cells.size(); ++j) {
      if (cells[j] == cells[i]) {
        throw std::runtime_error(
            "uploadImposedCellsDevice: duplicate imposed cell " + std::to_string(cells[i]) +
            " -- two device threads would write one row and the winner would be the scheduler's");
      }
    }
  }
  imposed.cells.uploadFrom(cells.data(), static_cast<Index>(cells.size()));
  imposed.values.uploadFrom(values.data(), static_cast<Index>(values.size()));
}

void fixImposedRowsDevice(const DeviceImposedCells& imposed, DeviceScalarTransportSystem& system) {
  const Index imposedCount = imposed.cells.size();
  if (imposedCount == 0) return;  // no launch: an empty list leaves the system untouched
  if (imposed.values.size() != imposedCount) {
    throw std::runtime_error("fixImposedRowsDevice: cell and value buffers differ in length");
  }
  // P16 Step 4: reused rather than reallocated. A one-int buffer cost one
  // device allocation per call, and this is called several times per
  // turbulence correction -- part of the 30 allocations the production
  // residency ledger measured. Same function-local-static pattern
  // reduceToHost's partialSumsCache already uses. resize(1) after the first
  // call is a no-op, and uploadFrom still clears it to zero every time.
  static DeviceBuffer<int> missingDiagonal;
  const int zero = 0;
  missingDiagonal.uploadFrom(&zero, 1);

  cfd::Timer timer;
  fixRowsKernel<<<blockCountFor(imposedCount), kThreadsPerBlock>>>(
      imposedCount, imposed.cells.data(), imposed.values.data(), system.rowOffsets.data(),
      system.columnIndices.data(), system.values.data(), system.rhs.data(), missingDiagonal.data());
  checkCuda(cudaGetLastError(), "fixRowsKernel");
  checkCuda(cudaDeviceSynchronize(), "fixRowsKernel sync");
  auto& stats = gpuExecutionStats();
  ++stats.kernelLaunches;
  stats.kernelSeconds += timer.elapsedSeconds();

  int flag = 0;
  missingDiagonal.downloadTo(&flag, 1);
  if (flag != 0) {
    throw std::runtime_error(
        "fixImposedRowsDevice: an imposed row has no diagonal slot in the candidate pattern, so "
        "the unit diagonal has nowhere to go and the row would be silently all-zero");
  }
}

void assembleScalarTransportDevice(
    const DeviceDiffusionPlan& diffusionPlan, const DeviceConvectionPlan& convectionPlan,
    const DeviceBuffer<Real>& cellVolumes, const DeviceBuffer<Real>& phi,
    const DeviceBuffer<Real>& massFlux, const DeviceBuffer<Real>& diffusivity,
    const DeviceBuffer<Real>& Su, const DeviceBuffer<Real>& Sp, bool nonOrthogonalEnabled,
    const DeviceTransportTimeState& time, DeviceScalarTransportSystem& system) {
  // Same order as the host: diffusion, convection, source, transient.
  DeviceDiffusionSystem& diffusion = system.diffusionScratch;
  assembleScalarDiffusionDevice(diffusionPlan, phi, diffusivity, nonOrthogonalEnabled, diffusion);

  DeviceConvectionSystem& convection = system.convectionScratch;
  assembleScalarConvectionDevice(convectionPlan, phi, massFlux, /*specificHeatField=*/nullptr,
                                 /*specificHeat=*/1.0, convection);

  requireSamePattern(diffusion.rowOffsets, diffusion.columnIndices, convection.rowOffsets,
                     convection.columnIndices);

  // Take the diffusion system as the base and add convection into it.
  // SWAPPED, not moved. `diffusion` IS system.diffusionScratch, so a move
  // leaves the scratch empty and assembleScalarDiffusionDevice reallocates all
  // four buffers on the next call -- four device allocations per assembly,
  // eight per turbulence correction. The swap hands the scratch the PREVIOUS
  // iteration's buffers instead, already the right size, so the next resize is
  // a no-op. Same rotation the correction's own publication uses.
  std::swap(system.rowOffsets, diffusion.rowOffsets);
  std::swap(system.columnIndices, diffusion.columnIndices);
  std::swap(system.values, diffusion.values);
  std::swap(system.rhs, diffusion.rhs);

  const Index valueCount = system.values.size();
  const Index cellCount = cellVolumes.size();
  if (valueCount > 0) {
    cfd::Timer timer;
    addValuesKernel<<<blockCountFor(valueCount), kThreadsPerBlock>>>(
        valueCount, convection.values.data(), system.values.data());
    checkCuda(cudaGetLastError(), "addValuesKernel(values)");
    addValuesKernel<<<blockCountFor(cellCount), kThreadsPerBlock>>>(
        cellCount, convection.rhs.data(), system.rhs.data());
    checkCuda(cudaGetLastError(), "addValuesKernel(rhs)");
    checkCuda(cudaDeviceSynchronize(), "addValuesKernel sync");
    auto& stats = gpuExecutionStats();
    stats.kernelLaunches += 2;
    stats.kernelSeconds += timer.elapsedSeconds();
  }

  applyScalarSourceDevice(cellVolumes, Su, Sp, system);
  applyTransientDevice(cellVolumes, time, system);
}

}  // namespace cfd::gpu
