#pragma once

#include <optional>

#include "cfd/algebra/LinearSolver.hpp"
#include "cfd/boundary/BoundaryCondition.hpp"
#include "cfd/discretization/NonOrthogonalDiffusion.hpp"
#include "cfd/fields/ScalarField.hpp"
#include "cfd/gpu/GpuKEpsilonCorrection.hpp"
#include "cfd/mesh/Mesh.hpp"
#include "cfd/physics/FluidProperties.hpp"
#include "cfd/turbulence/KEpsilonCoefficients.hpp"
#include "cfd/turbulence/TurbulenceModel.hpp"
#include "cfd/turbulence/WallTreatment.hpp"

namespace cfd::gpu {
// CUDA-free pimpl facade; only its name crosses this header.
class GpuSimpleDiscretization;
}  // namespace cfd::gpu

namespace cfd::turbulence::testing {
// P16 Step 4 -- declared, never defined in production.
//
// Its definition lives in tests/unit/gpu/KEpsilonModelTestAccess.hpp and is
// compiled ONLY into the GPU test binary. Naming it here grants the friendship;
// it does not make the capability reachable, because no production translation
// unit has a definition to call.
class KEpsilonModelTestAccess;
}  // namespace cfd::turbulence::testing

namespace cfd::turbulence {

// Every numerical control KEpsilonModel needs, beyond the physical
// coefficients (KEpsilonCoefficients) -- P2-TURB-004 section 21-22.
// kFloor/epsilonFloor are the "clearly documented numerical floor" this
// task's section 11 requires: applied only where k/epsilon would
// otherwise sit at/near zero in a *denominator* (mu_t's own formula, and
// the Sp destruction coefficients below), never used to silently
// overwrite a converged, physically negative-trending field elsewhere
// (see KEpsilonModel.cpp's correct() for exactly where each floor is
// applied).
struct KEpsilonConfig {
  KEpsilonCoefficients coefficients;

  // Both required to be finite and > 0 at construction (P2-TURB-004
  // section 10: k/epsilon must never start undefined, and epsilon = 0
  // would make mu_t's own division undefined at cell 0 of iteration 0).
  Real initialK{0.01};
  Real initialEpsilon{0.001};

  cfd::algebra::LinearSolverSettings kSolver{};
  cfd::algebra::LinearSolverSettings epsilonSolver{};

  // P12-NUM-003: non-orthogonal correction of the k/epsilon diffusion terms
  // (default disabled -- exactly the pre-existing operator). Evaluated once
  // per model correct() call from the lagged field; SIMPLE's outer
  // iteration converges the lag. See cfd::transport.
  cfd::discretization::NonOrthogonalCorrectionOptions nonOrthogonal{};

  // Patankar implicit under-relaxation (reusing
  // cfd::pressure_velocity::applyImplicitUnderRelaxation -- see
  // KEpsilonModel.cpp), same alpha semantics as SIMPLESettings::
  // velocityRelaxation: 1.0 disables relaxation.
  Real kRelaxation{0.7};
  Real epsilonRelaxation{0.7};

  // Numerical floors (section 11) -- deliberately tiny, never large
  // enough to visibly perturb a physically-reasonable converged field
  // (chosen well below any of this task's own worked-example values,
  // e.g. k=2, epsilon=0.5).
  Real kFloor{1e-10};
  Real epsilonFloor{1e-10};

  // P16: the high-Re wall treatment.
  //
  // DEFAULT OFF. A standard k-epsilon needs it -- the audit measured a 33%
  // Re_tau error without it (results/p16/turbulence/audit/audit.md) -- but
  // turning it on by default would silently change every existing k-epsilon
  // result, including the pre-P16 channel baseline this phase is measured
  // against. The case system opts in; the baseline stays reproducible.
  //
  // When enabled, the wall-adjacent cell gets:
  //   * epsilon IMPOSED at u_tau*^3/(kappa y_P), not transported;
  //   * production REPLACED by tau_w u_tau*/(kappa y_P);
  //   * k left zero-gradient -- imposing both over-constrains the cell.
  bool wallFunctions{false};
  WallFunctionCoefficients wallFunctionCoefficients{};
};

// Standard k-epsilon (Launder & Spalding 1974) -- P2-TURB-004. Solves
// steady transport equations for k and epsilon (KEpsilonEquation.hpp's
// model-agnostic scalar-transport assembly, reused, not duplicated) and
// exposes mu_t = rho*Cmu*k^2/epsilon through the TurbulenceModel
// interface exactly like LaminarModel does for mu_t=0 -- SIMPLE/PISO
// remain completely unaware this is k-epsilon rather than laminar; they
// only ever call name()/turbulentViscosity()/effectiveViscosity()/
// correct() through the base interface (TODO.md P2-TURB-003's own
// established pattern, unchanged by this task).
//
// No k-omega, SST, wall functions, or near-wall damping -- see this
// class's own .cpp header comment on the wall boundary-condition
// simplification this implies (P2-TURB-004 section 17).
class KEpsilonModel final : public TurbulenceModel {
 public:
  // mesh/fluid/velocityBoundaries/kBoundaries/epsilonBoundaries are not
  // owned; the caller must keep them alive for the lifetime of this
  // object (mirrors PISO's own non-owned reference-member convention).
  // velocityBoundaries is needed here (unlike LaminarModel, which reads
  // none of correct()'s arguments) because computing this iteration's
  // convective mass flux and velocity-gradient-based production both
  // require it -- TurbulenceModel::correct()'s own interface deliberately
  // stays velocity/pressure-only (TODO.md P2-TURB-001), so a real
  // transport-equation model supplies whatever else it needs itself,
  // rather than the interface growing a parameter per future model.
  //
  // k/epsilon are initialized to config.initialK/initialEpsilon in every
  // cell (never lazily/partially initialized -- same invariant
  // LaminarModel establishes for mu_t=0), and mu_t is computed from them
  // immediately, so this object is already valid (a correct(), positive
  // turbulentViscosity()) before correct() is ever called.
  //
  // Throws InvalidArgumentError if config.initialK/initialEpsilon is not
  // finite and > 0, or if any KEpsilonCoefficients entry is not finite
  // and > 0.
  KEpsilonModel(const cfd::mesh::Mesh& mesh, const cfd::physics::FluidProperties& fluid,
                const cfd::boundary::BoundaryConditionSet& velocityBoundaries,
                const cfd::boundary::BoundaryConditionSet& kBoundaries,
                const cfd::boundary::BoundaryConditionSet& epsilonBoundaries,
                KEpsilonConfig config);

  [[nodiscard]] std::string_view name() const noexcept override;

  [[nodiscard]] const cfd::fields::ScalarField& turbulentViscosity() const override;

  [[nodiscard]] const cfd::fields::ScalarField& k() const noexcept;
  [[nodiscard]] const cfd::fields::ScalarField& epsilon() const noexcept;

  // One segregated pass, matching this task's own documented lifecycle
  // (P2-TURB-004 section 20 -- see this class's .cpp for the exact
  // implemented order and why): recompute this iteration's mass flux and
  // velocity-gradient-based production from `velocity`, assemble+solve
  // the k equation, assemble+solve the epsilon equation, then update
  // mu_t from the freshly-solved k/epsilon. Both equations' *own*
  // coefficients (Gamma_k/Gamma_eps via the pre-update mu_t, and the
  // Su/Sp source linearization via the pre-update k/epsilon) are
  // deliberately evaluated from the state at the *start* of this call --
  // i.e. k and epsilon are each updated using the other's *previous*
  // value, exactly the same one-iteration-lagged coupling convention
  // SIMPLE itself already applies to mu_eff across outer iterations
  // (TODO.md P2-TURB-003) -- not a partially-updated mix within a single
  // correct() call. `pressure` is accepted (matching the base interface)
  // but unused: standard k-epsilon has no direct pressure dependence.
  //
  // Throws InvalidArgumentError if velocity/pressure's size does not
  // match mesh.numberOfCells(). Throws NumericalError if either the k or
  // the epsilon linear solve fails to converge, or if either assembled
  // system or the resulting mu_t is non-finite -- k_/epsilon_/
  // turbulentViscosity_ are left completely unmodified in that case (the
  // failing update is discarded wholesale, never partially applied), the
  // same "do not silently accept a failed step" policy
  // TransientSolver/PISO already apply to their own per-step failures.
  // SIMPLE/PISO call this from inside the same try/catch that already
  // catches momentum-assembly's NumericalError/InvalidArgumentError
  // (TODO.md P2-TURB-003), mapping a correct() failure to the identical
  // NonFiniteState outcome as any other runtime-invalid assembled system
  // this outer iteration -- not a crash.
  void correct(const cfd::mesh::Mesh& mesh, const cfd::fields::VectorField& velocity,
               const cfd::fields::ScalarField& pressure) override;

  // P2-TURB-004 section 23-24: max(|k_new - k_old|, |epsilon_new -
  // epsilon_old|) over every cell, from the *last successful* correct()
  // call -- std::nullopt before correct() has ever been called (there is
  // no "change" to report yet; construction alone does not count,
  // mirroring LaminarModel's own "valid but uncorrected" starting state).
  // Absolute, not relative -- same convention as
  // thermal::ThermalSolver's own maxTemperatureChange outer-convergence
  // gate, which this mirrors.
  [[nodiscard]] std::optional<Real> convergenceResidual() const override;

  // P16 -- the wall treatment's outputs, valid after the first correct().
  //
  // The momentum bridge: a per-FACE effective viscosity, zero away from wall
  // faces. nullptr when wall functions are off, so a caller threading it
  // through changes nothing for a model that does not use them.
  [[nodiscard]] const cfd::fields::SurfaceField* wallViscosity() const override;

  // y+ per cell, zero away from walls. A real output a user can export, not an
  // internal transient: a wall treatment whose resolution cannot be inspected
  // cannot be trusted.
  [[nodiscard]] const cfd::fields::ScalarField& yPlus() const noexcept { return yPlus_; }
  [[nodiscard]] const cfd::fields::ScalarField& wallShearStress() const noexcept {
    return wallShearStress_;
  }
  // How many wall faces landed outside [30, 300], and how many hit the
  // mu_t >= 0 viscosity floor. Both are symptoms of a wall-normal spacing the
  // high-Re wall function cannot be applied at; reported, never swallowed.
  [[nodiscard]] Index wallFacesOutsideValidBand() const noexcept {
    return wallFacesOutsideValidBand_;
  }
  [[nodiscard]] Index wallFacesWithFlooredViscosity() const noexcept {
    return wallFacesWithFlooredViscosity_;
  }
  // P16-TWF: transport this model's own wall state to the thermal wall
  // function. Null unless the wall treatment actually ran.
  [[nodiscard]] const cfd::fields::ScalarField* wallShearStressField() const override {
    return config_.wallFunctions ? &wallShearStress_ : nullptr;
  }
  [[nodiscard]] const cfd::fields::ScalarField* wallYPlusField() const override {
    return config_.wallFunctions ? &yPlus_ : nullptr;
  }

  [[nodiscard]] Real minimumWallYPlus() const noexcept { return minimumWallYPlus_; }
  [[nodiscard]] Real maximumWallYPlus() const noexcept { return maximumWallYPlus_; }

 private:
  const cfd::mesh::Mesh& mesh_;
  const cfd::physics::FluidProperties& fluid_;
  const cfd::boundary::BoundaryConditionSet& velocityBoundaries_;
  // P16 Step 4 -- equivalence testing only.
  //
  // WHY A FRIEND AND NOT A SETTER. The CPU/GPU implementation-equivalence gate
  // needs one outer iteration run from an IDENTICAL mature state on both
  // backends, and a mature turbulence state cannot be reached by construction:
  // the constructor takes uniform scalars.
  //
  // It is deliberately NOT a public setter. P13-001 refuses turbulence
  // checkpoint/resume precisely because restoring k, epsilon and mu_t correctly
  // is unsolved, and a public mutator would make that refusal reachable through
  // the case file, the CLI or the GUI. A friend whose definition exists only in
  // the test binary cannot.
  friend class cfd::turbulence::testing::KEpsilonModelTestAccess;

  const cfd::boundary::BoundaryConditionSet& kBoundaries_;
  const cfd::boundary::BoundaryConditionSet& epsilonBoundaries_;
  KEpsilonConfig config_;

  cfd::fields::ScalarField k_;
  cfd::fields::ScalarField epsilon_;
  cfd::fields::ScalarField turbulentViscosity_;
  std::optional<Real> lastConvergenceResidual_;

  // P16. Wall faces are geometry, so they are collected ONCE at construction --
  // the mesh and its wall patches never change during a solve, the same
  // reasoning SSTModel already applies to its wall-distance field.
  std::vector<WallFace> wallFaces_;
  cfd::fields::SurfaceField wallViscosity_;
  cfd::fields::ScalarField yPlus_;
  cfd::fields::ScalarField wallShearStress_;
  Index wallFacesOutsideValidBand_{};
  Index wallFacesWithFlooredViscosity_{};
  Real minimumWallYPlus_{};
  Real maximumWallYPlus_{};

  // P16-GPU-TURB Stage 7D -- the production backend seam.
  //
  // SELECTED BY `config_.kSolver.backend`, which is how every other backend
  // decision in this project is expressed (LinearSolverFactory, SIMPLE's
  // pressure and momentum stages). No new interface, no new configuration
  // concept, and nothing in TurbulenceModel changes -- SIMPLE/PISO still only
  // ever call name()/turbulentViscosity()/correct().
  //
  // The facade is CUDA-free; in a CPU-only build its stub's available()
  // returns false at compile time. `gpuActive_` is decided ONCE at
  // construction and never re-examined, so a solve cannot silently change
  // backend halfway through.
  //
  // THE WALL TREATMENT STAYS ON THE HOST either way. The GPU branch evaluates
  // computeWallTreatment exactly as the CPU branch does and passes its compact
  // O(wall cells) outputs down; only the transport and the algebra move.
  cfd::gpu::GpuKEpsilonCorrection gpuCorrection_;
  bool gpuActive_{false};

  // The two branches of correct(). Split so the ordering each preserves is
  // readable in one piece rather than interleaved with a backend test.
  void correctOnHost(const cfd::mesh::Mesh& mesh, const cfd::fields::VectorField& velocity,
                     const cfd::fields::ScalarField& pressure);
  void correctOnDevice(const cfd::mesh::Mesh& mesh, const cfd::fields::VectorField& velocity);

 public:
  // Which backend correct() will actually use. Reported rather than inferred:
  // a caller that asked for GPU and got CPU can see it here, and a test cannot
  // mistake a numerically-correct CPU fallback for GPU execution.
  [[nodiscard]] bool usingGpuBackend() const noexcept { return gpuActive_; }

  // P16 Step 4. True only when the whole resident chain is present: the GPU
  // backend was requested, the device is usable, and prepare() built every
  // plan. `gpuActive_` is decided once in the constructor and is exactly that
  // conjunction, so this is not a second, looser test.
  [[nodiscard]] bool supportsResidentGpuCorrection() const noexcept override { return gpuActive_; }

  // The resident correction, driven from the discretization's own device
  // state. Returns false when the discretization has no resident state yet,
  // which the caller must treat as "not eligible this iteration" rather than
  // as a failure. Throws what correct() throws on a genuine numerical failure.
  //
  // It reads NO host velocity. See TurbulenceModel::supportsResidentGpuCorrection.
  [[nodiscard]] bool correctResidentGpu(const cfd::mesh::Mesh& mesh,
                                        cfd::gpu::GpuSimpleDiscretization& discretization) override;

  // Empty when the GPU path was not requested or is in use; otherwise why it
  // was refused.
  [[nodiscard]] const std::string& gpuUnsupportedReason() const noexcept {
    return gpuCorrection_.unsupportedReason();
  }
};

}  // namespace cfd::turbulence
