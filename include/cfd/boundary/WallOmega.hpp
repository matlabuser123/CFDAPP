#pragma once

#include "cfd/boundary/BoundaryCondition.hpp"

namespace cfd::boundary {

// P2-TURB-006: the standard (Wilcox) near-wall asymptotic omega value --
//
//   omega_wall = 60 * nu / (beta1 * y^2)
//
// where nu is the *kinematic* molecular viscosity (mu/rho -- SST's own
// near-wall formulation is expressed in kinematic terms, unlike this
// codebase's usual dynamic-viscosity convention; see
// SSTModel.cpp/turbulence::TurbulenceModel.hpp's own unit-convention
// comment) and y is the normal distance from the owner cell centroid to
// this boundary face -- `normalDistance`, supplied automatically by the
// same discretization machinery every other boundary condition already
// uses (cfd::discretization::interpolateBoundaryFace /
// physics::MomentumEquation's boundaryVelocity() / etc.). beta1 is
// SST's own *inner* (near-wall) beta constant (SSTCoefficients::beta1),
// not the blended beta -- the wall is, by definition, exactly where the
// inner coefficient set applies (F1 -> 1).
//
// "Boundary vs first-cell interpretation" (P2-TURB-006 section 28): the
// true continuous omega field is singular exactly at a solid wall (y=0
// makes the formula blow up), so this is not evaluated as a true y=0
// Dirichlet value -- it is evaluated at the *first interior cell's own*
// wall distance (== normalDistance for a wall-adjacent cell, since the
// wall face itself lies exactly on the wall), giving a well-defined,
// finite value there instead. This is the standard interpretation used
// throughout the k-omega/SST literature and this is a deliberate,
// documented choice, not accidental reuse of a formula intended for a
// different location.
//
// Deliberately distinct from the *simplified* zero-gradient omega wall
// treatment cfd::turbulence::KOmegaModel's own case-derived boundaries
// still use (P2-TURB-005 section 28's own "documented simplification")
// -- SST is explicitly given a real wall-distance capability this task
// introduces, so it uses the real formula rather than silently copying
// standard k-omega's simplification (P2-TURB-006 section 28's own "do
// not silently copy... unless the same formula is intended").
class WallOmega final : public ScalarBoundaryCondition {
 public:
  // Throws InvalidArgumentError if kinematicViscosity is not finite and
  // > 0, or beta1 is not finite and > 0.
  WallOmega(Real kinematicViscosity, Real beta1);

  [[nodiscard]] Real kinematicViscosity() const noexcept;
  [[nodiscard]] Real beta1() const noexcept;

  [[nodiscard]] BoundaryConditionType type() const noexcept override;
  [[nodiscard]] std::string_view name() const noexcept override;

  // Ignores ownerValue (matches FixedValue's own "ignores owner"
  // convention -- this is a Dirichlet value, not a reconstructed
  // gradient). Throws InvalidArgumentError if normalDistance is
  // non-finite or <= 0 (same HeatFlux/FixedGradient::boundaryValue
  // contract).
  [[nodiscard]] Real boundaryValue(Real ownerValue, Real normalDistance) const override;

 private:
  Real kinematicViscosity_{};
  Real beta1_{};
};

}  // namespace cfd::boundary
