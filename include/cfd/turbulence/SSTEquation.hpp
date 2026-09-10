#pragma once

#include "cfd/core/Types.hpp"

namespace cfd::turbulence {

// P2-TURB-006: every genuinely SST-specific formula, gathered in one
// place, matching this task's own "before coding, explicitly reconcile
// P_k units, rho placement, mu_t placement, alpha production form,
// cross-diffusion form, sigma convention" instruction (section 8) --
// each function below is deliberately small and independently, exactly
// testable, so a unit test can pin down one formula at a time rather
// than only ever exercising the whole blended system together. None of
// these functions know about mesh/fields/BoundaryConditionSet -- they
// take/return plain Real, so SSTModel.cpp is the only place mesh-level
// looping happens.
//
// Units: consistent with cfd::turbulence::TurbulenceModel's own
// dynamic-viscosity convention everywhere `rho`/`mu_t` appear, EXCEPT
// where a function explicitly takes `kinematicViscosity` (F1/F2's own
// `nu`-based terms are inherently kinematic in Menter's original
// formulation -- physics::FluidProperties::kinematicViscosity()).

// alpha = beta/betaStar - sigmaOmega*kappa^2/sqrt(betaStar) -- the
// standard SST consistency relation (Menter 1994) that makes the model
// recover the logarithmic law of the wall for the given (beta, sigmaOmega)
// pair. Used once for set 1 (alpha1, from beta1/sigmaOmega1) and once for
// set 2 (alpha2, from beta2/sigmaOmega2) -- SSTModel.cpp calls this
// directly rather than storing alpha1/alpha2 as separate literal
// coefficients, so there is exactly one formula, never two independently
// hand-copied numbers that could silently drift apart.
//
// Throws InvalidArgumentError if betaStar is not finite and > 0.
[[nodiscard]] Real computeSSTAlpha(Real beta, Real sigmaOmega, Real betaStar, Real kappa);

// phi = F1*phi1 + (1-F1)*phi2 -- section 4's own blending formula,
// reused for every blended coefficient (sigmaK, sigmaOmega, beta,
// alpha). Does not itself validate F1 in [0,1] (F1 is already validated
// where it is computed, computeF1 below); a caller passing an
// out-of-range F1 gets the algebraically-consistent (if unphysical)
// extrapolated result, the same "this is pure algebra, the caller
// supplies physically meaningful inputs" convention as
// KEpsilonEquation.hpp's applyImplicitScalarSource.
[[nodiscard]] Real blendSSTCoefficient(Real F1, Real phi1, Real phi2);

// CDkw = max(2*rho*sigmaOmega2*(gradK . gradOmega)/omega, minimum) --
// the cross-diffusion *coefficient* used only inside F1's own arg1
// (section 11), clamped to a small positive minimum so arg1's own
// division never blows up when grad(k).grad(omega) is negative or zero.
// This is NOT the actual cross-diffusion source term added to the omega
// equation -- see computeCrossDiffusionSource below for that (the
// un-clamped, (1-F1)-weighted quantity that actually enters the RHS).
//
// Throws InvalidArgumentError if omega is not finite and > 0.
[[nodiscard]] Real computeCrossDiffusionCoefficient(Real rho, Real sigmaOmega2, Real omega,
                                                    Real gradKDotGradOmega);

// arg1 = min(max(sqrt(k)/(betaStar*omega*y), 500*nu/(y^2*omega)),
//             4*rho*sigmaOmega2*k/(crossDiffusionCoefficient*y^2))
// -- section 11's F1 blending argument. k is clamped to >= 0 internally
// before the sqrt (a defensive floor -- see SSTModel.cpp's own
// documented positivity policy for where the *stored* k field itself is
// floored; this is an independent, local safety net for this one
// formula). Throws InvalidArgumentError if omega/y/crossDiffusionCoefficient
// are not finite and > 0.
[[nodiscard]] Real computeF1Argument(Real k, Real omega, Real wallDistance,
                                     Real kinematicViscosity, Real rho, Real betaStar,
                                     Real sigmaOmega2, Real crossDiffusionCoefficient);

// F1 = tanh(arg1^4) -- always in [0, 1] for any finite arg1 (tanh's own
// range), so this never needs separate clamping.
[[nodiscard]] Real computeF1(Real arg1);

// arg2 = max(2*sqrt(k)/(betaStar*omega*y), 500*nu/(y^2*omega)) --
// section 12's F2 blending argument, deliberately NOT reusing arg1's own
// value (F2's definition omits the cross-diffusion-protected third
// term entirely; substituting F1 for F2 or vice versa is exactly the
// mistake section 12 warns against). Throws InvalidArgumentError if
// omega/wallDistance are not finite and > 0.
[[nodiscard]] Real computeF2Argument(Real k, Real omega, Real wallDistance,
                                     Real kinematicViscosity, Real betaStar);

// F2 = tanh(arg2^2) -- always in [0, 1].
[[nodiscard]] Real computeF2(Real arg2);

// The actual omega-equation cross-diffusion SOURCE term (section 13):
//   2*(1-F1)*rho*sigmaOmega2*(gradK . gradOmega)/omega
// Deliberately NOT clamped to a positive minimum (unlike
// computeCrossDiffusionCoefficient above, which exists only to protect
// F1's own division) -- the real source term is allowed to be negative
// (grad(k).grad(omega) can be negative), and (1-F1) already suppresses
// it near walls where F1 -> 1, per Menter's own formulation. Throws
// InvalidArgumentError if omega is not finite and > 0.
[[nodiscard]] Real computeCrossDiffusionSource(Real rho, Real sigmaOmega2, Real omega,
                                               Real gradKDotGradOmega, Real F1);

// mu_t = rho*a1*k / max(a1*omega, strainMagnitude*F2) -- the SST eddy-
// viscosity limiter (section 15), replacing standard k-omega's bare
// mu_t = rho*k/omega. k is clamped to >= 0 internally (same local
// defensive floor as computeF1Argument). Throws InvalidArgumentError if
// a1/omega are not finite and > 0, or strainMagnitude is not finite and
// >= 0.
[[nodiscard]] Real computeSSTTurbulentViscosity(Real rho, Real a1, Real k, Real omega,
                                                Real strainMagnitude, Real F2);

// P_k_limited = min(productionK, productionLimiterFactor*betaStar*rho*k*omega)
// -- section 19's production limiter, applied only to the k-equation's
// own source (the omega-equation's production term is computed directly
// as blendedAlpha*rho*S^2 -- algebraically equal to
// blendedAlpha*rho/mu_t*productionK when productionK is *unlimited* and
// mu_t = productionK/S^2, per section 7/8's own "P_k units, mu_t
// placement" reconciliation -- so it is never divided by a limited
// production here at all, sidestepping any risk of dividing by a
// near-zero limited value). Throws InvalidArgumentError if
// productionLimiterFactor/betaStar are not finite and > 0.
[[nodiscard]] Real limitProduction(Real productionK, Real productionLimiterFactor, Real betaStar,
                                   Real rho, Real k, Real omega);

}  // namespace cfd::turbulence
