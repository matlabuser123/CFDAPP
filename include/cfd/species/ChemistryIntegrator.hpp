#pragma once

// G8 closure, step 3 -- THE CHEMISTRY INTEGRATOR.
//
// WHY P18.6'S STEP IS NOT ENOUGH. `advanceClosedReactor` takes ONE explicit step and REFUSES one
// that would drive any `rho Y_i` negative, with its own comment saying why: "there is no chemistry
// integrator in P18.6 scope, so an invalid explicit step is an error, not something to normalise
// away". That is the right call for P18.6 and useless for a flame. In a premixed front the
// chemical time scale is orders of magnitude shorter than the transport time scale a flow solver
// wants to march on, so a transport-sized `dt` handed to that step is refused -- correctly.
//
// WHAT THIS ADDS, and deliberately nothing more: adaptive SUB-STEPPING of the qualified step. The
// transport solver asks for a `dt`; this subdivides it until every sub-step is one the qualified
// step accepts, and reports how it got there.
//
// WHY NOT AN IMPLICIT INTEGRATOR. Backward Euler on the chemistry needs `d(omega)/dY` -- a kinetics
// Jacobian this codebase does not have (P18.4's own note: "linearising omega_i in Y_i needs
// d(omega)/d(Y), a kinetics derivative P18.4 does not have"). Building one plus a Newton solve is a
// larger, separately-gateable piece of numerics. For the ONE-STEP irreversible mechanism G8
// freezes, the reaction progress is monotone and a sub-stepped explicit march with local control is
// sufficient and far cheaper to qualify. If a stiffer mechanism is ever wanted, THAT is when an
// implicit integrator and its gate are justified -- not now, on speculation.
//
// WHAT IT WILL NOT DO:
//   * it does not clip, clamp or renormalise anything. P18's invariant is that an invalid state is
//     REFUSED, and this integrator's response to a refusal is to take a smaller step, never to
//     repair the state.
//   * it does not reuse stale rates. Progress rates are recomputed from the CURRENT state at every
//     sub-step; reusing them across a sub-step is exactly the defect controls N7/N8 exist to catch.
//   * it does not silently give up. Exhausting the sub-step budget THROWS, naming the budget, the
//     remaining time and the cell that forced the smallest step.
//
// WHICH REFUSALS THE RETRY PATH ACTUALLY DEFENDS, stated because it is narrower than it looks.
// The dominant refusal -- "the explicit step would drive rho*Y negative" -- is UNREACHABLE under
// this limiter, by construction and not by luck: the sub-step is at most
// `maximumFractionalChange * rho Y_i / |omega_i|` with `maximumFractionalChange <= 1`, and the
// qualified step applies the SAME frozen rates that produced that estimate. So the update is
// `rho Y - dt |omega| >= rho Y - (rho Y / |omega|) |omega| = 0`: it can reach exactly zero and
// cannot pass it. Measured at `maximumFractionalChange = 1.0` in
// test_chemistry_integrator.cpp: 0 refusals.
//
// The retry path therefore defends the OTHER refusal modes -- a non-finite `S_E`, energy or
// recovered temperature, a recovered `T <= 0`, or a temperature outside the bracket -- which the
// one-step constant-`cp` mechanism G8 freezes does not reach. It is DEFENSIVE AND UNEXERCISED by
// that mechanism, and is recorded as such rather than claimed as tested: a mechanism with
// temperature-ranged `cp` or a stiffer rate can reach them, and the branch must work when it does.
// Its exception type is pinned by a test for exactly that reason.
#include <cstddef>
#include <string>
#include <vector>

#include "cfd/core/Types.hpp"
#include "cfd/fields/ScalarField.hpp"
#include "cfd/species/ArrheniusKinetics.hpp"
#include "cfd/species/HeatRelease.hpp"
#include "cfd/species/MassFractionState.hpp"
#include "cfd/species/MixtureThermodynamics.hpp"
#include "cfd/species/ReactionMechanism.hpp"
#include "cfd/species/SpeciesRegistry.hpp"

namespace cfd::species {

// What limits a sub-step. Reported, not inferred, because "it took 4000 sub-steps" and "it took
// 4000 sub-steps because every one was rejected and halved" are different situations.
enum class SubStepLimit {
  TargetReached,  // the remaining time was smaller than the estimate: the last sub-step
  ChemicalScale,  // the fractional-change estimate bounded it
  Refusal,        // the qualified step refused and the sub-step was halved
};

struct ChemistryIntegratorSettings {
  // TD-002: carry the per-species round-off across sub-steps.
  //
  // WHY IT DEFAULTS TO TRUE. Without it this integrator has a measured defect that makes it
  // unusable for the thing it exists to do. `sum_i Y_i` walks away from 1 linearly at ~0.006
  // eps/step, and MassFractionState -- which validates EVERY sub-step -- refuses the state once
  // that crosses P18.1's 4*eps*N, at roughly 1333 steps. So a long march does not return a drifted
  // answer, it THROWS; and tightening `maximumFractionalChange` below 0.05 on a 20 s dt made this
  // integrator throw before TD-002 (reproducer.md V1). An accuracy control that cannot be tightened
  // is not an accuracy control, so the compensated march is the default and the plain one is the
  // control.
  //
  // It is not clipping, not normalisation and not a loosened tolerance -- the three things TD-002's
  // authorization forbids. All three act ON the sum; this never touches the sum. It carries the
  // low-order bits each store could not represent into the next increment, so nothing is discarded
  // and `sum_i Y_i = 1` follows from `sum_i omega_i = 0` instead of being imposed. Measured: 0.00
  // eps after 100000 steps, against 4241 eps for the plain march.
  //
  // Setting it false reproduces the pre-TD-002 arithmetic exactly, which is what negative control
  // N1 needs -- so the control runs the same binary as production rather than a separate build.
  bool compensateRoundOff{true};

  // A sub-step may consume at most this fraction of any cell's smallest species mass. 0.1 means a
  // species may lose at most ~10% of itself in one sub-step.
  //
  // THIS IS A STABILITY CONTROL, NOT AN ACCURACY TOLERANCE. It bounds the explicit step against
  // the local chemical time scale `tau = rho Y_i / |omega_i|`. Accuracy is established by
  // refinement -- halving it must reduce the error -- which is how P18.7's batch reactor is gated
  // and how this one is too.
  Real maximumFractionalChange{0.1};

  // Hard ceiling on sub-steps for one call. Exceeding it THROWS rather than returning a partially
  // advanced state: a caller that received `t + 0.6*dt` believing it got `t + dt` would corrupt
  // every conservation balance downstream.
  std::size_t maximumSubSteps{100000};

  // How many times one sub-step may be halved after a refusal before giving up. A refusal means
  // the qualified step judged the state invalid; if halving 30 times does not help, the problem is
  // not the step size.
  std::size_t maximumHalvings{30};
};

struct ChemistryIntegratorResult {
  ReactorState state;

  // TD-002: max over the march of |sum_i Y_i - 1|, as achieved. Reported rather than left to the
  // caller to recompute, because the whole point of the item is that this quantity stays inside
  // P18.1's invariant over a march long enough to be useful -- and a number that is gated should be
  // visible without re-deriving it. Compare against massFractionSumTolerance(speciesCount).
  Real worstSumDeviation{};

  std::size_t subSteps{};        // how many qualified steps were actually taken
  std::size_t refusals{};        // how many were refused and retried smaller
  Real smallestSubStep{};        // the smallest dt actually accepted
  Real largestSubStep{};         // the largest
  Real worstFractionalChange{};  // max over the march of |dY_i| / Y_i, as achieved
  SubStepLimit lastLimit{SubStepLimit::TargetReached};
};

// Integrate the closed, homogeneous, adiabatic reactor from `state` over `dt`.
//
// Throws InvalidArgumentError if `dt` is not finite or not > 0, or if the settings are out of
// range -- faults in the ARGUMENTS, which no different step size can fix.
//
// Throws NumericalError if the sub-step budget is exhausted, or if a sub-step is still refused
// after `maximumHalvings` -- numerical failures on valid arguments. The message names the budget,
// the time remaining and the step that failed. The split is not cosmetic: a caller may sensibly
// respond to NumericalError by asking for a smaller `dt`, and must never respond that way to
// InvalidArgumentError.
//
// On return the state has advanced by EXACTLY `dt`: the sub-steps sum to it, with the final one
// taking whatever remains, so no caller has to reconcile a partial advance.
// ===================================================================================
// THE RAW-FIELDS PATH -- TD-002 scope (b). Everything above takes a validated ReactorState.
// ===================================================================================
//
// WHY A SECOND ENTRY POINT EXISTS, and why it is REQUIRED rather than a convenience.
//
// TD-001 wired a one-step operator-split reacting advance into ProjectRunner and recorded why it
// could not be a march: the chemistry half of step 2 would have to start from step 1's TRANSPORTED
// composition, and a transported composition closes only to the LINEAR SOLVER's tolerance --
// measured 5.718198e-08, which is eleven orders coarser than the 4*eps*N that MassFractionState's
// constructor enforces. So `ReactorState` cannot be built from it at all, and there is no baseline
// path by which a second step could run (reproducer.md V3).
//
// This is the same wall P18.2 hit for the iterate path and answered with
// `arrheniusProgressRatesFromFields`, whose own comment states the policy this follows: the
// invariant is enforced at STEP BOUNDARIES, where it must hold; an iterate is a working vector.
// A transported composition is exactly such a working vector.
//
// WHAT IT MUST NOT DO, and what gate criterion C9 measures. It must NOT normalise the composition
// it is handed. Normalising would make every downstream conservation figure meaningless -- the
// inherited transport closure that TD-001 root-caused to the FLOW solver's residual continuity
// error would silently disappear, and a reader would conclude the composition closes to machine
// precision when it does not. So this carries the closure it is given: the returned fields deviate
// from 1 by approximately the input deviation, NOT by ~eps. C9 gates exactly that
// (`abs(sum Y_out - 1) >= 0.5 * abs(sum Y_in - 1)`), with a normalising variant as control N2.
//
// The composition is still REFUSED if it is not a plausible working vector at all -- non-finite, or
// negative beyond what round-off explains -- because those are faults no step size can fix.
struct RawFieldsChemistryResult {
  std::vector<cfd::fields::ScalarField> composition;  // species-index order
  cfd::fields::ScalarField temperature;

  std::size_t subSteps{};
  std::size_t refusals{};
  Real worstSumDeviationIn{};   // what was handed in
  Real worstSumDeviationOut{};  // what came back -- C9 compares the two
  Real minMassFraction{};
};

[[nodiscard]] RawFieldsChemistryResult integrateChemistryFromFields(
    const ReactionMechanism& mechanism, const MixtureThermodynamics& mixture,
    const FormationEnthalpies& formation, const ReactionKinetics& kinetics,
    const SpeciesRegistry& registry, const std::vector<cfd::fields::ScalarField>& composition,
    const cfd::fields::ScalarField& temperature, const cfd::fields::ScalarField& density, Real dt,
    const ChemistryIntegratorSettings& settings = ChemistryIntegratorSettings{});

[[nodiscard]] ChemistryIntegratorResult integrateChemistry(
    const ReactionMechanism& mechanism, const MixtureThermodynamics& mixture,
    const FormationEnthalpies& formation, const ReactionKinetics& kinetics,
    const ReactorState& state, Real dt,
    const ChemistryIntegratorSettings& settings = ChemistryIntegratorSettings{});

// The local chemical time scale, min over cells and species of `rho Y_i / |omega_i|`, and where it
// occurred. Exposed because a caller choosing a transport `dt` needs to know how stiff the
// chemistry currently is, and because a flame-speed study must be able to REPORT the stiffness it
// integrated through rather than assert it was fine.
//
// Returns an infinite scale when every source is zero -- the non-reacting limit, where no chemical
// step restriction applies at all.
struct ChemicalTimeScale {
  Real smallest{};     // seconds; infinite when nothing reacts
  Index cell{};        // where
  Index species{};     // which species set it
  bool anyReaction{};  // false => nothing is reacting anywhere
};

[[nodiscard]] ChemicalTimeScale chemicalTimeScale(const ReactionMechanism& mechanism,
                                                  const MixtureThermodynamics& mixture,
                                                  const ReactionProgressRates& rates,
                                                  const ReactorState& state);

}  // namespace cfd::species
