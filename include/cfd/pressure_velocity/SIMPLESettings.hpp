#pragma once

#include "cfd/algebra/LinearSolver.hpp"
#include "cfd/core/Types.hpp"
#include "cfd/discretization/Convection.hpp"
#include "cfd/discretization/Gradient.hpp"
#include "cfd/discretization/NonOrthogonalDiffusion.hpp"
#include "cfd/solver/SolverRobustness.hpp"

namespace cfd::pressure_velocity {

// Every numerical control SIMPLE needs, gathered in one place rather
// than scattered as magic numbers through the iteration (TODO.md P0 --
// SIMPLE section 4). Defaults are reasonable starting points for a
// lid-driven cavity, not correctness guarantees -- validateSIMPLESettings
// only checks structural validity (finite, in-range), not "will this
// converge for your case".
struct SIMPLESettings {
  Index maxIterations{1000};

  Real velocityRelaxation{0.7};
  Real pressureRelaxation{0.3};

  Real velocityTolerance{1e-8};
  Real pressureTolerance{1e-8};
  Real continuityTolerance{1e-8};

  // P2-TURB-004 section 24: when the active TurbulenceModel reports a
  // convergence residual (TurbulenceModel::convergenceResidual() --
  // LaminarModel and any model that does not override it report
  // std::nullopt, meaning "no turbulence residual to gate on", so this
  // tolerance is simply unused and laminar convergence behavior is
  // unchanged), SIMPLE additionally requires that residual to be <=
  // this value before reporting Converged -- otherwise U/V/P settling
  // while k/epsilon are still visibly changing would be reported as a
  // converged RANS solve, which is physically wrong.
  Real turbulenceTolerance{1e-6};

  cfd::algebra::LinearSolverSettings momentumSolver;
  cfd::algebra::LinearSolverSettings pressureSolver;

  // P12-NUM-001: which convection scheme assembleRelaxedMomentumComponent
  // uses for the u/v momentum equations. Defaults to Upwind -- exactly
  // the only scheme that existed before this field, so every pre-
  // P12-NUM-001 caller (which never sets this) gets byte-identical
  // behavior. See cfd::discretization::ConvectionScheme's own header
  // comment.
  cfd::discretization::ConvectionScheme convectionScheme{
      cfd::discretization::ConvectionScheme::Upwind};

  // P12-NUM-002: which gradient reconstruction the pressure-source term
  // (assembleRelaxedMomentumComponent) and the velocity-correction step
  // (correctVelocity) use. Defaults to GreenGauss -- exactly the only
  // scheme that existed before this field, so every pre-P12-NUM-002
  // caller (which never sets this) gets byte-identical behavior. See
  // cfd::discretization::GradientScheme's own header comment.
  cfd::discretization::GradientScheme gradientScheme{
      cfd::discretization::GradientScheme::GreenGauss};

  // P12-NUM-003: the non-orthogonal correction switch AND pass count.
  //   0 (default, exactly the pre-P12-NUM-003 solver): uncorrected two-
  //     point diffusion everywhere; two-point pressure-correction coupling.
  //   N >= 1: every diffusion term is corrected (momentum here; thermal/
  //     species/turbulence through nonOrthogonalOptions() below), and each
  //     outer iteration runs N momentum-predictor passes
  //     (runNonOrthogonalCorrectionPasses) and N pressure-correction passes
  //     (over-relaxed implicit coefficient; passes 2..N add the explicit
  //     -rho_f T . grad(p') term) -- see SIMPLE.cpp. SIMPLEResult's
  //     momentumPredictorPasses / pressureCorrectionPasses count them.
  // On an orthogonal (Cartesian) mesh every correction term is exactly
  // zero: N = 1 is bit-identical to N = 0.
  Index nonOrthogonalCorrections{0};

  // P6-GPU-001 -- Performance: opt-in only, default false, so every
  // existing caller's behavior (CPU-only build or not) is unchanged
  // unless it deliberately sets this. When true, SIMPLE::solve() mirrors
  // each outer iteration's momentum/pressure-correction matrices and u/
  // v/pressure fields into a persistent, per-solve()-call
  // cfd::gpu::GpuResidencyManager (structure uploaded once, values
  // updated in place thereafter -- see that class's own header comment)
  // purely to keep that residency path exercised against genuine
  // production data. This never affects the numerical result: the
  // mirrored GPU data is not read back into the solve, and the CPU
  // linear solvers (momentumSolver/pressureSolver above) remain the sole
  // source of SIMPLE's computed velocity/pressure -- no GPU linear
  // solver exists yet (TODO.md P6-GPU-002). Silently inert (no CUDA
  // calls at all) in a CPU-only build, or a CUDA build with no usable
  // device at runtime -- see GpuResidencyManager::active().
  bool enableGpuResidency{false};

  // P12-NUM-004: normalized residuals / convergence criterion, stagnation
  // and divergence detection, adaptive under-relaxation and the linear-
  // solver fallback (cfd/solver/SolverRobustness.hpp). Default-constructed:
  // absolute criterion, every feature off -- exactly the pre-P12-NUM-004
  // solver (velocityRelaxation/pressureRelaxation above stay fixed; with
  // the adaptive controller enabled they are its INITIAL values). The
  // normalized residual histories are always reported.
  cfd::solver::SolverRobustnessSettings robustness;
};

// Throws InvalidArgumentError if:
//   - maxIterations == 0
//   - velocityRelaxation or pressureRelaxation is not finite, or not in
//     (0, 1] (TODO.md section 5: 0 < alpha <= 1, never silently clamped)
//   - any tolerance is not finite and > 0 (turbulenceTolerance included,
//     even though it goes unused unless a turbulence model actually
//     reports a residual -- keeping every tolerance field uniformly
//     validated is simpler and safer than special-casing the one that is
//     sometimes inert)
//   - P12-NUM-004: `robustness` is invalid
//     (cfd::solver::validateSolverRobustnessSettings)
void validateSIMPLESettings(const SIMPLESettings& settings);

// P12-NUM-003: the non-orthogonal-correction options every OTHER implicit
// diffusion term of a case (thermal conduction, species diffusion, k/
// epsilon/omega diffusion) is assembled with -- derived from the same two
// case settings the momentum path uses (nonOrthogonalCorrections >= 1 ->
// enabled; gradientScheme -> the correction's gradient), so one
// solver.json switch controls every diffusion term consistently.
[[nodiscard]] cfd::discretization::NonOrthogonalCorrectionOptions nonOrthogonalOptions(
    const SIMPLESettings& settings) noexcept;

}  // namespace cfd::pressure_velocity
