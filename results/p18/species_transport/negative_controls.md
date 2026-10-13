# P18.2 — negative controls (§38)

```text
Each control breaks ONE thing and requires its predicted criterion to fail. Where a control can be
injected through the production API it is a PERMANENT TEST rather than a source mutation -- a
permanent control cannot rot, and it runs in every regression.

Two of the eight arose NATURALLY as real defects during the phase, which is the strongest form of
non-vacuity available: the criterion caught the mistake before the control was written for it.
```

---

## 1. The eight

| # | Defect | How injected | Criterion that must fail | Result |
|---|---|---|---|---|
| **N1** | advance `Y` instead of `ρY` | pass `rhoNew` where `rhoOld` belongs | the transient RHS must scale by the density ratio | **DETECTED** |
| **N2** | remove `ρ` from the convective mass flux | see §3 — covered by construction | — | **COVERED BY CONSTRUCTION** |
| **N3** | wrong diffusion sign | **arose naturally** | closed-enclosure conservation | **DETECTED, 1.29e-3** |
| **N4** | one diffusivity for all species | collapse `D_i` in `makeSpeciesDiffusivities` | `Σ_f` non-zero, corrected fluxes differ | **DETECTED** |
| **N5** | remove the closure correction | `closureCorrection = false` | species sum of the assembled system | **DETECTED, 1e14 ratio** |
| **N6** | reintroduce the boundary lag | `GradientBoundaryTreatment::Lagged` | ≤ 2 outer iterations (G4(c)) | **DETECTED** |
| **N7** | omit a boundary species flux | see §4 | open-domain balance | **DETECTED** |
| **N8** | let `ΣY` drift unnoticed | loosen the solver tolerance | the per-step `ΣY` bound | **DETECTED** |

## 2. The two that arose as real defects

```text
N3 -- WRONG DIFFUSION SIGN. Not written as a control: it was a genuine defect in the first
      implementation. rhs[owner] -= correction where the derivation requires += . Caught by
      ClosedEnclosureConservesEverySpeciesMass as a sum(Y) drift of 1.2860866145953942e-03, and the
      fix moved it to 1.5774036361548838e-06 -- a factor of 815.
      The criterion demonstrably detects a sign error in the correction, because it did.
      logs/04_conservation_failure_PRESERVED.log

N6 -- THE BOUNDARY LAG. The gate itself calls this control free, because it is the pre-P18.2
      behaviour. Made PERMANENT as LaggedGradientBoundaryNeedsFarMoreIterations, which asserts the
      ITERATION COUNT and not the converged field -- §18 is explicit that a field comparison is too
      weak, because an iteration-lag defect disappears after convergence. The companion test
      ExactAndLaggedAgreeOnceConverged proves that is exactly what happens: both treatments reach
      the same field, so only a count can see the difference.
```

## 3. N2, covered by construction rather than by mutation

```text
The defect N2 names -- "remove rho from the convective mass flux" -- cannot be injected into P18.2's
production code, because P18.2 NEVER FORMS A MASS FLUX. It takes the authoritative SurfaceField from
physics::calculateMassFlux as a parameter and passes it straight to P14's convection assembler.
There is no rho_f, no U_f and no mdot_f anywhere in src/species/SpeciesTransport.cpp -- verifiable
by grep, and asserted by the integrity check in summary.md.

So the property N2 guards is structural: there is no duplicated flux logic to get wrong. Recorded as
COVERED BY CONSTRUCTION rather than DETECTED, because claiming a detection for a control that could
not be run would be a false positive of exactly the kind the P15 harness notes warn about.
```

## 4. N7, through the open-domain balance

```text
OpenDomainBalanceClosesWithEachTermSeparate reports accumulation, convective boundary flux,
diffusive boundary flux and source as FOUR SEPARATE NUMBERS (§12 requires exactly this), so omitting
any one of them leaves a residual that the imbalance criterion sees. Folding them into a single
residual -- which §12 forbids -- is what would have let an omitted term hide inside a small total.
```

## 5. N8, and why it is the same instrument as the per-step bound

```text
N8 asks for a control against sum(Y) drifting unnoticed. PerStepDriftScalesWithTheSolverTolerance
IS that control, in its strongest form: it does not merely check that a drift is detected, it checks
that the detected drift SCALES with the mechanism the bound names.

    tau_solve = 1e-15  ->  2.886579864025407e-15   inside perStepSumBound(2, 1e-15)
    tau_solve = 1e-08  ->  3.290559935642534e-08   BREACHES it by six orders of magnitude
                                                    and stays inside perStepSumBound(2, 1e-08)

A bound that passes at one tolerance and fails at another, in proportion, is a bound that measures
something. A fitted constant would not do this.
```

## 6. What is NOT controlled, and why

```text
source-mutation controls    NOT USED for P18.2. Every defect above is injectable through the
                            production API -- an options flag, a swapped argument, a collapsed
                            input -- so the controls are PERMANENT TESTS that run in every
                            regression rather than a script that mutates source and restores it.
                            P18.1 used source mutation because its invariants were unconditional
                            checks with no API surface to disable them; P18.2's are parameterised,
                            which is strictly better for control durability.
chemistry controls          nothing to break: omega_i = 0 and no reaction code exists.
GPU controls                no GPU species path; P18.8's.
```
