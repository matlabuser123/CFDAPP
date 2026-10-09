# P18 — verification gates

```text
FROZEN 2026-09-29, before any P18.1 implementation exists.
DERIVED FROM: results/p18/audit/*.md -- the gates below are shaped by what the audit found, not by
the generic list P18.0's authorization suggested. Where a suggested gate turned out to be already
satisfied, or to need a different criterion than expected, that is stated.
DRY-RUN: section 13. Every gate was run or reasoned against the CURRENT tree before freezing.
```

## Rule on thresholds, applied throughout (§30)

```text
Every numerical threshold below is DERIVED before measurement, from one of:
    an exact algebraic identity        -> BITWISE or a stated round-off multiple of eps
    a round-off floor                  -> k * eps * N, with N and k both named
    an existing frozen tolerance       -> inherited verbatim and cited
    an analytical solution             -> the discretization's own order
    an independent reference           -> computed OUTSIDE CFDApp

NO THRESHOLD MAY BE SET AFTER SEEING THE MEASUREMENT IT JUDGES. Where no defensible threshold can be
derived, the quantity is REPORTED and the gate rests on a derivable part instead -- the convention
P17's G6(f) established.

eps = 2.220446049250313e-16 (double). N_cells and N_species are named per criterion.
```

---

## G1 — baseline / regression preservation

```text
purpose      P18 must not change what already works. Species is a SHIPPED production feature with
             two case directories, a strictly-validated schema and export columns users may depend
             on -- so preservation here is a user-facing contract, not an internal nicety.
required     the pre-P18 baseline reproduced, and the two shipped species cases unchanged.
measured     (a) the three-configuration regression, counts against the frozen P18.0 baseline
             (b) cases/species_diffusion and cases/heated_species_diffusion, field by field
             (c) the P13-001 SIMPLE fingerprint
acceptance   (a) release_cuda 2712 passed / 0 failed / 49 disabled
                 cpu_only     2493 passed / 0 failed / 47 disabled
                 debug_gui    2770 passed / 4 failed (the inherited timeouts) / 49 disabled
                 DERIVED: these are the P18.0 measured baseline. Any deviation is a P18 finding.
             (b) BITWISE on every exported species field.
                 DERIVED, not aspirational: P14's Arithmetic face interpolation of a UNIFORM
                 diffusivity field reproduces the constant-D operator exactly, and both stacks
                 already consume the same massFlux. A migration that preserves the operator must
                 therefore preserve the answer bit for bit. If it does not, the operator changed.
             (c) BITWISE, empty diff, 10 cases / 130 lines.
negative     the fingerprint instrument must detect a planted one-character perturbation
control      (P17.8/P17.11 precedent: 112 differing lines). For (b), a deliberate 1-ULP change to
             one species field must break the comparison.
evidence     results/p18/qualification/ (later phases); baseline at results/p18/baseline/
failure      STOP. A preservation failure is not traded against new capability.
```

## G2 — species-state validity

```text
purpose      a multi-species state must be a valid mixture, not N unrelated scalars that happen to
             be stored together. THIS IS NEW: today nothing enforces any of it (capability_matrix
             rows 3, 4).
required     a species collection with stable identity and enforced invariants.
measured     (a) sum_i Y_i = 1 at every cell
             (b) 0 <= Y_i <= 1 at every cell
             (c) stable species identity: name -> index mapping is bijective and order-independent
             (d) rejection, not repair, of an invalid state
acceptance   (a) |sum_i Y_i - 1| <= 4 * eps * N_species per cell.
                 DERIVED: a sum of N_species doubles each <= 1 accumulates at most ~N_species/2 eps
                 of round-off; 4x that is a bound, not a fitted number. NOT zero, because a
                 normalised set cannot be exactly 1 in binary floating point for general N.
             (b) boolean, no tolerance. A mass fraction is in [0,1] or it is not.
             (c) boolean.
             (d) an inadmissible state must be REFUSED with a named error, never clamped.
                 INHERITED POLICY, not invented here: SpeciesEquation.hpp already refuses to clip
                 ("boundedness violation is a numerical-quality signal a caller reports, not
                 something silently patched over"), and P17.5's recover() refuses rather than
                 flooring. G2 keeps that.
negative     * a state with sum(Y) = 1.01 must FAIL (a)
control      * a state with Y_i = -1e-12 must FAIL (b) -- a boolean has no tolerance to hide in
             * silently normalising instead of refusing must FAIL (d)
evidence     results/p18/multi_species_state/
phase        P18.1
```

## G3 — species conservation

```text
purpose      the transported quantity must be conserved, including with sources.
required     a closed global balance per species, and across species.
measured     for each species i, the integral balance
                 d/dt INT rho Y_i dV + OINT rho Y_i U.n dA + OINT J_i.n dA - INT omega_i dV
             plus sum_i of the same, which must vanish identically for a closed mixture.
acceptance   closed domain, zero flux:  |imbalance| / |total species mass| <= 8 * eps * N_cells
                 DERIVED: inherited verbatim from P17.8's conservation floor, which used the same
                 8 eps N_cells form for the same reason -- a sum over N_cells cells accumulates
                 O(N_cells) eps, and 8 is the margin P17.8 justified and measured against.
             open domain, steady:  |in - out - source| / |in| <= 8 * eps * N_cells
             sum over species:  the mixture imbalance must be <= the worst single-species
                 imbalance (it cannot be better than its parts, and must not be worse).
negative     * source sign reversed (S -> -S) must FAIL
control      * massFlux omitted from convection must FAIL
             * one species' flux dropped from the mixture sum must FAIL the sum criterion while
               the per-species criteria still pass -- this is the control that the MIXTURE
               criterion is not merely a restatement of the per-species ones
evidence     results/p18/conservation/
phase        P18.7 (per-species steady conservation already passes today -- see the dry-run)
```

## G4 — transport / diffusion correctness

```text
purpose      the discretized species equation must be right, and its boundary treatment EXACT.
required     analytical validation plus an exact gradient boundary.
measured     (a) 1D slab diffusion against the analytical profile, 3 grids
             (b) advection-diffusion at two Peclet numbers against the analytical profile
             (c) THE GRADIENT BOUNDARY: outer Picard iterations to convergence on a pure-Neumann
                 problem
             (d) a constant field gives exactly zero diffusive flux
acceptance   (a),(b) the existing frozen tolerances, INHERITED VERBATIM from
                 tests/integration/species/ -- they are already analytical comparisons at three
                 grids and two regimes, and P18 has no reason to loosen them.
             (c) <= 2 outer iterations.
                 DERIVED FROM A MEASUREMENT OF THE FIXED PATH, not chosen: the thermal solver,
                 which has the exact gradient assembly, converges the identical problem in exactly
                 2 (one to solve, one to confirm no change). Species today takes 335. The criterion
                 is "match the path that is already correct", which is why 2 and not "fewer than
                 335".
             (d) EXACTLY 0, bitwise. An exactly representable constant field has an exactly zero
                 gradient; any non-zero result is an asymmetry in the face coefficients.
negative     * re-introduce today's lagged boundary -- must FAIL (c). THIS CONTROL IS FREE: it is
control        the current behaviour, measured at 335 iterations in
               results/p18/logs/11_lag_probe.log.
             * diffusion disabled (D -> 0 with a gradient present) must FAIL (a)
             * massFlux omitted must FAIL (b)
evidence     results/p18/transport/
phase        P18.2
note         (c) CANNOT BE INHERITED BY MIGRATING TO P14. P14's ScalarTransport carries the same
             lag (ScalarTransport.cpp:129-133). Whichever stack P18.2 builds on, the exact
             treatment must be applied deliberately. See audit/p14_reuse.md section 4.
```

## G5 — mixture thermodynamic closure

```text
purpose      mixture properties must satisfy their defining identities exactly, not approximately.
required     the algebraic closure, before any reacting result is claimed.
measured     1 / W_mix = sum_i (Y_i / W_i)
             R_mix     = R_u / W_mix
             cp_mix    = sum_i Y_i cp_i(T)
             cv_mix    = cp_mix - R_mix
             gamma_mix = cp_mix / cv_mix
             h_mix     = sum_i Y_i h_i(T)
             rho       = p / (R_mix T)
             and the single-species LIMIT: with one species at Y = 1, every mixture quantity must
             reduce to P17's existing single-species value.
acceptance   each identity to <= 8 * eps * scale, with scale the magnitude of the quantity.
                 DERIVED: these are closed-form algebraic identities evaluated in double
                 precision; 8 eps allows a handful of roundings per expression and nothing more.
             THE SINGLE-SPECIES LIMIT IS BITWISE. With N_species = 1 and Y = 1, sum_i Y_i cp_i is
                 1.0 * cp_1, and multiplication by exactly 1.0 is exact -- so there is no
                 round-off to allow for, and anything other than bitwise means the mixture path is
                 not a generalisation of the existing one.
negative     * a mixture of one species that does NOT reproduce P17's value bitwise must FAIL
control      * W_i omitted from W_mix (i.e. arithmetic instead of harmonic averaging) must FAIL
             * cp_mix computed as a simple mean rather than mass-weighted must FAIL
evidence     results/p18/mixture_properties/
phase        P18.3
note         TotalEnergyState currently REFUSES a temperature-dependent cp, and its own error
             message names the prerequisite: "supporting it requires a variable-gamma EOS". That
             refusal must be lifted deliberately as part of this gate, not deleted as an obstacle.
```

## G6 — reaction-rate correctness

```text
purpose      reaction rates must match an INDEPENDENT reference, not CFDApp's own output.
required     rate evaluation verified outside CFDApp.
measured     omega_i for a known mechanism against an independent arbitrary-precision
             implementation (Python/mpmath, no CFDApp code), over a matrix of T, p and composition
             spanning the intended range.
acceptance   relative error <= 32 * eps * scale on each rate.
                 DERIVED: an Arrhenius rate is exp(-Ea/RT) times a power law -- a handful of
                 transcendental evaluations, each correctly rounded, so tens of eps is the
                 achievable floor. 32 eps is P17.7's own frozen multiple for the same kind of
                 pointwise closed-form comparison, inherited rather than invented.
             stoichiometric closure: sum_i W_i nu_i = 0 EXACTLY for each reaction (mass is
                 conserved by the stoichiometry itself), to <= 4 eps * max|W_i nu_i|.
negative     * Arrhenius exponent sign flipped must FAIL
control      * activation energy set to 0 must FAIL (removes the T dependence entirely)
             * a stoichiometric coefficient perturbed must FAIL the closure criterion
evidence     results/p18/reactions/ + the independent reference script preserved under tools/
phase        P18.5
```

## G7 — reacting energy conservation

```text
purpose      heat release must be consistent with the species it consumes -- the two must be the
             same reaction.
required     a closed energy balance including the reaction source.
measured     total enthalpy (or total energy) balance with q = -sum_i h_f,i omega_i, and the
             cross-check that the heat released equals the enthalpy change implied by the species
             consumed.
acceptance   |energy imbalance| / |total energy| <= 8 * eps * N_cells, the same derived floor as
                 G3 and for the same reason.
             adiabatic closed reactor: the final temperature must match the adiabatic flame
                 temperature computed from an INDEPENDENT equilibrium/enthalpy balance, to the
                 discretization's own accuracy -- REPORTED with its order, not gated on an invented
                 constant (P17's G6(f) convention).
negative     * heat-release sign flipped (exothermic <-> endothermic) must FAIL
control      * the reaction rate fed to species but NOT to energy must FAIL -- this is the control
               that the two are actually coupled rather than separately plausible
             * h_f,i replaced by the SENSIBLE reference enthalpy must FAIL. This control matters
               because those are different quantities that the existing API could easily conflate
               (audit/thermal_energy_coupling.md section 1)
evidence     results/p18/heat_release/
phase        P18.6
```

## G8 — canonical reacting validation

```text
purpose      quantitative agreement with a published benchmark. Visual similarity is not evidence.
required     at least one canonical reacting case with an external reference.
measured     a 1D laminar premixed flame: flame speed, peak temperature, species profiles.
acceptance   flame speed within the published experimental/numerical SPREAD for the mechanism,
                 with the reference and its spread CITED before the run. Where the literature gives
                 a range, the range is the criterion -- not its midpoint.
             profiles: REPORTED with their L1/L2 errors and grid refinement, gated only on
                 convergence UNDER refinement (the error must fall), not on an invented absolute
                 bound. A reacting front is a discontinuity-like structure and no single tight
                 tolerance is derivable -- the same reasoning P17.9's A2 used for the rarefaction
                 head.
negative     * reactions disabled must FAIL (no flame propagates)
control      * a 10x error in the pre-exponential factor must move the flame speed outside the
               cited spread
evidence     results/p18/validation/
phase        P18.7
```

## G9 — CPU/GPU equivalence

```text
purpose      if a GPU species path is built, it must solve the same numerical problem.
required     paired CPU/GPU solves compared on the STATE, not on summary norms.
measured     Y_i per cell, face flux, cell residual, dt/iteration control, reaction rates,
             heat-release source, and the control-flow decisions (iteration counts, rejections,
             bounds violations detected).
acceptance   BITWISE.
                 DERIVED FROM P17.10, which established that bitwise is achievable for a
                 face-loop assembly when (1) one shared source compiles for both, (2) the per-cell
                 gather visits faces in the CPU's accumulation order, (3) geometry is uploaded not
                 recomputed, (4) --fmad=false, (5) reductions are order-independent. The species
                 assembly is a face loop of the same shape. Anything looser is a concession that
                 must be argued from a specific mechanism, not assumed.
             control-flow decisions: EXACTLY EQUAL counts, and the same first-offending cell.
negative     * the contact/upwind direction reversed on device must break it
control      * --fmad=true must be REPORTED with its measured cost (P17.10 measured 7 of 15 tests
               failing and divergence growing to 704 of 1025 values by step 50)
evidence     results/p18/cpu_gpu/
phase        P18.8
note         THIS IS AN IMPLEMENTATION PHASE, not a comparison phase. There is no GPU species path
             today (audit/gpu_inventory.md). A claim of "GPU species" resting on the existence of
             DeviceScalarTransport would be vacuous: every consumer of it is k-epsilon.
```

## G10 — deterministic repeatability

```text
purpose      the same input must give the same output.
required     repeated runs, same process and separate processes.
measured     all species fields, reaction rates, iteration and outer-loop counts, residual
             histories, conservation sums.
acceptance   BITWISE, on both backends independently.
                 DERIVED: determinism is not an accuracy property. The existing species suite
                 already asserts this at three levels (assembly, solve, full run) and P17.10
                 extended it to separate processes and launch configurations. Inherited.
negative     * a run-to-run difference of 1 ULP in any field must be DETECTED -- the comparison
control        instrument must be shown able to fail (P17.10's dry-run precedent)
evidence     results/p18/determinism/
phase        P18.8 / P18.9
```

## G11 — full regression / qualification

```text
purpose      the final integrated P18 tree preserves everything and satisfies the new gates.
required     all three configurations, plus every earlier foundation.
measured     Release+CUDA, Debug+GUI, CPU-only; SIMPLE / CompressibleSIMPLE / P14 / P15 / P16 / P17
             preservation; the full P18 gate set.
acceptance   counts matching a prediction declared BEFORE the run, 0 new failures.
             THE 4 DEBUG+GUI TIMEOUTS ARE INHERITED, NOT P18's. They are pre-P17 DriverSeegmiller
             cases that pass in both Release configurations; P17.11 left G10 PARTIAL for exactly
             this. P18 reproduces them as baseline behaviour and does NOT raise the frozen 7200 s
             timeout to clear them.
negative     the regression harness must count DISABLED tests explicitly and predict the count --
control      the inherited harness reported "Not Run : 0" for every P17 phase while 49 DISABLED_
             tests were not running (results/p17/cpu_gpu_equivalence/HARNESS_FINDINGS.md section 1).
             P18 inherits the FIXED harness, not the original.
evidence     results/p18/qualification/
phase        P18.9
```

---

## 13. Dry-run against the current tree (§31)

Run or reasoned **before** freezing. "EXPECTED FAIL / NOT IMPLEMENTED" is the honest result for a
capability that does not exist, and is recorded as evidence rather than hidden.

| Gate | Dry-run result on today's tree | Basis |
|---|---|---|
| G1 | **PASSES** (a) and (c); (b) not yet applicable | the P18.0 baseline is being measured now; the SIMPLE fingerprint passed bitwise in P17.11 with its planted-defect control detected |
| G2 | **EXPECTED FAIL — NOT IMPLEMENTED** | no species collection, no `ΣY=1`, no bounds enforcement. All four criteria have nothing to measure |
| G3 | **PARTIAL PASS** | the steady per-species balance already passes 4 conservation tests, including "for any arbitrary field". The transient term and the mixture sum are **NOT EXPRESSIBLE** — no transient exists, species are independent |
| G4 | (a),(b),(d) **PASS** today · (c) **FAILS today: 335 outer iterations against a criterion of ≤2** | measured, `results/p18/logs/11_lag_probe.log`. This is the pre-existing lag, and the criterion was derived from thermal's measured 2 before the species number was quoted into the gate |
| G5 | **EXPECTED FAIL — NOT IMPLEMENTED** | no `W_i`, no `R_mix`, no `cp_mix`. The single-species-limit sub-criterion is the only one with anything to compare against today |
| G6 | **EXPECTED FAIL — NOT IMPLEMENTED** | no reaction rates exist |
| G7 | **EXPECTED FAIL — NOT IMPLEMENTED** | no heat release exists |
| G8 | **EXPECTED FAIL — NOT IMPLEMENTED** | no reacting case exists |
| G9 | **EXPECTED FAIL — NOT IMPLEMENTED** | no GPU species path exists |
| G10 | **PASSES for what exists** | `RepeatedAssemblyIsDeterministic`, `RepeatedSolveIsDeterministic`, `RepeatedRunsAreDeterministic` ×2, `CartesianSolveIsBitIdentical` all pass today |
| G11 | **baseline being established** | see results/p18/baseline/ |

```text
WHAT THE DRY-RUN CAUGHT, which is the point of doing it (§31):

1. G4(c) IS THE ONLY GATE THAT FAILS ON EXISTING, SHIPPED CODE. Every other failure is "the
   capability does not exist yet". That distinction matters: G4(c) is a defect in something users
   can run today, and its negative control is free because the defect IS the control.

2. G3's MIXTURE CRITERION WOULD HAVE BEEN VACUOUS AS ORIGINALLY DRAFTED. "Species conservation"
   sounds like one gate; the audit showed the per-species steady balance already passes while the
   transient and cross-species parts are not expressible at all. Splitting the criterion is what
   stops a later phase from claiming G3 on the strength of tests that already pass.

3. G5's SINGLE-SPECIES LIMIT IS THE ONLY MIXTURE CRITERION MEASURABLE TODAY, and it is the one that
   protects P17: if the mixture path does not reduce bitwise to the existing single-species values,
   it is not a generalisation, it is a replacement. Freezing it now means P18.3 cannot quietly
   change P17's numbers.

4. G9 WOULD HAVE BEEN CLAIMABLE VACUOUSLY. DeviceScalarTransport exists and looks generic, so
   "GPU species equivalence" could be asserted from a kernel-level test. The gate therefore
   requires an END-TO-END species solve on device, and the note records why.
```
