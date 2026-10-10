// P16-GPU-TURB Stage 6 -- composing one k-epsilon correction on the device.
//
// See DeviceKEpsilon.hpp for the audited production order this reproduces, and
// for the two operations that are NOT on the device yet (the wall treatment and
// the linear solves) with the reason for each.
//
// This is a .cpp, not a .cu: every kernel it needs already exists. Its whole
// job is ORDER, and order is the thing a one-correction differential cannot
// fully verify -- see the multi-correction test.

#include "cfd/gpu/DeviceKEpsilon.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <vector>

#include "cfd/algebra/LinearSystem.hpp"
#include "cfd/algebra/SparseMatrix.hpp"
#include "cfd/algebra/Vector.hpp"
#include "cfd/fields/ScalarField.hpp"
#include "cfd/gpu/DeviceVectorOps.hpp"

namespace cfd::gpu {

using cfd::Index;
using cfd::Real;

cfd::algebra::SolverResult solveScalarTransportResident(
    const cfd::algebra::LinearSolverSettings& settings, const DeviceScalarTransportSystem& system,
    Index cellCount, const DeviceBuffer<Real>& guess, GpuKrylovWorkspace& workspace,
    DeviceBuffer<Real>& solution) {
  workspace.resize(cellCount);

  // Adopt the assembly's own buffers. Nothing is copied and nothing is freed
  // here -- the system still owns them, which is exactly why the matrix never
  // reaches the host.
  DeviceCsrMatrix matrix;
  matrix.adoptDevice(system.rowOffsets.data(), system.columnIndices.data(), system.values.data(),
                     cellCount, cellCount, system.values.size());

  copyToWorkspaceRhs(system.rhs, workspace);
  copyToWorkspaceGuess(guess, workspace);

  const bool wantJacobi = settings.preconditioner == cfd::algebra::PreconditionerType::Jacobi;
  const ResidentSystemCheck check = checkSystemAndBuildJacobi(matrix, workspace, wantJacobi);
  if (!check.inputsFinite) {
    throw std::runtime_error("solveScalarTransportResident: assembled system is non-finite");
  }
  if (wantJacobi && !check.diagonalUsable) {
    throw std::runtime_error("solveScalarTransportResident: Jacobi diagonal is unusable");
  }

  const cfd::algebra::SolverResult result = solveBiCGSTABResident(settings, matrix, workspace);
  copyWorkspaceSolution(workspace, cellCount, solution);
  return result;
}

bool turbulenceGpuPathQualified(const TurbulenceBackendTrace& trace) noexcept {
  // The seven the GPU path performs. WallTreatment is deliberately absent --
  // see the header, and results/p16/turbulence/gpu_equivalence/.
  const TurbulenceOperation required[] = {TurbulenceOperation::StrainRate,
                                          TurbulenceOperation::Production,
                                          TurbulenceOperation::TurbulentViscosity,
                                          TurbulenceOperation::KAssembly,
                                          TurbulenceOperation::KSolve,
                                          TurbulenceOperation::EpsilonAssembly,
                                          TurbulenceOperation::EpsilonSolve};
  return trace.allOnGpu(required, 7);
}

void applyWallProductionDevice(const DeviceWallTreatment& wall, DeviceBuffer<Real>& production) {
  if (!wall.active()) return;
  const Index count = wall.wallCells.size();
  if (wall.wallCellProduction.size() != count) {
    throw std::runtime_error(
        "applyWallProductionDevice: wall cell and production lists differ in length");
  }
  // Reuses the imposed-value kernel: "write these values at these indices" is
  // exactly the operation, and the wall cells are unique by construction
  // (computeWallTreatment keeps one slot per cell).
  //
  // STRAIGHT FROM THE RESIDENT BUFFERS. This used to download both lists and
  // re-upload them through uploadImposedCellsDevice, which was a device -> host
  // -> device round trip of data no host code reads -- 16 bytes per wall cell
  // in each direction, every correction. The kernel only ever needed the two
  // device pointers, which `wall` already holds. See DeviceTurbulence.hpp for
  // why skipping uploadImposedCellsDevice's validation is sound for THIS list.
  reassertImposedValuesDevice(wall.wallCells, wall.wallCellProduction, production);
}

void correctKEpsilonDevice(const DeviceKEpsilonPlans& plans, const DeviceBuffer<Real>& cellVolumes,
                           const DeviceVelocityGradient& velocityGradient,
                           const DeviceBuffer<Real>& massFlux, const DeviceWallTreatment& wall,
                           const DeviceKEpsilonConfig& config, DeviceKEpsilonState& state,
                           DeviceKEpsilonCorrection& correction) {
  const Index n = state.k.size();
  if (n == 0) return;
  if (plans.kDiffusion == nullptr || plans.kConvection == nullptr ||
      plans.epsilonDiffusion == nullptr || plans.epsilonConvection == nullptr) {
    throw std::runtime_error(
        "correctKEpsilonDevice: k and epsilon each need their own diffusion and convection plan");
  }
  if (state.epsilon.size() != n || state.turbulentViscosity.size() != n) {
    throw std::runtime_error("correctKEpsilonDevice: k, epsilon and mu_t must be the same length");
  }

  // --- 2, 3: S^2 then P_k, from the OLD mu_t -------------------------------
  computeStrainRateMagnitudeSquaredDevice(velocityGradient, n, correction.strainRateSquared);
  computeTurbulentProductionDevice(state.turbulentViscosity, correction.strainRateSquared,
                                   correction.production);

  // --- 5: the wall replacement, BEFORE any source is built -----------------
  applyWallProductionDevice(wall, correction.production);

  // --- 6: the diffusivities, from the OLD mu_t -----------------------------
  // P16 Step 4: the correction's own persistent workspace, not locals. See
  // DeviceKEpsilon.hpp for why.
  DeviceBuffer<Real>& gammaK = correction.gammaK;
  DeviceBuffer<Real>& gammaEpsilon = correction.gammaEpsilon;
  computeEffectiveDiffusivityDevice(config.dynamicViscosity, state.turbulentViscosity,
                                    config.sigmaK, gammaK);
  computeEffectiveDiffusivityDevice(config.dynamicViscosity, state.turbulentViscosity,
                                    config.sigmaEpsilon, gammaEpsilon);

  // --- 7, 8: BOTH source pairs from the OLD k and epsilon ------------------
  //
  // Built here, before either solve, because both equations are lagged against
  // the same state. Building epsilon's sources after solving k would feed it
  // k_NEW and quietly change the model.
  DeviceBuffer<Real>& suK = correction.suK;
  DeviceBuffer<Real>& spK = correction.spK;
  computeKSourceTermsDevice(config.density, state.k, state.epsilon, correction.production,
                            config.kFloor, suK, spK);
  DeviceBuffer<Real>& suEpsilon = correction.suEpsilon;
  DeviceBuffer<Real>& spEpsilon = correction.spEpsilon;
  computeEpsilonSourceTermsDevice(config.density, config.c1Epsilon, config.c2Epsilon, state.k,
                                  state.epsilon, correction.production, config.kFloor, suEpsilon,
                                  spEpsilon);

  const DeviceTransportTimeState steady;

  // --- 9: k -- assemble, relax, solve, floor. NO imposed rows. -------------
  DeviceScalarTransportSystem& kSystem = correction.kSystem;
  assembleScalarTransportDevice(*plans.kDiffusion, *plans.kConvection, cellVolumes, state.k,
                                massFlux, gammaK, suK, spK, config.nonOrthogonal, steady, kSystem);
  applyImplicitUnderRelaxationDevice(state.k, config.kRelaxation, kSystem);
  turbulenceBackendTrace().record(TurbulenceOperation::KAssembly, ExecutionBackend::Gpu);

  DeviceBuffer<Real>& deviceK = correction.solvedK;
  const auto kResult =
      solveScalarTransportResident(config.kSolver, kSystem, n, state.k, state.kWorkspace, deviceK);
  turbulenceBackendTrace().record(TurbulenceOperation::KSolve, ExecutionBackend::Gpu);
  if (!kResult.converged()) {
    throw std::runtime_error("correctKEpsilonDevice: the k solve did not converge");
  }
  correction.kSolve = {kResult.converged(), kResult.iterations, kResult.finalResidual};
  applyScalarFloorDevice(config.kFloor, deviceK);

  // --- 10: epsilon -- assemble, relax, FIX IMPOSED, solve, floor, re-assert -
  DeviceScalarTransportSystem& epsilonSystem = correction.epsilonSystem;
  assembleScalarTransportDevice(*plans.epsilonDiffusion, *plans.epsilonConvection, cellVolumes,
                                state.epsilon, massFlux, gammaEpsilon, suEpsilon, spEpsilon,
                                config.nonOrthogonal, steady, epsilonSystem);
  applyImplicitUnderRelaxationDevice(state.epsilon, config.epsilonRelaxation, epsilonSystem);
  fixImposedRowsDevice(wall.imposedEpsilon, epsilonSystem);
  turbulenceBackendTrace().record(TurbulenceOperation::EpsilonAssembly, ExecutionBackend::Gpu);

  DeviceBuffer<Real>& deviceEpsilon = correction.solvedEpsilon;
  const auto epsilonResult = solveScalarTransportResident(
      config.epsilonSolver, epsilonSystem, n, state.epsilon, state.epsilonWorkspace, deviceEpsilon);
  turbulenceBackendTrace().record(TurbulenceOperation::EpsilonSolve, ExecutionBackend::Gpu);
  if (!epsilonResult.converged()) {
    throw std::runtime_error("correctKEpsilonDevice: the epsilon solve did not converge");
  }
  correction.epsilonSolve = {epsilonResult.converged(), epsilonResult.iterations,
                             epsilonResult.finalResidual};
  applyScalarFloorDevice(config.epsilonFloor, deviceEpsilon);
  // AFTER the floor, never before -- a wall epsilon below the floor is legal
  // and the floor must not move it.
  reassertImposedValuesDevice(wall.imposedEpsilon, deviceEpsilon);

  // --- 11: mu_t from the FRESH k and epsilon -------------------------------
  DeviceBuffer<Real>& newMuT = correction.freshMuT;
  computeTurbulentViscosityDevice(config.density, config.cMu, deviceK, deviceEpsilon, config.kFloor,
                                  config.epsilonFloor, newMuT);

  // --- 12: the residual, then publication ----------------------------------
  //
  // ON DEVICE. This used to download kOld, epsilonOld, kNew and epsilonNew in
  // full -- 4 * 8 * N bytes per correction, more than the publication below --
  // to run KEpsilonModel's host loop and produce ONE scalar. Only the two
  // scalars cross now.
  //
  // `state.k` and `state.epsilon` are still the OLD fields here: nothing has
  // been published yet, which is exactly the "before either member is
  // overwritten" ordering KEpsilonModel.cpp documents for this metric.
  //
  // max over k and max over epsilon, then the larger, is the same number as
  // the host's single interleaved loop: max is associative and commutative,
  // and neither partial result can be NaN (see maxAbsDifference2).
  Real maxKChange = 0.0;
  Real maxEpsilonChange = 0.0;
  maxAbsDifference2(deviceK, state.k, deviceEpsilon, state.epsilon, maxKChange,
                    maxEpsilonChange);
  correction.convergenceResidual =
      (maxKChange < maxEpsilonChange) ? maxEpsilonChange : maxKChange;

  // Nothing above wrote to `state`. Everything lands here, together, only once
  // both solves have succeeded -- so a throwing solve leaves the caller's state
  // exactly as it was, matching KEpsilonModel's own documented guarantee.
  // SWAPPED, not moved. A move would leave the workspace buffers empty and
  // force a fresh allocation on the next correction -- the cost this phase
  // exists to remove. After the swap the workspace holds the previous
  // iteration's fields, already exactly the right size, so the next
  // correction's resize() is a no-op.
  //
  // Still atomic and still last: all three land together, only once both
  // solves have succeeded, exactly as before.
  std::swap(state.k, deviceK);
  std::swap(state.epsilon, deviceEpsilon);
  std::swap(state.turbulentViscosity, newMuT);
}

}  // namespace cfd::gpu
