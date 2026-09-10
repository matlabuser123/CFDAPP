#pragma once

#include "cfd/core/Types.hpp"
#include "cfd/core/Vector2.hpp"

namespace cfd::physics {

// P3-PHYS-001: the Boussinesq buoyancy body-force model. A thin,
// validated value type (construction is the only place invalid physics
// can enter), same design precedent as FluidProperties/ThermalProperties
// -- every downstream consumer can assume a valid BoussinesqBuoyancy
// without re-checking.
//
// Governing model: rho(T) = rho_ref * [1 - beta*(T - T_ref)]. The
// pressure field this solver's momentum equation already carries is
// *modified* (dynamic) pressure -- the hydrostatic reference-density
// term rho_ref*g is understood to already be absorbed into it (this is
// the entire point of the Boussinesq approximation: constant-density
// continuity/pressure-correction stays untouched, TODO.md's own
// "do not replace constant-density continuity" constraint), so only the
// *deviation* from the reference hydrostatic state enters the momentum
// equation as an explicit source:
//
//   rho*g - rho_ref*g = (rho - rho_ref)*g
//                      = rho_ref*[1 - beta*(T-T_ref) - 1]*g
//                      = -rho_ref*beta*(T - T_ref)*g
//
//   S_b = -rho_ref * beta * (T - T_ref) * g
//
// Sign convention, verified against a worked physical example (also
// covered by BoussinesqBuoyancyTest.HotCellWithDownwardGravityDrivesFluidUpward):
// with g = (0, -9.81) (gravity pointing in -y, this class's own default)
// and T > T_ref, S_b.y = -rho_ref*beta*(T-T_ref)*(-9.81) > 0 -- an
// *upward* (+y) force on hot fluid, opposite the downward gravity
// direction, exactly the physically-required behavior a naive
// S_b = +rho_ref*beta*(T-T_ref)*g (no leading minus) gets backwards --
// that formula would give hot fluid a *stronger downward* acceleration,
// the exact common error this class's own header comment and P3-PHYS-001's
// task spec both warn against. The leading minus sign above is not
// optional or a matter of taste; it falls directly out of subtracting
// the hydrostatic reference state, and is the single most important
// documented fact about this class.
class BoussinesqBuoyancy {
 public:
  // Throws InvalidArgumentError if referenceDensity is not finite and
  // > 0, if beta is not finite and >= 0 (a negative thermal-expansion
  // coefficient is physically implausible for this model's intended use
  // and not silently accepted), if referenceTemperature is not finite,
  // or if either gravity component is not finite.
  BoussinesqBuoyancy(Real referenceDensity, Real beta, Real referenceTemperature,
                     Vector2 gravity);

  [[nodiscard]] Real referenceDensity() const noexcept;
  [[nodiscard]] Real beta() const noexcept;
  [[nodiscard]] Real referenceTemperature() const noexcept;
  [[nodiscard]] Vector2 gravity() const noexcept;

  // S_b = -referenceDensity * beta * (temperature - referenceTemperature) * gravity,
  // in force-per-unit-volume (matching every other momentum source this
  // codebase's assembly layer already works in -- see
  // assemblePressureSourceContribution's own -V_P*grad(p) convention;
  // callers integrate over cell volume themselves, this function does
  // not). Exactly (0,0) whenever temperature == referenceTemperature,
  // beta == 0, or gravity == (0,0) -- the three documented "zero-buoyancy
  // equivalence" cases (P3-PHYS-001 Phase 5/6), each following directly
  // from the formula with no special-cased branch needed. Throws
  // InvalidArgumentError if temperature is not finite.
  [[nodiscard]] Vector2 source(Real temperature) const;

 private:
  Real referenceDensity_{};
  Real beta_{};
  Real referenceTemperature_{};
  Vector2 gravity_{};
};

}  // namespace cfd::physics
