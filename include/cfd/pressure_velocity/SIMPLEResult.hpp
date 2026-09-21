#pragma once

#include <optional>
#include <string>
#include <vector>

#include "cfd/core/Types.hpp"
#include "cfd/fields/ScalarField.hpp"
#include "cfd/fields/SurfaceField.hpp"
#include "cfd/fields/VectorField.hpp"
#include "cfd/pressure_velocity/SIMPLESettings.hpp"
#include "cfd/solver/SolverRobustness.hpp"

namespace cfd::pressure_velocity {

// TODO.md P0 -- SIMPLE section 7: evidence, not just a converged-or-not
// flag. A failure category is never masked as MaxIterations (TODO.md
// section 74: no secret fallbacks) -- a failed inner linear solve is
// always its own distinct status, never silently absorbed into an outer
// "didn't converge in time" result.
enum class SIMPLEStatus {
  Converged,
  MaxIterations,
  MomentumFailure,
  PressureCorrectionFailure,
  NonFiniteState,
  InvalidConfiguration,
  // P5-B -- GUI Solver Workflow, section 13: a solve stopped early by an
  // external cancellation request (see SIMPLE's own optional
  // SIMPLECancellationCheck constructor parameter), checked only at the
  // top of an outer iteration -- never mid-iteration -- so the fields
  // returned alongside this status are always the last *complete*
  // converged-or-not outer iterate, never a partially-updated one. Never
  // produced by any pre-P5 call site (the cancellation check defaults to
  // null, i.e. "never cancelled"), so this is purely additive: every
  // existing exhaustive switch over SIMPLEStatus needed a new case added
  // once this value existed, but no prior *behavior* changed.
  Cancelled,
  // P12-NUM-004 (appended, so every existing value keeps its number):
  // produced only when the corresponding detector is enabled in
  // SIMPLESettings::robustness (default off). Stagnated: the residuals
  // plateaued materially above convergence (no material improvement over
  // the stagnation window) -- stopped before maxIterations instead of being
  // reported as MaxIterations. Diverging: a finite but clearly diverging
  // residual history (see cfd::solver::OuterIterationMonitor for both exact
  // criteria). SIMPLEResult::robustness.statusDetail says why.
  Stagnated,
  Diverging,
};

// residualHistory[k] (all four histories, plus continuityHistory) is the
// value *after* completed SIMPLE iteration k+1 -- history.size() ==
// iterations for a normal run (TODO.md section 51).
//
// U/V residuals are each momentum linear solve's *initial* residual
// (||b - A x0||), not its final one (TODO.md section 36-37: "be
// explicit... do not invent a second incompatible residual
// definition"). SIMPLE.cpp warm-starts each solve from the previous
// SIMPLE iterate, so this measures how far that previous velocity is
// from satisfying the newly-reassembled equation -- which genuinely
// shrinks as the outer iteration approaches a steady state. The linear
// solve's own *final* residual is unusable for this: it is small by
// construction on every successful inner solve (that is what "the
// linear solve converged" means) regardless of outer-loop progress, and
// using it would falsely report SIMPLE convergence after a single
// iteration. The P residual applies the same "before this iteration's
// solve" principle to pressure correction's own reset convention (p'
// starts from 0 every outer iteration -- section 17), so it is
// ||b_p|| -- not a standalone physical pressure equation's residual
// (section 38). Continuity is computed from the corrected (not
// predictor) face flux (section 39).
//
// By default all four gates plus globalMassImbalance use plain absolute
// tolerances against SIMPLESettings (the P0 rule). P12-NUM-004: with
// SIMPLESettings::robustness.convergenceCriterion == Normalized the u/v/p
// gates are judged against their normalization references instead
// (continuity and globalMassImbalance keep their absolute gates) -- see
// cfd::solver::OuterIterationMonitor. The normalized histories are always
// reported in `robustness`.
struct SIMPLEResult {
  cfd::fields::VectorField velocity;
  cfd::fields::ScalarField pressure;
  cfd::fields::SurfaceField massFlux;

  SIMPLEStatus status{SIMPLEStatus::MaxIterations};
  Index iterations{0};

  Real finalUResidual{};
  Real finalVResidual{};
  Real finalPressureResidual{};
  Real finalContinuityResidual{};
  Real globalMassImbalance{};
  // P12-MESH-006: the W momentum residual of a 3D solve (same definition as
  // U/V: the warm-started linear solve's initial residual); 0 in 2D.
  Real finalWResidual{};

  // P12-MESH-006: the predictor face-flux scheme the solve actually used
  // (SIMPLESettings::faceFlux with Automatic resolved for the mesh).
  FaceFluxScheme faceFlux{FaceFluxScheme::Linear};

  // P2-TURB-004 section 23: the active TurbulenceModel's own
  // convergenceResidual() after the final iteration -- std::nullopt for
  // laminar (or any model that does not report one), populated (and
  // gated on, via SIMPLESettings::turbulenceTolerance) once a transport-
  // equation model like KEpsilonModel is active. Not folded into
  // finalContinuityResidual or any velocity/pressure residual -- a
  // distinct physical quantity gets its own field (TODO.md P0 section 7
  // precedent).
  std::optional<Real> finalTurbulenceResidual;

  // P12-NUM-003: total number of momentum-predictor solves (each a u and a
  // v linear solve) and pressure-correction solves over the whole run --
  // iterations * max(1, SIMPLESettings::nonOrthogonalCorrections) each on
  // a completed run, the observable that pins the correction pass count.
  // GPU-DISC-001M: whether this solve's DISCRETIZATION actually ran on the
  // device, and why it did not when it did not. Reported rather than logged so
  // a test can assert the dispatch instead of parsing output -- the same
  // principle as SolverResult::backendUsed and gpuBackendFallbacks.
  // `gpuDiscretization` false with an empty reason means it was never asked
  // for.
  bool gpuDiscretization{false};
  std::string gpuDiscretizationFallbackReason;
  // GPU-PIPE-001: whether the pressure-correction solve ran directly against
  // the device-resident system instead of round-tripping through a host
  // LinearSystem. Reported for the same reason as gpuDiscretization above -- a
  // test asserts the DISPATCH rather than inferring it from a transfer count.
  // A transfer count proves traffic fell; only this proves which path produced
  // that, and the negative controls need to tell those two apart.
  bool residentPressureSolve{false};
  // GPU-PIPE-001 final residency: whether the whole GPU outer iteration ran
  // device-resident -- momentum assembled and solved on the device, the
  // predictor and corrected velocity never downloaded. Separate from
  // residentPressureSolve because they are separately declinable, and the
  // controlled comparison asserts BOTH so it can prove which one it varied.
  bool residentSimpleLoop{false};

  Index momentumPredictorPasses{0};
  Index pressureCorrectionPasses{0};

  // P12-NUM-007: total inner linear-solver iterations over the whole run --
  // every successful u and v momentum solve (non-orthogonal passes
  // included) and every pressure-correction solve, each counted as its
  // SolverResult::iterations (so linear-solver fallback attempts are
  // included). A cost observable only; nothing reads it back.
  Index momentumLinearIterations{0};
  Index pressureLinearIterations{0};

  std::vector<Real> uResidualHistory;
  std::vector<Real> vResidualHistory;
  std::vector<Real> pressureResidualHistory;
  std::vector<Real> continuityHistory;
  // P12-MESH-006: one entry per completed iteration of a 3D solve; EMPTY in 2D.
  std::vector<Real> wResidualHistory;

  // P12-NUM-004: normalized residual histories and references, the
  // relaxation factors used each iteration, linear-solver fallback events,
  // and the reason for a Stagnated/Diverging/linear-failure status.
  cfd::solver::OuterIterationDiagnostics robustness;

  // GPU-DISC-001Q: wall time per stage of the outer loop, summed over the whole
  // solve. Added because nothing else could answer the question this project
  // actually needed answered -- whether moving discretization to the device
  // REMOVED the CPU assembly bottleneck or merely moved it -- and no existing
  // counter separates assembly from the linear solve.
  //
  // Measured with cfd::Timer (std::chrono::steady_clock): two reads per stage,
  // ~20-30 ns each, under 0.04% of even the fastest measured outer iteration.
  //
  // NO SYNCHRONIZATION IS ADDED. That is deliberate and it changes what these
  // numbers mean on the GPU path: every discretization stage there launches
  // asynchronously, so its timer measures HOST ISSUE TIME, a lower bound on
  // device cost, not device execution. The two solve stages are real wall time
  // (GpuBiCGSTAB synchronizes on each reduction and ends in a blocking D2H),
  // and the SUM over an outer iteration is real, because the iteration ends at
  // a synchronizing point. Device-side attribution of the asynchronous stages
  // needs a profiler; see results/gpu-disc-001/performance/profiling/.
  //
  // Adding a sync here to make the per-stage numbers "honest" would destroy the
  // very property GPU-PIPE-001 Phase 3 created (syncs -66.7%) and would change
  // production behaviour to measure it, which this gate is not permitted to do.
  struct StageSeconds {
    double setup{};                 // pre-loop: derived fields, device plans, initial mass flux
    double momentumAssembly{};      // assembleRelaxedMomentumComponent x {U,V,W}
    double momentumSolve{};         // momentumSolver->solve() x {U,V,W}
    double responseCoefficients{};  // computeMomentumResponseCoefficient x {U,V,W}
    double predictedFaceFlux{};     // rhieChowMassFlux / calculateMassFlux
    double pressureAssembly{};      // assemblePressureCorrection, all passes
    double pressureSolve{};         // pressureSolver->solve(), all passes
    double velocityCorrection{};    // correctVelocity
    double faceFluxCorrection{};    // correctFaceMassFlux
    double bookkeeping{};           // residuals, continuity, convergence tests, history
    double total{};                 // the whole solve() call, inclusive of everything above

    // What the ten stages do not account for: monitor/diagnostics work, the
    // turbulence update, allocation churn, and anything else between stages.
    [[nodiscard]] double other() const noexcept {
      return total - (setup + momentumAssembly + momentumSolve + responseCoefficients +
                      predictedFaceFlux + pressureAssembly + pressureSolve + velocityCorrection +
                      faceFluxCorrection + bookkeeping);
    }
    // The stages that are DISCRETIZATION rather than linear algebra -- the
    // quantity GPU-DISC-001 exists to reduce.
    [[nodiscard]] double discretization() const noexcept {
      return momentumAssembly + responseCoefficients + predictedFaceFlux + pressureAssembly +
             velocityCorrection + faceFluxCorrection;
    }
    [[nodiscard]] double linearSolve() const noexcept { return momentumSolve + pressureSolve; }
  };
  StageSeconds stageSeconds;

  [[nodiscard]] bool converged() const noexcept { return status == SIMPLEStatus::Converged; }
};

}  // namespace cfd::pressure_velocity
