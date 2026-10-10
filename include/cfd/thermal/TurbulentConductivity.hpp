#pragma once

#include "cfd/core/Types.hpp"
#include "cfd/fields/ScalarField.hpp"
#include "cfd/mesh/Mesh.hpp"

// P16 -- turbulent heat transfer for the eddy-viscosity framework.
//
// THE PHYSICS, ONCE
// -----------------
// An eddy-viscosity model closes the turbulent momentum flux with mu_t. The
// Reynolds analogy closes the turbulent HEAT flux the same way, with a
// turbulent thermal diffusivity tied to it by a single dimensionless number:
//
//   alpha_t = mu_t / (rho Pr_t)                 turbulent thermal diffusivity
//   k_t     = rho c_p alpha_t = mu_t c_p / Pr_t turbulent conductivity
//   k_eff   = k_mol + k_t                       what the energy equation sees
//
// so the transported energy equation becomes
//
//   rho c_p (dT/dt + u.grad T) = div(k_eff grad T) + S_T
//
// Note that rho cancels out of k_t entirely: alpha_t carries a 1/rho and
// k_t = rho c_p alpha_t puts it back. So this formula needs no density, and
// asking for one would be an invitation to pass an inconsistent value.
//
// WHY A SEPARATE, SEPARATELY-TESTED FUNCTION
// The momentum side already does this: TurbulenceModel::effectiveViscosity is
// a small non-virtual helper rather than something each model reimplements, and
// computeEffectiveDiffusivity exists specifically so `mu + mu_t/sigma` -- easy
// to transcribe as `mu_eff/sigma` -- is mechanically verifiable on its own.
// `mu_t c_p / Pr_t` is the same kind of formula: short, easy to get wrong in a
// way no integrated test would localise, and worth pinning by itself.
//
// WHAT IS NOT HERE
// No default Pr_t. The value is resolved in the turbulence configuration and
// passed in; a thermal helper that supplied its own default could silently
// disagree with the configured one, which is exactly the class of defect this
// codebase rejects elsewhere ("reject rather than ignore").
namespace cfd::thermal {

// k_t = mu_t c_p / Pr_t for one cell.
//
// mu_t = 0 returns EXACTLY 0.0, so a laminar cell adds the identity to
// k_mol -- that exactness is what makes the bitwise laminar-compatibility gate
// achievable rather than approximate.
//
// Throws InvalidArgumentError if mu_t is not finite or negative, if c_p is not
// finite or not > 0, or if Pr_t is not finite or not > 0.
[[nodiscard]] Real turbulentConductivity(Real turbulentViscosity, Real specificHeat,
                                         Real turbulentPrandtl);

// alpha_t = mu_t / (rho Pr_t), the diffusivity form. Not used by the energy
// equation (which wants k_eff) but exposed because it is the quantity the
// Reynolds analogy is usually stated in, and because a diagnostic that reports
// alpha_t should not have to re-derive it from k_t and risk a second
// convention.
//
// Throws InvalidArgumentError if mu_t is not finite or negative, or if rho or
// Pr_t is not finite or not > 0.
[[nodiscard]] Real turbulentThermalDiffusivity(Real turbulentViscosity, Real density,
                                               Real turbulentPrandtl);

// k_eff = k_mol + mu_t c_p / Pr_t, per cell, as a field.
//
// All three input fields are read at the SAME index, which is the point: the
// caller is expected to pass k_mol and c_p evaluated at the CURRENT temperature
// iterate, so that k_eff(T) = k_mol(T) + mu_t c_p(T)/Pr_t rather than a
// frozen-property approximation to it.
//
// k_eff >= k_mol structurally, since mu_t >= 0, c_p > 0 and Pr_t > 0. The
// energy assembler independently rejects a non-positive conductivity, so a
// k_mol that was already valid stays valid here.
//
// Throws InvalidArgumentError on any size mismatch against the mesh, or on any
// entry that violates the per-cell contract above.
[[nodiscard]] cfd::fields::ScalarField effectiveConductivityField(
    const cfd::mesh::Mesh& mesh, const cfd::fields::ScalarField& molecularConductivity,
    const cfd::fields::ScalarField& turbulentViscosity,
    const cfd::fields::ScalarField& specificHeat, Real turbulentPrandtl);

}  // namespace cfd::thermal
