#pragma once

// P14 Phase I -- CUDA-only. See DeviceBuffer.hpp's "never include from a
// CPU-only-compiled file" rule.
//
// Device assembly of the generic scalar transport equation.
//
// WHAT THIS ADDS, AND WHAT IT REUSES
// -----------------------------------
// The Phase A audit found 0 of 18 device kernels touching scalar transport, so
// Phase I was expected to be new work. Reading the GPU layer more closely
// showed the same shape as the CPU side: the two hard halves ALREADY EXIST as
// qualified ports of the production assemblers.
//
//   assembleScalarDiffusionDevice   ports transport::assembleScalarDiffusionContribution
//   assembleScalarConvectionDevice  ports thermal::assembleThermalConvectionContribution
//
// Those are exactly the two functions cfd::transport::assembleScalarTransportEquation
// composes on the host. So this file adds only what genuinely did not exist:
//
//   * the Patankar Su/Sp source contribution, on device;
//   * the conservative implicit-Euler transient contribution, on device;
//   * the composition of all four into ONE CSR system, on device.
//
// This is NOT "assemble on the host and upload", which Phase B10 rules out:
// every contribution below is computed by a kernel reading device-resident
// fields, and the assembled system never leaves the device.
//
// SPARSITY
// --------
// DeviceDiffusionSystem and DeviceConvectionSystem are both built on the
// CANDIDATE pattern -- row P holds column P plus every cell sharing an internal
// face with P, ascending -- so the two can be merged by adding values
// elementwise. That is checked at runtime rather than assumed:
// requireSamePattern() compares the row offsets and column indices and refuses
// if they differ,
// because a silent mismatch would add coefficients into the wrong columns and
// still produce a plausible-looking matrix.

#include <string>
#include <vector>

#include "cfd/core/Types.hpp"
#include "cfd/gpu/DeviceBuffer.hpp"
#include "cfd/gpu/DeviceConvection.hpp"
#include "cfd/gpu/DeviceDiffusion.hpp"

namespace cfd::gpu {

// The assembled transport system, device-resident, on the candidate pattern.
struct DeviceScalarTransportSystem {
  DeviceBuffer<cfd::Index> rowOffsets;     // size cellCount + 1
  DeviceBuffer<cfd::Index> columnIndices;  // size rowOffsets[cellCount]
  DeviceBuffer<cfd::Real> values;          // same size as columnIndices
  DeviceBuffer<cfd::Real> rhs;             // size cellCount

  // P16 Step 4 -- the assembly's own scratch, held here rather than built as
  // locals inside assembleScalarTransportDevice.
  //
  // They were four buffers each, allocated and freed on EVERY assembly: eight
  // device allocations per call, sixteen per turbulence correction, and the
  // largest single contributor to the 30-per-iteration figure the production
  // residency ledger measured.
  //
  // Carried by the SYSTEM rather than made function-local statics, so the k and
  // epsilon assemblies keep their own storage -- the same separation their
  // plans and Krylov workspaces already have, and for the same reason: two
  // equations sharing one scratch buffer is a defect waiting for someone to
  // reorder the calls.
  DeviceDiffusionSystem diffusionScratch;
  DeviceConvectionSystem convectionScratch;
};

// Old-time state for a device transient assembly. Mirrors
// cfd::transport::TransportTimeState, including its separation of the two
// density levels: the diagonal takes rho^{n+1}, the RHS takes rho^n phi^n.
// Keeping them separate on the device too means the non-conservative form
// cannot be written by passing one buffer where the other was meant.
struct DeviceTransportTimeState {
  bool transient{false};
  cfd::Real dt{0.0};
  const DeviceBuffer<cfd::Real>* phiOld{nullptr};
  const DeviceBuffer<cfd::Real>* rhoOld{nullptr};
  const DeviceBuffer<cfd::Real>* rhoNew{nullptr};
};

// Patankar source on device: a_P -= Sp_P V_P, b_P += Su_P V_P.
// `cellVolumes` is device-resident (DeviceMesh already holds it).
void applyScalarSourceDevice(const DeviceBuffer<cfd::Real>& cellVolumes,
                             const DeviceBuffer<cfd::Real>& Su, const DeviceBuffer<cfd::Real>& Sp,
                             DeviceScalarTransportSystem& system);

// Conservative implicit Euler on device. Adds nothing when `time.transient` is
// false -- no kernel is launched at all, so a steady device assembly cannot be
// perturbed by the transient path existing.
void applyTransientDevice(const DeviceBuffer<cfd::Real>& cellVolumes,
                          const DeviceTransportTimeState& time,
                          DeviceScalarTransportSystem& system);

// The whole equation, assembled on device in the same order as the host:
// diffusion, convection, source, transient.
//
// The order matters for the same reason it does on the host: floating-point
// addition is not associative.
//
// MEASURED agreement with the host (results/p14/transport-foundation/, probe
// tools/transport_equivalence.cpp, grids up to 40x30):
//
//   sparsity structure, RHS, off-diagonals          BITWISE (0 ulp)
//   a source-only or diffusion-only system          BITWISE (0 ulp)
//   diagonals where convection AND diffusion act    <= 2 ulp (max 1.4e-14)
//   solution relative L2                            <= 2.1e-12
//   the same device assembly twice                  bitwise identical
//
// The residual difference is ASSOCIATION ORDER, not FMA contraction -- this
// kernel is in cuda/CMakeLists.txt's -fmad=false list like every other
// qualified kernel. The host accumulates ((d1+d2+..)+c1)+c2+.. into one
// builder; the device adds each term's completed total. Off-diagonals take one
// contribution per term, which is exactly why they are bitwise and diagonals
// are not. Making them bitwise would need convection fused into the diffusion
// kernel face-by-face, which is a larger change than 2 ulp justifies.
//
// Throws std::runtime_error if the diffusion and convection systems do not
// share a sparsity pattern (see the SPARSITY note above).
void assembleScalarTransportDevice(
    const DeviceDiffusionPlan& diffusionPlan, const DeviceConvectionPlan& convectionPlan,
    const DeviceBuffer<cfd::Real>& cellVolumes, const DeviceBuffer<cfd::Real>& phi,
    const DeviceBuffer<cfd::Real>& massFlux, const DeviceBuffer<cfd::Real>& diffusivity,
    const DeviceBuffer<cfd::Real>& Su, const DeviceBuffer<cfd::Real>& Sp, bool nonOrthogonalEnabled,
    const DeviceTransportTimeState& time, DeviceScalarTransportSystem& system);

// ---------------------------------------------------------------------------
// P16-GPU-TURB -- the two discrete semantics the CPU turbulence path relies on
// and this device assembly did not have.
//
// THE ORDER IS LOAD-BEARING, and it is the CPU order
// (cfd::turbulence::solveRelaxedScalarTransport):
//
//     assemble  ->  under-relax  ->  fix imposed rows  ->  solve
//
// Relaxation before imposition, never after: fixRow discards everything in the
// row, so a relaxation applied afterwards would put a 1/alpha on a row whose
// only legitimate coefficient is a unit diagonal, and the imposed value would
// stop being imposed. Both halves of an imposed row -- the matrix row AND the
// right-hand side -- are therefore settled last, and the RHS is OVERWRITTEN
// rather than accumulated, exactly as on the host.
//
// AND THE ORDER DOES NOT END AT THE SOLVE. solveRelaxedScalarTransport applies
// two further steps to the SOLVED FIELD -- the floor, then a re-assertion of
// every imposed value -- and the second is not cosmetic: an iterative solver
// only APPROACHES a fixed row. Measured at 1.5e-10 on all three imposed cells
// by tests/unit/gpu/test_device_scalar_transport_semantics.cpp's
// ReplicaReproducesTheProductionSolve. Fixing the row is therefore necessary
// and NOT sufficient, and a device k-epsilon integration owes the same
// re-assertion after its own solve, or its wall cells drift off the wall
// function that put them there.
// ---------------------------------------------------------------------------

// Patankar implicit under-relaxation, the device port of
// cfd::pressure_velocity::applyImplicitUnderRelaxation:
//
//     a_P^relaxed = a_P / alpha
//     b_P^relaxed = b_P + ((1 - alpha) / alpha) * a_P * phi_P^old
//
// where a_P is the PRE-RELAXATION diagonal in BOTH expressions. Reusing the
// already-relaxed diagonal in the right-hand side correction is the obvious
// bug here and it is a quiet one: it still converges, to a different answer,
// with an alpha-dependent error that looks like a physics difference. The
// kernel reads values[k] into `extra` before writing values[k] back, which is
// what makes the pre-relaxation reading structural rather than a convention a
// later edit could lose.
//
// The host arithmetic is reproduced term for term -- `factor = 1/alpha - 1`,
// `extra = a_P * factor`, `a_P += extra`, `b_P += extra * phi_P^old` -- so that
// identical inputs give bitwise identical output rather than
// algebraically-equal output.
//
// alpha == 1 launches NO kernel at all (the host returns early for the same
// reason), so an unrelaxed device assembly cannot be perturbed by this path
// merely existing. Throws std::runtime_error if alpha is not finite and in
// (0, 1], or if phiOld does not have one entry per row.
void applyImplicitUnderRelaxationDevice(const DeviceBuffer<cfd::Real>& phiOld, cfd::Real alpha,
                                        DeviceScalarTransportSystem& system);

// A device-resident imposed-cell list: the wall cells whose row is replaced by
// `phi_cell = value`. Built once on the host, where the range/duplicate checks
// are cheap, then reused across assemblies.
struct DeviceImposedCells {
  DeviceBuffer<cfd::Index> cells;
  DeviceBuffer<cfd::Real> values;

  [[nodiscard]] cfd::Index size() const noexcept { return cells.size(); }
  [[nodiscard]] bool empty() const noexcept { return cells.empty(); }
};

// Validates and uploads an imposed-cell list.
//
// DUPLICATE CELL INDICES ARE REFUSED rather than resolved. On the host
// fixRow() is idempotent and the RHS assignment is last-wins, both well defined
// because the loop is sequential; on the device two threads would write the
// same row concurrently and a list carrying two different values for one cell
// would produce whichever the scheduler ran last. The producers cannot emit
// duplicates anyway -- computeWallTreatment keeps one slot per wall cell even
// when several wall faces share it -- so this refuses an input that is already
// impossible instead of inventing a tie-break the host does not have.
//
// Throws std::runtime_error on an out-of-range cell, a non-finite value, a
// duplicate cell, or mismatched list lengths.
void uploadImposedCellsDevice(const std::vector<cfd::Index>& cells,
                              const std::vector<cfd::Real>& values, cfd::Index cellCount,
                              DeviceImposedCells& imposed);

// The device port of SparseMatrixBuilder::fixRow plus the host's
// `rhs[cell] = value`: every coefficient in the row is discarded, the diagonal
// becomes 1, and the right-hand side is overwritten with the imposed value.
//
// The host builder drops exact zeros at build() time, so a fixed row there ends
// up holding literally one entry. The device keeps the candidate pattern, so
// the same row holds a 1 on the diagonal and an explicit 0 everywhere else.
// Those are the same matrix, and an equivalence check has to compare them in
// BOTH directions to say so honestly: every host entry must be found on the
// device with an equal value, AND every device entry with no host counterpart
// must be exactly zero. Checking only the first direction would accept a device
// row that kept its assembled off-diagonals, which is precisely the defect this
// function exists to prevent.
//
// Fails closed if a row has no diagonal slot in the candidate pattern: without
// one there is nowhere to put the 1, and the row would silently become
// all-zero -- a singular matrix that a solver reports as a convergence failure
// somewhere else entirely. Throws std::runtime_error in that case.
void fixImposedRowsDevice(const DeviceImposedCells& imposed, DeviceScalarTransportSystem& system);

}  // namespace cfd::gpu
