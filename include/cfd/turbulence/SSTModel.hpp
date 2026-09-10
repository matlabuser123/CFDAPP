#pragma once

#include <optional>

#include "cfd/algebra/LinearSolver.hpp"
#include "cfd/boundary/BoundaryCondition.hpp"
#include "cfd/fields/ScalarField.hpp"
#include "cfd/mesh/Mesh.hpp"
#include "cfd/physics/FluidProperties.hpp"
#include "cfd/turbulence/SSTCoefficients.hpp"
#include "cfd/turbulence/TurbulenceModel.hpp"

namespace cfd::turbulence {

// Every numerical control SSTModel needs, beyond the physical
// coefficients (SSTCoefficients) -- P2-TURB-006, mirrors KOmegaConfig's
// own shape/reasoning exactly (SST transports the same two fields, k and
// omega). kFloor/omegaFloor are applied only where k/omega would
// otherwise sit in a denominator (SSTEquation.hpp's own functions each
// document exactly which floors they apply internally), never used to
// silently overwrite a converged, physically negative-trending field
// elsewhere.
struct SSTConfig {
  SSTCoefficients coefficients;

  // Both required to be finite and > 0 at construction (section 29: k/
  // omega must never start undefined).
  Real initialK{0.01};
  Real initialOmega{10.0};

  cfd::algebra::LinearSolverSettings kSolver{};
  cfd::algebra::LinearSolverSettings omegaSolver{};

  // Patankar implicit under-relaxation, same mechanism/semantics as
  // KEpsilonConfig/KOmegaConfig's own (section 34: "reuse generic k/
  // omega under-relaxation... do not introduce arbitrary SST-only
  // relaxation").
  Real kRelaxation{0.7};
  Real omegaRelaxation{0.7};

  Real kFloor{1e-10};
  Real omegaFloor{1e-10};
};

// Menter SST k-omega (1994 original formulation -- see SSTCoefficients.hpp's
// own header comment on exactly which SST revision) -- P2-TURB-006.
// Behaves like standard k-omega near walls (F1 -> 1) and blends toward a
// k-epsilon-derived formulation away from walls (F1 -> 0), implements
// the F1/F2 blending functions, cross-diffusion, and the eddy-viscosity
// limiter that are SST's own defining features (none of which exist in
// cfd::turbulence::KOmegaModel). Reuses KEpsilonEquation.hpp's model-
// agnostic scalar-transport assembly (diffusion/convection/implicit-
// source/relaxed-solve) and TurbulenceProduction.hpp's strain/production
// machinery unmodified -- no second scalar-equation or SIMPLE-like
// solver architecture exists here (section 2's own "do not copy the
// entire KOmegaModel implementation and independently evolve two nearly
// identical solvers"). Exposes mu_t through the TurbulenceModel
// interface exactly like every other concrete model -- SIMPLE/PISO
// remain completely unaware this is SST rather than k-omega, k-epsilon,
// or laminar.
//
// No SST transition (gamma-Re_theta), no compressibility/curvature/
// rotation corrections, no rough-wall treatment -- see this class's own
// .cpp comment on the wall omega treatment this task actually
// implements (a real Wilcox near-wall formula, via
// cfd::boundary::WallOmega, now that wall distance is a genuine
// capability -- unlike KOmegaModel's own documented zero-gradient
// simplification).
class SSTModel final : public TurbulenceModel {
 public:
  // mesh/fluid/velocityBoundaries/kBoundaries/omegaBoundaries are not
  // owned; the caller must keep them alive for the lifetime of this
  // object (mirrors KOmegaModel's own non-owned reference-member
  // convention). Wall distance (WallDistance.hpp) is computed exactly
  // once here, from velocityBoundaries, and cached for the lifetime of
  // this object (section 58: "for fixed meshes, wall distance can be
  // computed once").
  //
  // k/omega are initialized to config.initialK/initialOmega in every
  // cell (never lazily/partially initialized), and mu_t is computed from
  // them immediately (using F1/F2/strain evaluated against a zero
  // velocity field, i.e. S=0, so mu_t reduces to rho*a1*k/(a1*omega) =
  // rho*k/omega at construction -- the same value standard k-omega would
  // give, a sensible, well-defined starting point before the first real
  // correct() call), so this object is already valid before correct()
  // is ever called.
  //
  // Throws InvalidArgumentError if config.initialK/initialOmega is not
  // finite and > 0, if any SSTCoefficients entry is not finite and > 0,
  // or if velocityBoundaries has no Wall/MovingWall patch (see
  // WallDistance.hpp).
  SSTModel(const cfd::mesh::Mesh& mesh, const cfd::physics::FluidProperties& fluid,
          const cfd::boundary::BoundaryConditionSet& velocityBoundaries,
          const cfd::boundary::BoundaryConditionSet& kBoundaries,
          const cfd::boundary::BoundaryConditionSet& omegaBoundaries, SSTConfig config);

  [[nodiscard]] std::string_view name() const noexcept override;

  [[nodiscard]] const cfd::fields::ScalarField& turbulentViscosity() const override;

  [[nodiscard]] const cfd::fields::ScalarField& k() const noexcept;
  [[nodiscard]] const cfd::fields::ScalarField& omega() const noexcept;
  // F1/F2/wallDistance from the *last successful* correct() call (or,
  // before the first one, the construction-time values -- see this
  // class's own constructor comment). Exposed publicly (section 3: "only
  // store them persistently if useful for validation/export" -- the
  // mandated F1/F2-bounds and wall-distance sanity checks, sections
  // 38-39/49, need them).
  [[nodiscard]] const cfd::fields::ScalarField& f1() const noexcept;
  [[nodiscard]] const cfd::fields::ScalarField& f2() const noexcept;
  [[nodiscard]] const cfd::fields::ScalarField& wallDistance() const noexcept;

  // One segregated pass, the same generic RANS lifecycle P2-TURB-003
  // established and KEpsilonModel/KOmegaModel already implement (section
  // 31 -- "do not create an SST-specific SIMPLE implementation"): mass
  // flux + velocity gradients + strain S^2 (reusing
  // computeVelocityGradient/computeStrainRateMagnitudeSquared
  // unmodified) -> grad(k)/grad(omega) (reusing
  // discretization::gradient, the same scalar Gauss-gradient operator
  // pressure/pressure-correction already use) -> cross-diffusion
  // coefficient -> F1/F2 -> blended coefficients -> production (raw,
  // from the *pre-update* stored mu_t, then limited) -> solve k -> solve
  // omega (including cross-diffusion) -> update mu_t via the SST
  // limiter, from the freshly-solved k/omega and *this call's* S/F2
  // (documented iteration-ordering choice, section 32 -- see .cpp).
  // Wall distance is read from the cached member, never recomputed.
  //
  // Throws InvalidArgumentError if velocity/pressure's size does not
  // match mesh.numberOfCells(). Throws NumericalError if either the k or
  // the omega linear solve fails to converge, or if either assembled
  // system or the resulting mu_t is non-finite -- k_/omega_/
  // turbulentViscosity_/f1_/f2_ are left completely unmodified in that
  // case, the same all-or-nothing policy KEpsilonModel/KOmegaModel
  // already apply.
  void correct(const cfd::mesh::Mesh& mesh, const cfd::fields::VectorField& velocity,
              const cfd::fields::ScalarField& pressure) override;

  // max(|k_new - k_old|, |omega_new - omega_old|) over every cell, from
  // the last successful correct() call -- std::nullopt before correct()
  // has ever been called. Same convention as
  // KEpsilonModel/KOmegaModel::convergenceResidual().
  [[nodiscard]] std::optional<Real> convergenceResidual() const override;

 private:
  const cfd::mesh::Mesh& mesh_;
  const cfd::physics::FluidProperties& fluid_;
  const cfd::boundary::BoundaryConditionSet& velocityBoundaries_;
  const cfd::boundary::BoundaryConditionSet& kBoundaries_;
  const cfd::boundary::BoundaryConditionSet& omegaBoundaries_;
  SSTConfig config_;

  cfd::fields::ScalarField wallDistance_;
  cfd::fields::ScalarField k_;
  cfd::fields::ScalarField omega_;
  cfd::fields::ScalarField turbulentViscosity_;
  cfd::fields::ScalarField f1_;
  cfd::fields::ScalarField f2_;
  std::optional<Real> lastConvergenceResidual_;
};

}  // namespace cfd::turbulence
