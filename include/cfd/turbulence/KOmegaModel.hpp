#pragma once

#include <optional>

#include "cfd/algebra/LinearSolver.hpp"
#include "cfd/boundary/BoundaryCondition.hpp"
#include "cfd/fields/ScalarField.hpp"
#include "cfd/mesh/Mesh.hpp"
#include "cfd/physics/FluidProperties.hpp"
#include "cfd/turbulence/KOmegaCoefficients.hpp"
#include "cfd/turbulence/TurbulenceModel.hpp"

namespace cfd::turbulence {

// Every numerical control KOmegaModel needs, beyond the physical
// coefficients (KOmegaCoefficients) -- P2-TURB-005, mirrors
// KEpsilonConfig's own shape/reasoning exactly. kFloor/omegaFloor are
// the "clearly documented numerical floor" section 11 requires: applied
// only where k/omega would otherwise sit in a *denominator* (mu_t's own
// formula uses omega; the omega equation's production term uses k --
// see KOmegaModel.cpp's correct() for exactly where each floor is
// applied), never used to silently overwrite a converged, physically
// negative-trending field elsewhere.
struct KOmegaConfig {
  KOmegaCoefficients coefficients;

  // Both required to be finite and > 0 at construction (section 10: k/
  // omega must never start undefined, and omega = 0 would make mu_t's
  // own division undefined at cell 0 of iteration 0).
  Real initialK{0.01};
  Real initialOmega{10.0};

  cfd::algebra::LinearSolverSettings kSolver{};
  cfd::algebra::LinearSolverSettings omegaSolver{};

  // Patankar implicit under-relaxation (reusing
  // cfd::pressure_velocity::applyImplicitUnderRelaxation via
  // turbulence::solveRelaxedScalarTransport -- the same mechanism
  // KEpsilonModel already uses, section 25's "if the existing turbulence
  // infrastructure uses equation relaxation... reuse it"), same alpha
  // semantics as SIMPLESettings::velocityRelaxation: 1.0 disables
  // relaxation.
  Real kRelaxation{0.7};
  Real omegaRelaxation{0.7};

  // Numerical floors (section 11) -- deliberately tiny, never large
  // enough to visibly perturb a physically-reasonable converged field
  // (chosen well below any of this task's own worked-example values,
  // e.g. k=2, omega=3).
  Real kFloor{1e-10};
  Real omegaFloor{1e-10};
};

// Standard (Wilcox) k-omega -- P2-TURB-005. Solves steady transport
// equations for k and omega, reusing KEpsilonEquation.hpp's model-
// agnostic scalar-transport assembly (diffusion/convection/implicit
// source/relaxed-solve) exactly as KEpsilonModel does -- the only
// k-omega-specific pieces are the mu_t formula, the production/
// destruction coefficients, and the *linear* effective-diffusivity form
// (KOmegaEquation.hpp's computeLinearEffectiveDiffusivity, deliberately
// distinct from k-epsilon's own reciprocal form -- see that header's
// comment). Exposes mu_t = rho*k/omega through the TurbulenceModel
// interface exactly like KEpsilonModel/LaminarModel do for their own
// mu_t -- SIMPLE/PISO remain completely unaware this is k-omega rather
// than k-epsilon or laminar; they only ever call name()/
// turbulentViscosity()/effectiveViscosity()/correct() through the base
// interface (TODO.md P2-TURB-003's own established pattern, unchanged by
// this task).
//
// No SST, no blending functions, no cross-diffusion term, no wall-
// distance-based near-wall treatment -- see this class's own .cpp
// comment on the wall boundary-condition simplification this implies
// (section 20, mirroring P2-TURB-004 section 17's own k-epsilon
// disclosure).
class KOmegaModel final : public TurbulenceModel {
 public:
  // mesh/fluid/velocityBoundaries/kBoundaries/omegaBoundaries are not
  // owned; the caller must keep them alive for the lifetime of this
  // object (mirrors KEpsilonModel's/PISO's own non-owned reference-
  // member convention). velocityBoundaries is needed here for the same
  // reason as KEpsilonModel: computing this iteration's convective mass
  // flux and velocity-gradient-based production both require it.
  //
  // k/omega are initialized to config.initialK/initialOmega in every
  // cell (never lazily/partially initialized -- same invariant
  // KEpsilonModel/LaminarModel establish), and mu_t is computed from
  // them immediately, so this object is already valid (a correct(),
  // positive turbulentViscosity()) before correct() is ever called.
  //
  // Throws InvalidArgumentError if config.initialK/initialOmega is not
  // finite and > 0, or if any KOmegaCoefficients entry is not finite and
  // > 0.
  KOmegaModel(const cfd::mesh::Mesh& mesh, const cfd::physics::FluidProperties& fluid,
              const cfd::boundary::BoundaryConditionSet& velocityBoundaries,
              const cfd::boundary::BoundaryConditionSet& kBoundaries,
              const cfd::boundary::BoundaryConditionSet& omegaBoundaries, KOmegaConfig config);

  [[nodiscard]] std::string_view name() const noexcept override;

  [[nodiscard]] const cfd::fields::ScalarField& turbulentViscosity() const override;

  [[nodiscard]] const cfd::fields::ScalarField& k() const noexcept;
  [[nodiscard]] const cfd::fields::ScalarField& omega() const noexcept;

  // One segregated pass, the same generic RANS lifecycle P2-TURB-003
  // established and KEpsilonModel already implements (section 22 --
  // this task's own "do not create a second SIMPLE implementation," and
  // there is no second turbulence-correction lifecycle here either):
  // recompute this iteration's mass flux and velocity-gradient-based
  // production from `velocity`, assemble+solve the k equation,
  // assemble+solve the omega equation, then update mu_t from the
  // freshly-solved k/omega. Both equations' *own* coefficients
  // (Gamma_k/Gamma_omega via the pre-update mu_t, and the Su/Sp source
  // linearization via the pre-update k/omega) are deliberately evaluated
  // from the state at the *start* of this call -- the same one-
  // iteration-lagged coupling convention KEpsilonModel already uses, in
  // turn mirroring SIMPLE's own mu_eff lag across outer iterations.
  // `pressure` is accepted (matching the base interface) but unused:
  // standard k-omega has no direct pressure dependence.
  //
  // Throws InvalidArgumentError if velocity/pressure's size does not
  // match mesh.numberOfCells(). Throws NumericalError if either the k or
  // the omega linear solve fails to converge, or if either assembled
  // system or the resulting mu_t is non-finite -- k_/omega_/
  // turbulentViscosity_ are left completely unmodified in that case,
  // the same all-or-nothing policy KEpsilonModel already applies.
  // SIMPLE/PISO call this from inside the same try/catch that already
  // catches momentum-assembly's NumericalError/InvalidArgumentError,
  // mapping a correct() failure to NonFiniteState -- not a crash.
  void correct(const cfd::mesh::Mesh& mesh, const cfd::fields::VectorField& velocity,
               const cfd::fields::ScalarField& pressure) override;

  // Section 23-24: max(|k_new - k_old|, |omega_new - omega_old|) over
  // every cell, from the *last successful* correct() call --
  // std::nullopt before correct() has ever been called. Same absolute-
  // change convention as KEpsilonModel::convergenceResidual() (which in
  // turn mirrors thermal::ThermalSolver's own maxTemperatureChange).
  [[nodiscard]] std::optional<Real> convergenceResidual() const override;

 private:
  const cfd::mesh::Mesh& mesh_;
  const cfd::physics::FluidProperties& fluid_;
  const cfd::boundary::BoundaryConditionSet& velocityBoundaries_;
  const cfd::boundary::BoundaryConditionSet& kBoundaries_;
  const cfd::boundary::BoundaryConditionSet& omegaBoundaries_;
  KOmegaConfig config_;

  cfd::fields::ScalarField k_;
  cfd::fields::ScalarField omega_;
  cfd::fields::ScalarField turbulentViscosity_;
  std::optional<Real> lastConvergenceResidual_;
};

}  // namespace cfd::turbulence
