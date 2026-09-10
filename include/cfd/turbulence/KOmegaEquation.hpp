#pragma once

#include "cfd/fields/ScalarField.hpp"

namespace cfd::turbulence {

// P2-TURB-005: standard k-omega's own effective-diffusivity convention --
// Gamma = mu + sigma*mu_t, the *linear* form, NOT k-epsilon's
// Gamma = mu + mu_t/sigma (TODO.md P2-TURB-005 section 17/34: these two
// conventions differ, and accidentally reusing k-epsilon's formula here
// is exactly the regression this task calls out explicitly). A
// separately-named, separately-tested function for the same reason
// KEpsilonEquation.hpp's computeEffectiveDiffusivity is: an easy-to-
// transcribe-wrong formula deserves its own name and its own hand test,
// not just an inline expression.
//
// Every other piece of k-omega's scalar-transport assembly (diffusion
// contribution given a per-cell diffusivity field, convection, the
// implicit Su+Sp*phi source linearization, and the combining
// assembleScalarTransportEquation) is genuinely model-agnostic and is
// reused directly from KEpsilonEquation.hpp -- see that file's own
// header comment, which already documents this exact reuse as the
// intended purpose of that assembly layer (KOmegaModel.cpp includes
// KEpsilonEquation.hpp and calls assembleScalarDiffusionContribution/
// applyImplicitScalarSource/assembleScalarTransportEquation directly, no
// second copy of any of them exists here).
//
// Throws InvalidArgumentError if molecularViscosity is not finite and
// > 0, sigma is not finite and > 0, or any turbulentViscosity entry is
// not finite or negative -- same validation policy as
// computeEffectiveDiffusivity.
[[nodiscard]] cfd::fields::ScalarField computeLinearEffectiveDiffusivity(
    Real molecularViscosity, const cfd::fields::ScalarField& turbulentViscosity, Real sigma);

}  // namespace cfd::turbulence
