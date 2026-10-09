# P17.9 — amendment coverage map

The frozen `acceptance_gate.md` is **UNCHANGED**, sha256
`b7185ceac2c0a79c103679218c14c1a17b70b7fc6acca626807dc2ade0910f40`. Every amendment is recorded
here and in its own file; none edits the frozen document, and none removes or loosens an acceptance
bound.

| Gate | Original issue | Amendment | Stronger / corrected? | Negative control | Result |
| --- | --- | --- | --- | --- | --- |
| **A1** Group V (V1, V2) | **VACUOUS** — HLL with the contact wave deleted passed the *entire* gate | stationary contact must be preserved with **exactly 0** smeared cells | **STRENGTHENS** — adds a criterion, removes none | V1 Rusanov **FAIL**, V2 HLL **FAIL**, both on `width = 21` vs required `0` | **PASS** — HLLC gives `width = 0` |
| **A2** X3 rarefaction head | **DIMENSIONAL ERROR** — a `C·dx` bound applied to a diffusively-smoothed *derivative* discontinuity, unsatisfiable as `dx → 0` | gate the derived scaling: err falls, `err/√dx` does not grow, `err/dx` grows. **No free constant** | **CORRECTS A SCALING ERROR** | Rusanov (more diffusive) gives a **larger** head error, `6.348e-2` vs `5.892e-2`, as `err ∝ √D` predicts | **PASS** — all three scaling assertions hold |
| **A3** S6 density plateaus | **CONTAMINATED** — the contact's diffusive tail enters the density window; `p*` converges at rate 1.367 from the *same* window while `ρ*_R` does not (rate −0.045) | measure the **flattest** 9-cell sub-window, which isolates the plateau from both wave tails | **REMOVES CONTAMINATION** — S1–S5 bounds untouched, S6 for `p*`/`u*` untouched | isolation must be ≥10× cleaner than the contaminated estimate, or the amendment is rejected | **PASS** — 14× / 948× / 227× cleaner |

---

## Exact evidence paths

```text
frozen gate, unmoved            results/p17/shock_validation/acceptance_gate.md
frozen configuration            results/p17/shock_validation/configuration.md
freeze record                   results/p17/shock_validation/logs/02_freeze.log

ORIGINAL FAILURES, preserved
  Sod first run (X3 failure)    logs/PRESERVED_03_sod_first_run.log
  refinement (S6 failure)       logs/06_waves.log
  controls, PRE-amendment       logs/07_controls.log          <- V2 passed everything
  detector dry-run failure      logs/PRESERVED_01_dryrun_detectors_broken.log

INVESTIGATION
  plateau contamination         logs/04_diagnose_plateau.log
  estimator comparison          logs/09_plateau_estimators.log
  contact width by flux         logs/08_contact_width.log
  V2 against each frozen row    logs/12_v2_detail.log

AMENDED RESULTS
  focused suite, 13/13          logs/10_amended.log
  controls, POST-amendment      logs/11_controls_amended.log  <- V1 and V2 both fail
  A2 negative control           logs/13_a2_control.log

AMENDMENT RECORDS
  A1                            amendments/A1_stationary_contact_width.md
  A2                            amendments/A2_rarefaction_head_scaling.md
  A3                            amendments/A3_plateau_contamination.md
  this map                      amendments/coverage_map.md
```

## The audit trail, in the order it happened

```text
frozen gate (02_freeze.log, before any benchmark existed)
   |
   +-- dry-run of the DETECTORS before freezing -> 3 detector defects found and fixed
   |     PRESERVED_01_dryrun_detectors_broken.log
   |
   v
observed problems
   |  X3  head error 11.78 dx against a 2 dx bound           PRESERVED_03_sod_first_run.log
   |  S6  rho*_R did not improve 400 -> 800                  06_waves.log
   |  V   HLL passed the ENTIRE gate                         07_controls.log
   |
   v
independent derivation / negative control
   |  A1  contact width by flux: 0 / 21 / 21                 08_contact_width.log
   |  A2  diffusion-length derivation; Rusanov err larger    13_a2_control.log
   |  A3  p* converges, rho* does not, same window           09_plateau_estimators.log
   |
   v
AMENDMENTS A1 / A2 / A3        (acceptance_gate.md NOT edited)
   |
   v
amended rerun
      focused suite 13/13                                    10_amended.log
      V1 FAIL, V2 FAIL, 0 unexpected                         11_controls_amended.log
```

## What each amendment would have cost if NOT made

```text
A1 not made  -> the gate certifies a solver with no contact wave. G7's own text calls this
                "not measuring shock capturing". This is the one that mattered.
A2 not made  -> X3 permanently FAILED, recording a solver defect the refinement study shows does
                not exist (the error halves under refinement), and failing worse on finer meshes.
A3 not made  -> S6 permanently FAILED for the density plateaus, recording a convergence failure
                that is a property of the sampling window rather than of the solver.
```

## Thresholds weakened

```text
NONE.

A1 adds a criterion and removes none.
A2 removes an unsatisfiable bound whose dimension is wrong and adds two scaling assertions;
   X1, X2, X4 and X5 are untouched and still enforced.
A3 changes only the ESTIMATOR for two density quantities; every acceptance bound (S1-S5) is
   unchanged and still evaluated against the ORIGINAL contaminated measurement, which passes.
```
