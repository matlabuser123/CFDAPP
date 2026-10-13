#pragma once

#include <string_view>

#include "cfd/core/Types.hpp"
#include "cfd/fields/ScalarField.hpp"

// P16 -- near-wall treatment: friction velocity, y+, and the standard high-Re
// wall functions.
//
// WHY THIS EXISTS
// ---------------
// The audit (results/p16/turbulence/audit/audit.md) found that no wallFunction,
// yPlus, uTau or wallShear symbol existed anywhere in production code, and that
// the consequence is not cosmetic: the wall treatment shipped before P16 was
//
//   k-epsilon   k = 0 at the wall, epsilon zero-gradient
//   k-omega     omega zero-gradient
//
// and a standard high-Re k-epsilon CANNOT be integrated to the wall. It is
// derived assuming the first cell sits in the log layer. With k = 0 imposed at
// the wall and a first cell at y+ ~ 7.5, the model was being used where its own
// derivation does not hold -- which is why the measured Re_tau was 120 against
// a target of 180 (a 33% error) and the velocity profile was out by ~14 in u+
// units. Those numbers are preserved verbatim in
// results/p16/turbulence/pre_p16_baseline/.
//
// So this file is not an optional diagnostic bolted onto working models. It
// supplies the near-wall physics the models were missing.
//
// WHAT IS AND IS NOT DECIDED HERE
// -------------------------------
// Everything here is a pure function of its arguments -- no mesh, no fields, no
// state. That is deliberate: each formula below can be checked against its own
// closed form and against the log law directly, without constructing a solve.
// Applying them to a mesh (which cells are wall-adjacent, what the wall-normal
// distance is, how epsilon is fixed in the wall cell) is the caller's job and
// lives with the models.
//
// UNITS follow cfd::turbulence::TurbulenceModel: every viscosity is DYNAMIC
// (Pa*s), never kinematic. Functions that need nu take rho and mu and form it
// themselves, so a caller cannot pass the wrong one silently.

namespace cfd::turbulence {

// The constants the wall treatment is built from.
//
// kappa and E are a PAIR, not independent tunables: B = ln(E)/kappa is the
// log-law intercept, so changing one without the other moves the log law this
// implementation claims to satisfy. kappa = 0.41 with E = 9.793 is the
// Launder-Spalding pairing used by most production codes (B = 5.57).
//
// cMu is the same 0.09 as KEpsilonCoefficients -- it appears here because the
// wall functions need it to form u_tau from k, and duplicating the value would
// let the two drift. A caller using a non-default cMu must pass the same value
// to both.
struct WallFunctionCoefficients {
  Real kappa{0.41};
  Real E{9.793};
  Real cMu{0.09};
};

// B = ln(E)/kappa, the log-law intercept implied by the pair above.
//
// Derived rather than stored so it cannot disagree with kappa and E. The
// acceptance gate compares against THIS log law -- the one the wall function
// itself is built from -- because comparing a model against a different
// convention measures the gap between two conventions, not the quality of the
// solution (see acceptance/methodology.md G3).
[[nodiscard]] Real logLawIntercept(const WallFunctionCoefficients& coefficients);

// --- friction velocity -------------------------------------------------------

// u_tau = sqrt(tau_w / rho). The definition, used for every REPORTED quantity
// (y+, u+, Re_tau) because it is tied to the wall shear the solution actually
// produced. Throws InvalidArgumentError if rho is not finite and > 0, or
// tauW is not finite and >= 0.
[[nodiscard]] Real frictionVelocityFromWallShear(Real wallShearStress, Real density);

// u_tau* = C_mu^(1/4) * sqrt(k). The EQUILIBRIUM estimate, used INSIDE the wall
// function to form tau_w from the turbulence field.
//
// These two are not the same quantity and are deliberately named differently.
// They coincide only in local equilibrium (production = dissipation); their
// disagreement is a useful diagnostic of how far from equilibrium a wall cell
// is, and silently conflating them is a classic way to get a wall function that
// looks right and drifts. Throws if k is not finite and >= 0, or cMu is not
// finite and > 0.
[[nodiscard]] Real frictionVelocityFromTurbulentEnergy(Real turbulentKineticEnergy, Real cMu);

// --- wall coordinates --------------------------------------------------------

// y+ = rho * u_tau * y / mu. Throws if any argument is not finite, if rho or mu
// is not > 0, or if y is negative.
[[nodiscard]] Real yPlus(Real density, Real frictionVelocity, Real wallDistance,
                         Real dynamicViscosity);

// u+ = U / u_tau. Throws if u_tau is not finite and > 0.
[[nodiscard]] Real uPlus(Real velocityMagnitude, Real frictionVelocity);

// --- the two near-wall laws --------------------------------------------------

// u+ = y+, valid in the viscous sublayer.
[[nodiscard]] Real uPlusViscousSublayer(Real yPlusValue);

// u+ = ln(y+)/kappa + B, valid in the log layer. Throws if y+ is not finite
// and > 0.
[[nodiscard]] Real uPlusLogLaw(Real yPlusValue, const WallFunctionCoefficients& coefficients);

// The y+ at which the two laws above intersect -- the conventional boundary
// between "use u+ = y+" and "use the log law".
//
// Solved by bisection rather than hardcoded as 11.25: that familiar number is
// the root for one particular (kappa, E) pair, and hardcoding it would silently
// become wrong for any other. The root is well-behaved (the difference is
// monotonic across the bracket), so bisection to 1e-12 is exact enough that the
// choice of solver is not a modelling decision.
[[nodiscard]] Real viscousLogLayerIntersection(const WallFunctionCoefficients& coefficients);

// --- wall resolution ---------------------------------------------------------

// Which near-wall regime a given y+ falls in.
//
// The bands are the standard ones and they are NOT a smooth grading: the buffer
// layer is where neither law holds, which is precisely why a first cell landing
// there (as every pre-P16 run did, at y+ ~ 7.5-8.2) makes a high-Re wall
// function invalid rather than merely inaccurate.
enum class WallResolution {
  ViscousSublayer,  // y+ <= 5      -- resolve, do not use a wall function
  BufferLayer,      // 5 < y+ < 30  -- NEITHER law holds
  LogLayer,         // 30 <= y+ <= 300 -- the wall function's own domain
  TooCoarse,        // y+ > 300     -- the first cell is outside the log layer
};

[[nodiscard]] WallResolution classifyWallResolution(Real yPlusValue);

[[nodiscard]] std::string_view wallResolutionName(WallResolution resolution);

// The inclusive band a high-Re wall function is valid over. Named constants
// rather than literals at the comparison sites, so the gate and the runtime
// check cannot disagree about what "valid" means.
inline constexpr Real kWallFunctionMinimumYPlus = 30.0;
inline constexpr Real kWallFunctionMaximumYPlus = 300.0;

// True iff a high-Re wall function may be applied at this y+.
[[nodiscard]] bool isWallFunctionResolutionValid(Real yPlusValue);

// --- the Launder-Spalding wall function --------------------------------------

// tau_w = rho * u_tau* * kappa * U_P / ln(E * y*)
//
// where u_tau* = C_mu^(1/4) sqrt(k_P) and y* = rho u_tau* y_P / mu, both formed
// from the wall-adjacent cell's own k. This is the standard high-Re form: the
// wall shear is obtained from the log law rather than from mu*U_P/y_P, which is
// the laminar expression and is what the pre-P16 code was effectively using.
//
// Returns 0 when U_P is 0 (a stationary wall cell carries no shear) rather than
// dividing. Throws InvalidArgumentError if any argument is non-finite, if rho,
// mu or y_P is not > 0, if k is negative, or if the resulting y* is not > 0 --
// the last because ln(E y*) is the denominator and a non-positive y* means the
// inputs cannot describe a wall cell.
//
// NOTE: this does NOT itself check that y* lies in the valid band. That check
// is a separate decision (reject the run, warn, or fall back) which belongs to
// the caller, and burying it here would make the formula untestable outside its
// own validity range -- which is exactly where a test most needs to probe it.
[[nodiscard]] Real wallShearStressLaunderSpalding(Real velocityMagnitude,
                                                  Real turbulentKineticEnergy, Real wallDistance,
                                                  Real density, Real dynamicViscosity,
                                                  const WallFunctionCoefficients& coefficients);

// epsilon_P = C_mu^(3/4) k_P^(3/2) / (kappa * y_P) = u_tau*^3 / (kappa y_P).
//
// In the standard treatment this value is IMPOSED in the wall-adjacent cell --
// epsilon's own transport equation is not solved there, because the equation is
// not valid in a cell that spans the log layer. Throws if y_P is not finite
// and > 0, or k is negative.
[[nodiscard]] Real wallAdjacentDissipation(Real turbulentKineticEnergy, Real wallDistance,
                                           const WallFunctionCoefficients& coefficients);

// P_k = tau_w * u_tau* / (kappa * y_P), the equilibrium production in a
// wall-adjacent cell.
//
// This REPLACES the general mu_t*S^2 production there. The reason is physical,
// not a numerical convenience: S^2 computed from a cell-centre gradient across
// a cell that spans the log layer badly under-resolves dU/dy, so the general
// form under-predicts production exactly where production peaks. Throws if y_P
// is not finite and > 0.
[[nodiscard]] Real wallAdjacentProduction(Real wallShearStress, Real frictionVelocityFromK,
                                          Real wallDistance,
                                          const WallFunctionCoefficients& coefficients);

// omega_P = u_tau* / (sqrt(C_mu) * kappa * y_P), the k-omega/SST counterpart of
// wallAdjacentDissipation above.
//
// Consistent with it by construction: omega = epsilon/(C_mu k) applied to the
// same equilibrium expressions gives exactly this, so a k-omega wall cell and a
// k-epsilon wall cell describe the same near-wall state. Throws if y_P is not
// finite and > 0, or k is negative.
[[nodiscard]] Real wallAdjacentSpecificDissipation(Real turbulentKineticEnergy, Real wallDistance,
                                                   const WallFunctionCoefficients& coefficients);

// k_P = u_tau^2 / sqrt(C_mu), the equilibrium wall-adjacent turbulent kinetic
// energy.
//
// Provided for the fixed-k variant of the treatment and for tests that need the
// inverse of frictionVelocityFromTurbulentEnergy. The default k-epsilon path
// uses a zero-gradient k at the wall together with the modified production and
// imposed epsilon above, which is the standard Launder-Spalding arrangement --
// imposing BOTH k and epsilon over-constrains the wall cell.
[[nodiscard]] Real wallAdjacentTurbulentEnergy(Real frictionVelocity,
                                               const WallFunctionCoefficients& coefficients);

// --- the effective wall viscosity --------------------------------------------

// The viscosity that makes the LAMINAR wall-shear expression reproduce the
// wall-function tau_w:
//
//   mu_wall = tau_w * y_P / U_P
//
// This is how a wall function reaches the momentum equation in a finite-volume
// code: the assembly keeps its existing mu*U_P/y_P boundary term and is handed
// a different mu at that face. Nothing in the momentum discretisation has to
// know a wall function exists.
//
// Returns `dynamicViscosity` unchanged when U_P is 0 -- with no velocity there
// is no shear to match, and the molecular value is the correct limit.
//
// FLOORED AT THE MOLECULAR VISCOSITY, and that floor is physics rather than a
// numerical convenience: mu_wall is mu + mu_t at that face, and mu_t >= 0 is
// the same positivity constraint validateTurbulentViscosityField already
// enforces on every turbulent viscosity in this codebase. A wall function
// returning less than mu would be asserting a negative eddy viscosity.
//
// The floor CAN fire in the buffer layer, where the log law under-predicts the
// shear a laminar wall would carry -- which is one of the concrete reasons a
// first cell must not land there. `floored` reports when it happened so the
// caller can surface it; a floor nobody can observe would be exactly the
// silent clipping this phase set out to remove.
[[nodiscard]] Real effectiveWallViscosity(Real wallShearStress, Real velocityMagnitude,
                                          Real wallDistance, Real dynamicViscosity,
                                          bool* floored = nullptr);

// --- P16b: the automatic (blended) near-wall treatment ----------------------
//
// WHY THE HIGH-Re WALL FUNCTION ALONE IS NOT ENOUGH
// -------------------------------------------------
// The corrected Driver & Seegmiller sequence
// (results/p16/turbulence/separated_flow/) showed the high-Re treatment
// diverging under refinement in separated flow, and the diagnostic pointed at a
// structural cause rather than a bug: at a separation or reattachment point
// tau_w is zero BY DEFINITION, so y+ -> 0 there no matter how the mesh is
// built. A treatment whose validity band is [30, 300] can never be valid at the
// exact points a reattachment benchmark measures. Measured min y+ over the
// corrected sequence fell 30.34 -> 22.73 -> 5.93 as the mesh refined.
//
// So the fix is not a finer mesh and it is certainly not a y+ clamp -- it is a
// treatment whose governing physics CHANGES with the local wall state.
//
// THE FORMULATION: Menter's automatic near-wall treatment
// ------------------------------------------------------
// Menter, Kuntz & Langtry (2003), "Ten years of industrial experience with the
// SST turbulence model" -- the treatment SST is normally paired with, and the
// reason SST is usable on meshes that are not uniformly y+ ~ 1 or uniformly
// y+ ~ 50. Both blends below are that paper's, NOT fitted here: no constant in
// this section was chosen by looking at a Driver & Seegmiller result.
//
//   friction velocity   u_tau = ( u_tau_vis^4 + u_tau_log^4 )^(1/4)
//   specific dissipation  omega_w = sqrt( omega_vis^2 + omega_log^2 )
//
// Both are smooth, monotonic in each argument, bounded above by
// 2^(1/4) and sqrt(2) times the larger branch at the crossover, and reduce to
// each branch exactly in its own limit. The mild crossover overshoot is the
// published behaviour, not an artefact: it is what keeps the transition narrow
// while never letting either branch be badly wrong.
//
// WHY THIS IS FINITE AT SEPARATION -- the property the whole exercise needs
// ------------------------------------------------------------------------
// As U_P -> 0 at a separation or reattachment point:
//   u_tau_vis = sqrt(nu U_P / y) -> 0
//   u_tau_log = C_mu^(1/4) sqrt(k_P) -> 0 with k
//   => tau_w -> 0 smoothly, with no logarithm and no division by U_P;
//   omega_vis = 6 nu / (beta1 y^2) is INDEPENDENT of U and k, so omega_w stays
//   finite and positive however small the shear becomes.
// The high-Re branch cannot do this: ln(E y*) is undefined as y* -> 0.

// Which near-wall treatment a model applies.
enum class WallTreatmentMode {
  // The P16 high-Re wall function, unchanged and still validated by the
  // Re_tau = 590 and Re_tau = 180 channel gates. Correct where the first cell
  // is deliberately placed in the log layer; invalid elsewhere, and says so
  // through its own y+ diagnostics.
  HighReWallFunction,
  // Menter's automatic treatment: viscous sublayer, blend, and log layer,
  // selected by the local wall state rather than by assumption.
  //
  // MEASURED TO REGRESS the Re_tau = 590 channel (E_Re_tau 0.0698 at ny16
  // against an adopted 0.059) because its viscous branch sqrt(nu U/y) presumes
  // a linear profile and stays too large through the buffer layer. Preserved
  // for comparison; see results/p16/turbulence/automatic_wall/.
  Automatic,
  // The same treatment with the wall shear taken from Spalding's composite law
  // solved implicitly, instead of from a 4th-power blend of two independently
  // computed friction velocities. Same omega treatment, same production
  // weighting, same constants.
  //
  // MEASURED to restore E_Re_tau on every Re_tau = 590 mesh but NOT the
  // companion u+ Linf metric, because Spalding's law only becomes this
  // repository's log law for y+ >~ 70 while the meshes sit at y+ = 18-95.
  AutomaticSpalding,
  // The published two-velocity-scale momentum wall flux, F_U = -rho u_tau u*.
  // Same omega blend and same production weighting as the modes above; only
  // the momentum wall flux differs.
  AutomaticMomentumFlux,
};

[[nodiscard]] std::string_view wallTreatmentModeName(WallTreatmentMode mode);

// omega in the viscous sublayer: the analytical near-wall solution
//
//   omega_vis = 6 nu / (beta1 y^2)
//
// NOTE the coefficient. cfd::boundary::WallOmega uses 60 nu/(beta1 y^2), which
// is Menter's (1994) recommended WALL FACE value; this is the sublayer
// solution evaluated at the first CELL CENTRE, and the two play different
// roles. Using 60 here would put the wall-adjacent cell an order of magnitude
// off its own asymptote. Both are Menter's; they are not interchangeable, and
// the distinction is recorded because a reader comparing the two files will
// otherwise think one of them is wrong.
//
// Throws InvalidArgumentError if nu, beta1 or y is not finite and > 0.
[[nodiscard]] Real viscousSublayerSpecificDissipation(Real kinematicViscosity, Real beta1,
                                                      Real wallDistance);

// u_tau in the linear sublayer, from tau_w = mu U_P / y_P:
//
//   u_tau_vis = sqrt( nu |U_P| / y_P )
//
// Defined and zero at U_P = 0, which is exactly the separation point the
// log-law branch cannot represent.
[[nodiscard]] Real viscousFrictionVelocity(Real velocityMagnitude, Real wallDistance,
                                           Real kinematicViscosity);

// u_tau = (u_tau_vis^4 + u_tau_log^4)^(1/4). Throws if either argument is not
// finite and >= 0.
[[nodiscard]] Real blendFrictionVelocity(Real viscous, Real logarithmic);

// omega_w = sqrt(omega_vis^2 + omega_log^2). Throws if either argument is not
// finite and >= 0.
[[nodiscard]] Real blendSpecificDissipation(Real viscous, Real logarithmic);

// Which branch dominates at a given y+, for diagnostics only -- the blend is
// continuous and never switches, so this REPORTS where a face sits rather than
// selecting anything. Reporting it is what replaces the high-Re mode's G1
// precondition for the automatic mode.
enum class WallRegime {
  ViscousSublayer,  // y+ <= 5     -- the low branch dominates
  Blended,          // 5 < y+ < 30 -- neither dominates; this is the point
  LogLayer,         // y+ >= 30    -- the high branch dominates
};

[[nodiscard]] WallRegime classifyWallRegime(Real yPlusValue);
[[nodiscard]] std::string_view wallRegimeName(WallRegime regime);

// Everything the automatic treatment produces for one wall cell.
struct AutomaticWallState {
  Real viscousFrictionVelocity{};
  // The LOG branch of the tau_w blend: sqrt(tau_LS/rho), the Launder-Spalding
  // wall shear the validated high-Re path computes. NOT C_mu^(1/4) sqrt(k) --
  // see this file's note on why that was wrong.
  Real logarithmicFrictionVelocity{};
  // u_tau* = C_mu^(1/4) sqrt(k), the EQUILIBRIUM scale. Distinct from the
  // branch above and not interchangeable with it: omega_log and the
  // equilibrium production are built on this one, the wall shear is not.
  Real equilibriumFrictionVelocity{};
  Real frictionVelocity{};  // the blend
  Real wallShearStress{};   // rho u_tau^2
  Real yPlus{};
  Real viscousSpecificDissipation{};
  Real logarithmicSpecificDissipation{};
  Real specificDissipation{};  // the blend -- imposed in the wall cell
  // omega_log^2 / (omega_vis^2 + omega_log^2): how much the LOG branch
  // counts at this face, in [0, 1]. Not a new constant -- it is the weight
  // the omega blend above already applies, read off so the k production can
  // be blended consistently with the omega that is imposed beside it.
  Real logLayerWeight{};
  Real effectiveViscosity{};  // for momentum
  WallRegime regime{WallRegime::LogLayer};
};

// The automatic treatment for one wall cell.
//
// Total function on its domain: every input combination with y > 0, rho > 0,
// mu > 0, k >= 0 and finite U produces a finite result, INCLUDING U = 0 and
// k = 0 together. There is no branch that can throw on a physically reachable
// state, which is the property the high-Re path lacks and the reason separated
// flow needs this one.
[[nodiscard]] AutomaticWallState automaticWallState(Real velocityMagnitude,
                                                    Real turbulentKineticEnergy, Real wallDistance,
                                                    Real density, Real dynamicViscosity, Real beta1,
                                                    const WallFunctionCoefficients& coefficients);

// --- P16b attempt 4: Spalding's composite law of the wall --------------------
//
// WHY, in one line: blending two independently-computed friction velocities
// put a +19% error into tau_w at y+ = 20, and that error is what regressed the
// Re_tau = 590 channel. A composite law has the right asymptotes BY
// CONSTRUCTION, so there is no crossover weighting left to get wrong.
//
// Spalding (1961). Written as y+ as an explicit function of u+:
//
//   y+ = u+ + e^(-kappa B) [ e^(kappa u+) - 1 - kappa u+ - (kappa u+)^2/2
//                            - (kappa u+)^3/6 - (kappa u+)^4/24 ]
//
// THE CONSTANTS ARE NOT NEW. This repository's log law is u+ = ln(E y+)/kappa,
// i.e. B = ln(E)/kappa, so kappa B = ln E and the prefactor is exactly
//
//   e^(-kappa B) = 1/E
//
// The composite law therefore carries CFDApp's own kappa and E and introduces
// no constant of its own. Nothing here was fitted to a channel or a step.
//
// THE ASYMPTOTES, both exact rather than approximate:
//   u+ -> 0    the bracket is the tail sum_{n>=5} (kappa u+)^n/n!, so
//              y+ -> u+                      (the linear sublayer)
//   u+ -> inf  e^(kappa u+) dominates, so y+ -> e^(kappa u+)/E, i.e.
//              E y+ -> e^(kappa u+) and u+ -> ln(E y+)/kappa
//              (this repository's log law, exactly)
//
// Throws InvalidArgumentError if uPlus is not finite and >= 0.
[[nodiscard]] Real spaldingYPlus(Real uPlus, const WallFunctionCoefficients& coefficients);

// Spalding's law solved implicitly for u_tau, given the wall-adjacent cell's
// velocity and distance.
//
// u+ = |U_P|/u_tau and y+ = u_tau y_P/nu, so substituting into the law above
// leaves one equation in one unknown:
//
//   F(u_tau) = spaldingYPlus(|U_P|/u_tau) - u_tau y_P/nu = 0
//
// F is STRICTLY DECREASING in u_tau (spaldingYPlus increases with u+, u+ falls
// with u_tau, and the subtracted term rises), F -> +inf as u_tau -> 0 and
// F -> -inf as u_tau -> inf. So the root exists, is unique, and bisection
// cannot fail -- no initial guess, no convergence tuning, no iteration limit
// that can silently return a wrong answer.
//
// THE BRACKET IS EXACT, not a guess. Spalding's law has y+ >= u+ for every
// u+ >= 0 (the bracketed tail is a sum of non-negative terms), hence
// |U|/u_tau <= u_tau y/nu, hence
//
//   u_tau >= sqrt(nu |U| / y)
//
// -- the viscous friction velocity is a rigorous LOWER bound, with equality
// only in the linear limit. The upper bound is found by doubling, which
// terminates because F -> -inf.
//
// DEFINED AT SEPARATION: |U_P| = 0 returns exactly 0, so this keeps the one
// property the whole automatic treatment exists for, and keeps it without any
// blending.
//
// Throws InvalidArgumentError if U is not finite, or y/nu are not finite > 0.
[[nodiscard]] Real spaldingFrictionVelocity(Real velocityMagnitude, Real wallDistance,
                                            Real kinematicViscosity,
                                            const WallFunctionCoefficients& coefficients);

// The composite-law counterpart of automaticWallState.
//
// Identical in every respect EXCEPT the wall shear, which comes from Spalding's
// law solved implicitly rather than from blending two friction velocities.
// omega, the branch report and the log-layer weight are produced by
// automaticWallState itself, so there is one implementation of those, not two.
//
// logLayerWeight and the omega branches keep their meaning: the one-cell
// diagnostic showed the omega blend is benign (within 11% of the validated
// value at y+ = 20, 5% at y+ = 30) while the SHEAR blend carried the whole
// regression, so only the shear is replaced here.
[[nodiscard]] AutomaticWallState compositeWallState(Real velocityMagnitude,
                                                    Real turbulentKineticEnergy, Real wallDistance,
                                                    Real density, Real dynamicViscosity, Real beta1,
                                                    const WallFunctionCoefficients& coefficients);

// --- P16b attempt 5: the two-velocity-scale momentum wall flux ---------------
//
// THE STRUCTURAL POINT. Every automatic attempt so far collapsed the wall shear
// onto a SINGLE scale:
//
//     tau_w = rho u_tau^2
//
// The published omega-based automatic wall treatment does not do that. Its
// momentum wall flux uses TWO distinct velocity scales:
//
//     F_U = -rho u_tau u*
//
// where u_tau blends the viscous and logarithmic FRICTION velocities, and u* is
// a separate scale blending the viscous-gradient scale with the turbulence
// k-scale. Identifying the two is what this file previously did, and the
// diagnostics traced the channel regression to exactly that conflation.
//
// PROVENANCE, stated plainly: the STRUCTURE above (F_U = -rho u_tau u*, the
// 4th-power blend for u_tau, and u* as a separate blend of the viscous-gradient
// and k scales) was supplied as the reference formulation. The individual scale
// DEFINITIONS below are this implementation's reading of it, chosen so that both
// asymptotic limits come out exactly right; they are not transcribed from a
// document this repository can cite. The two limit tests are therefore the
// specification that matters, and they are proved numerically:
//
//   y+ -> 0    F_U -> -mu |U_P| / y_P        (the resolved viscous wall)
//   y+ -> inf  F_U -> -tau_LaunderSpalding   (the VALIDATED high-Re path)
//
// The second limit is the property attempts 2 and 4 both failed to hold at
// moderate y+, and it is why this form is worth testing.
//
// THE SCALES, kept deliberately separate and separately named:
//
//   viscousVelocityScale      = sqrt(nu |U_P| / y_P)      -- shared by both blends
//   turbulenceVelocityScale   = C_mu^(1/4) sqrt(k)        -- u_tau*, k-based
//   logFrictionVelocity       = |U_P| / u+(y*)            -- log-law inversion
//   wallFrictionVelocity      = (visc^4 + log^4)^(1/4)    -- u_tau
//   momentumWallVelocityScale = (visc^4 + turb^4)^(1/4)   -- u*
//
// There is no circularity: u* depends only on the two scales above it, and y*
// is formed from u*, so u_tau follows in one pass with no iteration.
//
// A DEGENERACY WORTH KNOWING. At exact equilibrium the turbulence scale equals
// the log friction velocity (u_tau* = |U|/u+), so the two blends coincide and
// F_U collapses to rho u_tau^2 -- i.e. to the earlier blended form. This
// formulation can therefore only differ from that one where the wall cell is
// OFF equilibrium. Tests that use an equilibrium k cannot discriminate between
// them, and the ones below are built off-equilibrium for that reason.
struct AutomaticMomentumWallState {
  Real viscousVelocityScale{};
  Real turbulenceVelocityScale{};
  Real logFrictionVelocity{};
  Real wallFrictionVelocity{};       // u_tau
  Real momentumWallVelocityScale{};  // u*
  Real yStar{};                      // rho u* y / mu
  Real wallShearStress{};            // rho u_tau u*  -- NOT rho u_tau^2
  Real effectiveViscosity{};         // mu_w, see below
};

// mu_w is DERIVED, not posited. CFDApp's momentum discretisation applies a
// boundary diffusive flux of the form
//
//     F_U = -mu_w |U_P| / y_P
//
// so equating that with -rho u_tau u* gives
//
//     mu_w = rho u_tau u* y_P / |U_P|
//
// Dimensions: [kg/m^3][m/s][m/s][m]/[m/s] = kg/(m s) = Pa s. The sign is left
// to the momentum assembly, which already applies it from the velocity
// direction; only the magnitude is produced here.
//
// Throws InvalidArgumentError if U is not finite, or k/y/rho/mu are outside
// their domains.
[[nodiscard]] AutomaticMomentumWallState automaticMomentumWallState(
    Real velocityMagnitude, Real turbulentKineticEnergy, Real wallDistance, Real density,
    Real dynamicViscosity, const WallFunctionCoefficients& coefficients);

}  // namespace cfd::turbulence
