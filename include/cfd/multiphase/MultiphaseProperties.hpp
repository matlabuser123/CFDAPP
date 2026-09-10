#pragma once

#include "cfd/core/Types.hpp"
#include "cfd/fields/ScalarField.hpp"
#include "cfd/mesh/Mesh.hpp"
#include "cfd/multiphase/PhaseProperties.hpp"

namespace cfd::multiphase {

// P3-PHYS-005: a validated two-phase system (exactly two phases, this
// task's own mandatory scope -- section 3's "unsupported number of
// phases" is rejected implicitly by this class's own two-argument
// constructor, not a configurable N). Owns the linear volume-fraction
// mixture laws (sections 7-8):
//   rho_mix(alpha) = alpha*rho_1 + (1-alpha)*rho_2
//   mu_mix(alpha)  = alpha*mu_1  + (1-alpha)*mu_2
// where alpha is phase 1's volume fraction (alpha=1 -> pure phase 1,
// alpha=0 -> pure phase 2, per this task's own section 1 convention).
// Documented explicitly (section 8's own instruction) as the *initial*
// mixture rule for this foundation, not a universal physical law for
// every multiphase system (e.g. it is not appropriate for a
// highly-non-Newtonian dispersed phase) -- a different mixing law can be
// added later as a sibling method without touching momentum assembly,
// the same "swap the model, not the equation" separation
// cfd::physics::TemperatureProperty already established for
// temperature-dependent properties.
class TwoPhaseSystem {
 public:
  TwoPhaseSystem(PhaseProperties phase1, PhaseProperties phase2);

  [[nodiscard]] const PhaseProperties& phase1() const noexcept;
  [[nodiscard]] const PhaseProperties& phase2() const noexcept;

  // Throws InvalidArgumentError if alpha is not finite. Deliberately does
  // NOT reject or clamp alpha outside [0,1] -- the linear mixture formula
  // is well-defined for any finite alpha, and boundedness is a reporting
  // concern (section 14's own "do not blindly clamp... as the primary
  // correctness strategy"), handled separately by
  // VolumeFractionEquation.hpp's own volumeFractionBounds diagnostic, not
  // by silently rejecting a mildly out-of-range transport result here.
  [[nodiscard]] Real mixtureDensity(Real alpha) const;
  [[nodiscard]] Real mixtureViscosity(Real alpha) const;

 private:
  PhaseProperties phase1_;
  PhaseProperties phase2_;
};

// Evaluates TwoPhaseSystem::mixtureDensity/mixtureViscosity at every
// cell's current alpha, mirroring
// cfd::physics::evaluatePropertyField's role for temperature-dependent
// properties (P3-PHYS-003) -- the per-cell field these functions return
// is what momentum's *existing* field-based diffusion overload
// (physics::assembleDiffusionContribution(mesh, ScalarField
// effectiveViscosity, ...), P2-TURB-003) consumes directly for
// mu_mix (section 19's own "use mixture viscosity through the existing
// momentum diffusion path"). rho_mix is deliberately NOT wired into any
// pressure-velocity/continuity consumer by this task (section 20's own
// "do not accidentally implement P3-PHYS-006 here") -- evaluated and
// validated standalone only.
//
// Throws InvalidArgumentError if alpha.size() != mesh.numberOfCells(), or
// any alpha value is not finite.
[[nodiscard]] cfd::fields::ScalarField evaluateMixtureDensityField(
    const cfd::mesh::Mesh& mesh, const cfd::fields::ScalarField& alpha,
    const TwoPhaseSystem& system);
[[nodiscard]] cfd::fields::ScalarField evaluateMixtureViscosityField(
    const cfd::mesh::Mesh& mesh, const cfd::fields::ScalarField& alpha,
    const TwoPhaseSystem& system);

}  // namespace cfd::multiphase
