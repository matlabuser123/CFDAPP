#pragma once

#include "cfd/algebra/LinearSolver.hpp"
#include "cfd/boundary/BoundaryCondition.hpp"
#include "cfd/core/Types.hpp"
#include "cfd/mesh/Mesh.hpp"
#include "cfd/physics/FluidProperties.hpp"
#include "cfd/solver/TransientSolver.hpp"

namespace cfd::pressure_velocity {

// Every numerical control PISO's linear solves need. Deliberately *not*
// shaped like SIMPLESettings: PISO has no outer iteration of its own (no
// maxIterations, no relaxation, no convergence tolerances to check) --
// its "convergence" per time step is exactly two pressure corrections,
// always (TODO.md P2 -- PISO-H notes: "PISO is not a steady SIMPLE outer
// loop"). Each LinearSolverSettings is validated by BiCGSTAB's own
// constructor (LinearSolver's base-class validation), so no separate
// validate function is needed here.
struct PISOSettings {
  cfd::algebra::LinearSolverSettings momentumSolver;
  cfd::algebra::LinearSolverSettings pressureSolver;
};

// One complete PISO time step: transient momentum predictor -> predictor
// flux -> pressure correction #1 (assemble + solve + apply) -> pressure
// correction #2 (assemble from F1, not F* + solve + apply) -> final
// continuity -> return. Composes PISO-B through PISO-G's already-
// independently-verified building blocks
// ([TransientMomentum.hpp](TransientMomentum.hpp),
// [PressureCorrectionEquation.hpp](PressureCorrectionEquation.hpp),
// [ContinuityEquation.hpp](../physics/ContinuityEquation.hpp)) unmodified
// -- this class introduces no new numerical formulas.
//
// Implements TransientStepSolver so TransientSolver can drive it without
// knowing anything CFD-specific. PISO itself never advances physical
// time and never decides whether a step is accepted -- that remains
// TransientSolver's job entirely (TODO.md P2 sections 14-15): a failed
// step here just reports a status; PISO does not retry, does not adjust
// dt, and does not touch previousState.
class PISO final : public cfd::solver::TransientStepSolver {
 public:
  // mesh/fluid/velocityBoundaries/pressureBoundaries are not owned; the
  // caller must keep them alive for the lifetime of this object (mirrors
  // TransientSolver's own stepSolver_ reference-member convention).
  // referenceCell is the pressure-correction null-space gauge, fixed at
  // construction so it never moves between time steps or between
  // repeated calls for the same problem (determinism). Both corrections
  // within one time step reuse this same referenceCell.
  PISO(const cfd::mesh::Mesh& mesh, const cfd::physics::FluidProperties& fluid,
       const cfd::boundary::BoundaryConditionSet& velocityBoundaries,
       const cfd::boundary::BoundaryConditionSet& pressureBoundaries, PISOSettings settings,
       Index referenceCell = 0);

  // On success (status == Converged): result.state holds {U2, p2, F2} --
  // the fully corrected velocity/pressure/authoritative face flux after
  // both pressure corrections. result.continuityResidual is
  // maxCellImbalance and result.massImbalance is |globalNetFlux|, both
  // evaluated from F2 (never F1 or the predictor flux) via
  // evaluateContinuity -- the same fields TODO.md P2's ContinuityResult
  // already defines, not a PISO-specific metric. result.maxCFL is the
  // *pre-step* Courant number from previousState.massFlux and dt (before
  // this step's momentum predictor runs at all).
  //
  // On any non-Converged status, result.state is set to a copy of
  // previousState (unchanged) -- a deterministic, well-defined value
  // rather than a default-constructed empty state, even though
  // TransientSolver discards result.state whenever status != Converged.
  // Distinguishes: InvalidConfiguration (bad settings, mismatched
  // previousState size, referenceCell out of range, or dt not finite/
  // positive); NonFiniteState (a non-finite value detected after the
  // predictor, after either pressure-correction solve, or after either
  // correction's application -- checked at each stage, not only once at
  // the end); MomentumFailure (either component's linear solve did not
  // converge); PressureCorrectionFailure (either pressure-correction
  // solve did not converge).
  [[nodiscard]] cfd::solver::TransientStepResult solveTimeStep(
      const cfd::solver::TransientState& previousState, Real dt) const override;

  [[nodiscard]] const PISOSettings& settings() const noexcept;
  [[nodiscard]] Index referenceCell() const noexcept;

 private:
  const cfd::mesh::Mesh& mesh_;
  const cfd::physics::FluidProperties& fluid_;
  const cfd::boundary::BoundaryConditionSet& velocityBoundaries_;
  const cfd::boundary::BoundaryConditionSet& pressureBoundaries_;
  PISOSettings settings_;
  Index referenceCell_;
};

}  // namespace cfd::pressure_velocity
