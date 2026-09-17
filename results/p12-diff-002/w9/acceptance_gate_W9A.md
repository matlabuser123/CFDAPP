# P12-DIFF-002 W9 AMENDMENT (W9A) — acceptance gate

Frozen **before** the fresh W9 rerun. Its hash is recorded in [logs/04_w9a_freeze.log](logs/04_w9a_freeze.log).

## 0. Chronology

```text
W9 (original gate, acceptance_gate.md)   FAILED as frozen: 4 cases "worse" mass imbalance (logs/02, preserved)
W9A                                      this amendment; derived floor, dry-run below
W9A fresh run                            <pending>
```

The original W9 criterion reads "mass imbalance not worse than the recorded value". Applied
literally ([logs/02_w9_compare.log](logs/02_w9_compare.log)), it fails on:

| case | recorded (pre-DIFF-002) | current |
|---|---|---|
| `poiseuille_distorted` | 7.50e-13 | 3.97e-11 |
| `channel_transpiration_graded` | 3.48e-12 | 1.22e-11 |
| `step_channel_multiblock` | 2.01e-11 | 2.45e-11 |
| `compressible_channel_coupled` | 1.40e-11 | 6.23e-10 |

Every one of these values is 3–5 orders of magnitude below the case's own convergence tolerance
(1e-6). This failure stays on record.

## 1. Why the literal criterion is invalid

A threshold may not be tighter than the unresolved numerical uncertainty. That rule is part of this
authorization.

The global imbalance of a SIMPLE (or CompressibleSIMPLE) solution comes out of the last
pressure-correction solve:

- The corrected flux satisfies each cell's continuity equation up to that solve's residual `r`
  (the correction adds exactly the assembled coefficients; `SIMPLE.cpp`).
- Internal fluxes telescope, so the global net flux is `Σ_i r_i`, and `|Σ_i r_i| ≤ √N ‖r‖₂`.
- CG and BiCGSTAB stop at `‖r‖₂ ≤ absTol` **or** `‖r‖₂ ≤ relTol·‖b‖₂` (`CG.cpp:52–54`,
  `BiCGSTAB.cpp:83–85`). The iterate is therefore guaranteed only to `‖r‖₂ ≤ max(absTol, relTol·‖b‖₂)`.

**Resolution floor of a run:**

```text
floor = √N · max(absTol_p, relTol_p · ‖b_p‖_final)
```

- `N` is the cell count.
- `absTol_p` and `relTol_p` are the case's pressure linear-solver tolerances.
- `‖b_p‖_final` is the last iteration's pressure-correction right-hand-side norm, which the solver
  reports as `residuals.p`.

Below this floor, two imbalances cannot be ordered: which one is larger is decided by where the
linear solve happened to stop. The recorded values above all lie orders of magnitude below their
floors (2.3e-9, 3.4e-9, 4.1e-9, 2.0e-7).

## 2. W9A criteria

| id | criterion | derivation |
|---|---|---|
| **W9A-1** | Every committed case runs in all three configurations (A: pre-DIFF-002 library + original cases; C: current library + original cases; B: current library + current cases), and every SIMPLE/compressible solve that ran converged. | W9's rerun requirement |
| **W9A-2** | **Model validity.** Every run (A, C and B) satisfies `imbalance ≤ floor`. If any run violated its own floor, the floor model would be wrong and W9A would not be evaluable. | §1 |
| **W9A-3** | **Conservation no worse.** For every case, `imbalance_B ≤ max(recorded, floor_B)`. "Recorded" is `logs/05` where it lists the case, otherwise run A. | W9's own criterion, made resolution-aware |
| **W9A-4** | **Scope of the case fix.** Every exported field of every case other than the two DRIFT-001 cases is bit-identical between C and B. | DRIFT-001 F1 |
| **W9A-5** | **Changes explained.** Every A→C and C→B field change is attributed (§4) to DIFF-002's wall flux, the DRIFT-001 flux fix, or the linear flux's stopping-point dependence, and each changed case's accuracy is covered by a named independent-reference test that passes in W10. | W9's "solution-norm changes explained" |
| **W9A-6** | **Non-vacuity.** A scratch-tree library that omits the Dirichlet-boundary flux correction (`correctFaceMassFlux`) violates W9A-2/3, or fails W9A-1, on the open-channel cases. | §3 |

## 3. Pre-freeze dry-run (the original W9 runs, re-evaluated)

[logs/03_w9a_dryrun.log](logs/03_w9a_dryrun.log): **W9A-2 holds for all 57 runs.** The floor is
not vacuously loose:

| run | imbalance / floor |
|---|---|
| `poiseuille_flow` A | 1.977e-7 / 2.263e-7 = **0.87** |
| `compressible_validation` A | 0.67 |
| `duct_3d` A | 0.53 |

**W9A-3 holds for all 19 cases.** W9A-4 holds: C→B is exactly zero for 17 cases (logs/02).

W9A-6 is executed with the fresh run (a scratch library, [tools/mutant.sh](tools/mutant.sh)).

## 4. Explanations (W9A-5)

- **A → C (DIFF-002).**
  - Open and closed flow cases change by 0.4–9 % in velocity and 1.5–17 % in pressure (relative
    L2). This is the intended change of the Dirichlet wall flux from first to second order.
  - The large relative pressure changes are in closed cavities, whose gauge pressure is small (range
    ~0.1).
  - The linear-flux open cases also carry a stopping-point component
    (`results/p12-grad-002/drift-001/`).
  - Thermal and species conduction cases change by ≤ 4.5e-6 (4.3e-6 relative), because both wall
    schemes are exact for their linear profiles.
  - Accuracy references that must pass in W10:
    - Poiseuille: UC-001 (exact discrete);
    - distorted and curved channels: W8B (analytical);
    - Ghia cavity and natural convection: UF-001;
    - 3D duct and cube: MESH-006 A3 and G7;
    - compressible lubrication channel: `CompressibleSimpleMatchesLubricationSolution*`;
    - MMS suites.
- **C → B (DRIFT-001).** Only `poiseuille_distorted` and `curved_channel_multiblock` change:
  - velocity by 1.4–5.9 %, pressure by 5.3–5.8 %;
  - `v`, which is ≈ 0 in the exact solution, by 35 % of its small norm;
  - this is the removal of the odd-even pressure mode, and the linear-flux state was not stationary;
  - their accuracy against the analytical solutions is W8B.
