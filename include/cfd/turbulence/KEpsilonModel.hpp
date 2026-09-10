#pragma once

#include <optional>

#include "cfd/algebra/LinearSolver.hpp"
#include "cfd/boundary/BoundaryCondition.hpp"
#include "cfd/fields/ScalarField.hpp"
#include "cfd/mesh/Mesh.hpp"
#include "cfd/physics/FluidProperties.hpp"
#include "cfd/turbulence/KEpsilonCoefficients.hpp"
#include "cfd/turbulence/TurbulenceModel.hpp"

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

 private:
  const cfd::mesh::Mesh& mesh_;
  const cfd::physics::FluidProperties& fluid_;
  const cfd::boundary::BoundaryConditionSet& velocityBoundaries_;
  const cfd::boundary::BoundaryConditionSet& kBoundaries_;
  const cfd::boundary::BoundaryConditionSet& epsilonBoundaries_;
  KEpsilonConfig config_;

  cfd::fields::ScalarField k_;
  cfd::fields::ScalarField epsilon_;
  cfd::fields::ScalarField turbulentViscosity_;
  std::optional<Real> lastConvergenceResidual_;
};

}  // namespace cfd::turbulence
