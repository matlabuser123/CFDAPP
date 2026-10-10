#pragma once

// P16-GPU-TURB Stage 6 -- one k-epsilon correction, composed on the device.
//
// CUDA-only. See DeviceBuffer.hpp's "never include from a CPU-only-compiled
// file" rule.
//
// THE PRODUCTION ORDER, audited from KEpsilonModel::correct and reproduced here
// operation for operation. Every numbered step reads the PRE-UPDATE state:
//
//    1  velocity gradient            (GreenGauss)
//    2  S^2                          from that gradient
//    3  P_k = mu_t_OLD * S^2         <-- the OLD mu_t, not a fresh one
//    4  wall treatment               y+, u_tau, wall shear, wall cells
//    5  P_k[wallCell] = equilibrium  <-- REPLACEMENT, not an addition
//    6  Gamma_k, Gamma_eps           from mu_t_OLD
//    7  k sources                    from k_OLD, epsilon_OLD, modified P_k
//    8  epsilon sources              from k_OLD, epsilon_OLD, modified P_k
//    9  k:       assemble -> relax -> solve -> floor
//   10  epsilon: assemble -> relax -> fix imposed -> solve -> floor -> re-assert
//   11  mu_t_NEW = f(k_NEW, epsilon_NEW)      <-- the FRESH fields
//   12  publish k, epsilon, mu_t ATOMICALLY
//
// THE LAGGING IS THE WHOLE POINT OF STEP 12. Both equations are solved against
// the SAME old state -- the epsilon equation uses k_OLD, not the k that was
// just solved. Publishing k before assembling epsilon is the obvious
// "optimisation" and it is a different model. Nothing is written back to the
// state until both solves have succeeded, which also means a throwing solve
// leaves the state exactly as it was.
//
// A one-correction differential cannot see a publication-order defect, because
// with one correction there is no next iteration to be poisoned. That is what
// the multi-correction test exists for.
//
// WHAT IS NOT YET ON THE DEVICE, and is not claimed to be
// -------------------------------------------------------
//   * THE WALL TREATMENT. `computeWallTreatment` is a 270-line host function
//     over WALL FACES -- an O(sqrt(N)) set, negligible against the transport
//     assembly -- and its corner rule ("a cell touching two wall faces appears
//     ONCE, carrying the value from its NEAREST wall face") is sequential by
//     construction. It is evaluated on the host and its small outputs are
//     uploaded. The trace therefore records WallTreatment = Cpu, and
//     `allOnGpu` is FALSE. That is the honest state, not a gap being papered
//     over.
//
//   * THE LINEAR SOLVES. `makeGpuBiCGSTAB` takes a HOST LinearSystem and
//     uploads internally, so feeding it a device-assembled CSR would cost a
//     D2H of the matrix and an H2D straight back -- destroying the residency
//     this assembly exists to provide. KSolve/EpsilonSolve therefore record
//     Cpu until a device-resident scalar solve exists, the way
//     GpuResidentSolve provides one for pressure.
//
// Both are named in the Stage 6 evidence with their cost, because a caller
// reading `allOnGpu() == false` deserves to know which two operations and why.

#include <vector>

#include "cfd/algebra/LinearSolver.hpp"
#include "cfd/core/Types.hpp"
#include "cfd/gpu/DeviceBuffer.hpp"
#include "cfd/gpu/DeviceCsrMatrix.hpp"
#include "cfd/gpu/DeviceScalarTransport.hpp"
#include "cfd/gpu/DeviceTurbulence.hpp"
#include "cfd/gpu/GpuResidentSolve.hpp"

namespace cfd::gpu {

// The turbulence state carried across corrections. These three fields are the
// model's own members (k_, epsilon_, turbulentViscosity_) and nothing else:
// S^2, P_k and the source arrays are per-correction temporaries and are
// deliberately NOT state, so a stale one cannot survive into the next
// correction.
struct DeviceKEpsilonState {
  DeviceBuffer<cfd::Real> k;
  DeviceBuffer<cfd::Real> epsilon;
  DeviceBuffer<cfd::Real> turbulentViscosity;

  // Persistent Krylov scratch, one per equation, owned by the STATE rather
  // than created per correction. That is what makes "zero steady-state
  // allocations" hold: GpuKrylovWorkspace::resize() is a no-op once the
  // vectors are the right length, so correction 2 onwards allocates nothing.
  //
  // Two workspaces, not one shared: k and epsilon are solved in sequence and
  // could share, but a shared workspace would make the epsilon solve's initial
  // guess depend on the k solve's leftovers, which is a coupling the CPU path
  // does not have.
  GpuKrylovWorkspace kWorkspace;
  GpuKrylovWorkspace epsilonWorkspace;
};

// The wall treatment's outputs for one correction, uploaded from the host
// evaluation. Empty when wall functions are off, which must leave the
// correction byte-identical to a no-wall run.
struct DeviceWallTreatment {
  DeviceBuffer<cfd::Index> wallCells;
  DeviceBuffer<cfd::Real> wallCellProduction;
  DeviceImposedCells imposedEpsilon;

  [[nodiscard]] bool active() const noexcept { return !wallCells.empty(); }
};

struct DeviceKEpsilonConfig {
  cfd::Real density{};
  cfd::Real dynamicViscosity{};
  cfd::Real cMu{0.09};
  cfd::Real c1Epsilon{1.44};
  cfd::Real c2Epsilon{1.92};
  cfd::Real sigmaK{1.0};
  cfd::Real sigmaEpsilon{1.3};
  cfd::Real kFloor{1e-10};
  cfd::Real epsilonFloor{1e-10};
  cfd::Real kRelaxation{0.7};
  cfd::Real epsilonRelaxation{0.7};
  cfd::algebra::LinearSolverSettings kSolver{};
  cfd::algebra::LinearSolverSettings epsilonSolver{};
  bool nonOrthogonal{false};
};

// Per-equation solver diagnostics, so the production differential can compare
// iteration counts and residuals and not only fields.
struct DeviceKEpsilonSolveReport {
  bool converged{false};
  cfd::Index iterations{};
  cfd::Real residual{};
};

// Per-correction diagnostics, so a caller can compare more than the fields.
struct DeviceKEpsilonCorrection {
  DeviceBuffer<cfd::Real> strainRateSquared;
  DeviceBuffer<cfd::Real> production;  // AFTER the wall replacement
  cfd::Real convergenceResidual{};     // max|new - old| over k and epsilon
  DeviceKEpsilonSolveReport kSolve{};
  DeviceKEpsilonSolveReport epsilonSolve{};

  // P16 Step 4 -- PERSISTENT WORK BUFFERS.
  //
  // These were locals in correctKEpsilonDevice, constructed and destroyed on
  // every correction. That cost 17 device allocations per outer iteration, and
  // it went unnoticed until Step 4 produced a real production turbulent
  // iteration to measure: the ledger read 30 allocations per steady-state
  // iteration against a contract of zero.
  //
  // Held here rather than made static, so they follow the lifetime of the
  // correction that owns them and two concurrent corrections cannot share one
  // scratch buffer. `resize` on an unchanged mesh is a no-op, so steady state
  // allocates nothing while a mesh change still resizes exactly once.
  DeviceBuffer<cfd::Real> gammaK, gammaEpsilon;
  DeviceBuffer<cfd::Real> suK, spK, suEpsilon, spEpsilon;
  DeviceScalarTransportSystem kSystem, epsilonSystem;
  // The solve outputs. Published by SWAPPING with `state` rather than moving
  // into it: a move would leave these empty and force a fresh allocation next
  // correction, which is the very cost this exists to remove. After the swap
  // they hold the previous iteration's fields, already the right size.
  DeviceBuffer<cfd::Real> solvedK, solvedEpsilon, freshMuT;
};

// Applies P_k[wallCell] = wallCellProduction, in place.
//
// REPLACEMENT, not accumulation. The equilibrium expression stands in for
// mu_t*S^2 in a cell that spans the log layer; adding to it would double-count
// production exactly where production peaks.
void applyWallProductionDevice(const DeviceWallTreatment& wall,
                               DeviceBuffer<cfd::Real>& production);

// A DEVICE-RESIDENT scalar solve for a system DeviceScalarTransport produced.
//
// STAGE 7B IS A PROMOTION, NOT A NEW SOLVER. `solveBiCGSTABResident` already
// exists (GPU-PIPE-001) and already adopts a device-resident A, b and x0 while
// leaving x on the device; `DeviceCsrMatrix::adoptDevice` already wraps
// caller-owned device buffers without copying them. All this does is point the
// one at the other. There is still exactly one copy of the BiCGSTAB iteration
// in the codebase, one set of breakdown tests and one convergence test -- so a
// turbulence solve cannot drift numerically from a pressure solve.
//
// The CSR is never downloaded. What returns to the host is what the resident
// pressure path already returns: the converged flag, iteration count and
// residual, read in one 8-byte counter fetch plus the SolverResult scalars.
[[nodiscard]] cfd::algebra::SolverResult solveScalarTransportResident(
    const cfd::algebra::LinearSolverSettings& settings, const DeviceScalarTransportSystem& system,
    cfd::Index cellCount, const DeviceBuffer<cfd::Real>& guess, GpuKrylovWorkspace& workspace,
    DeviceBuffer<cfd::Real>& solution);

// P16's execution requirement, which is NOT allOnGpu().
//
// The high-Re wall treatment stays on the host in P16 by an explicit, recorded
// scope decision: it touches only O(sqrt(N)) wall faces, carries sequential
// corner semantics, and is already validated, so a CUDA port would add
// substantial complexity for negligible expected gain. This predicate
// therefore requires the seven operations the GPU path is meant to perform and
// deliberately says nothing about WallTreatment.
//
// It is a DIFFERENT question from allOnGpu(), not a relaxation of it --
// allOnGpu() still exists, still means what it says, and still returns false.
// A caller wanting "everything on the device" must keep asking allOnGpu().
[[nodiscard]] bool turbulenceGpuPathQualified(const TurbulenceBackendTrace& trace) noexcept;

// One complete correction, in the production order above.
//
// `state` is read AND written: on success all three fields are replaced
// atomically at the end. If anything throws, `state` is untouched -- the same
// guarantee KEpsilonModel documents for itself.
//
// Both solves are DEVICE RESIDENT (Stage 7B). The assembled CSR is adopted in
// place and never downloaded; `config.kSolver`/`epsilonSolver` drive the same
// BiCGSTAB the pressure stage uses.
// THE TWO EQUATIONS NEED THEIR OWN PLANS.
//
// k and epsilon have DIFFERENT boundary condition sets -- KEpsilonModel holds
// kBoundaries_ and epsilonBoundaries_ separately -- and a DeviceDiffusionPlan
// bakes the encoded boundary values in at build() time. Assembling epsilon
// with k's plans applies k's inlet value to epsilon, which produces a fully
// converged, entirely plausible, wrong epsilon field.
//
// This is not hypothetical: the first version of this function took ONE plan
// pair and the Stage 6 integration test caught it immediately -- k matched the
// production model to 0 ulp while epsilon was out by 21%. Every isolated
// Stage 4/5 gate had passed, because each built its own plans from the right
// boundary set. Only the composition could see it.
struct DeviceKEpsilonPlans {
  const DeviceDiffusionPlan* kDiffusion{nullptr};
  const DeviceConvectionPlan* kConvection{nullptr};
  const DeviceDiffusionPlan* epsilonDiffusion{nullptr};
  const DeviceConvectionPlan* epsilonConvection{nullptr};
};

void correctKEpsilonDevice(const DeviceKEpsilonPlans& plans,
                           const DeviceBuffer<cfd::Real>& cellVolumes,
                           const DeviceVelocityGradient& velocityGradient,
                           const DeviceBuffer<cfd::Real>& massFlux, const DeviceWallTreatment& wall,
                           const DeviceKEpsilonConfig& config, DeviceKEpsilonState& state,
                           DeviceKEpsilonCorrection& correction);

}  // namespace cfd::gpu
