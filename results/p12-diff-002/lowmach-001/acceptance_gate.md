# P12-DIFF-002-LOWMACH-001 — `LowMachRegressionTest` validation-instrument migration (gate)

This is a W10 blocker. `LowMachRegressionTest.GlobalMassImbalanceIsSmall` fails in the full
regression (1.0343290470e-4 against 1e-4). It passes on the pre-DIFF-002 build (INV-002), so
DIFF-002's change made it fail. The gate was frozen before the repository test was edited; its hash
is in [logs/00_freeze.log](logs/00_freeze.log).

## 1. Findings (probe: [tools/lowmach_probe.cpp](tools/lowmach_probe.cpp); logs 01, 02)

`runLowMachCase` is reproduced verbatim, including the failing value 1.034329e-4.

**Defect 1 — the test measures the wrong field.**

- `SIMPLE::solve` takes the initial velocity **by value**. The test's local `velocity`, which
  feeds the Mach number and `calculateCompressibleMassFlux`, is therefore the **uniform initial
  guess** and never the solution.
- Consequences:
  - "Mach max" = 2.8802e-3 = U/c exactly. The solved profile peaks at 1.5 U: Mach 4.16e-3.
  - The documented "~9 % low-Mach flux difference" is the uniform-versus-parabolic **profile**
    difference: 9.13 %, against 3.4e-5 with the solution.
  - The "global imbalance" of a uniform stream is its inlet-outlet density change, i.e. the
    pressure drop over p_ref. It equals `dp_b/p_avg` to six digits at every grid.

**Defect 2 — the bound is an empirical lock below the physical value of that quantity.**

- `dp_b/p_avg` converges to ≈ 1.09e-4 under refinement: 1.034e-4, 1.059e-4 and 1.087e-4 at 32×6,
  64×12 and 128×24.
- DIFF-002 made the discrete pressure drop more accurate (9.74e-5 → 1.034e-4 at 32×6), and so
  crossed the lock.

**No production defect.**

- The underlying SIMPLE solution conserves mass to 1.7e-8 (relative to UH).
- The compressible flux assembly is exactly consistent. Its net flux equals the analytic
  boundary-density change.

## 2. Migration (candidate: [data/candidate/](data/candidate/))

| test | change |
|---|---|
| `runLowMachCase` | Every compressible quantity is built from `flow.velocity`, the documented intent. The derived bound (below) is computed. |
| `MachNumberStaysWellBelowPointOne` | Keeps `0 < Ma < 0.1`, and **adds** `Ma_max > 1.25 · Ma(U)`: the developed profile peaks at 1.5 U, and the uniform-field defect gives exactly `Ma(U)`. |
| `CompressibleMassFluxApproachesTheIncompressibleLimit` | Comment corrected. **Bound 0.15 unchanged**; measured 3.4e-5. |
| `GlobalMassImbalanceIsSmall` | `imbalance ≤ bound`, a derived consistency bound, replaces the fixed `< 1e-4`. |

**The derived bound.**

- On a boundary face, the flux is `ρ_f F'_f`, where `F'_f` is the face's volumetric flux of the
  same velocity field.
- Every boundary density lies in `[ρ_lo, ρ_hi]`: the EOS density at the pressures the boundary
  conditions imply. That is the owner cell for the zero-gradient inlet and walls, and `p_ref` for
  the `p = 0` outlet.
- Hence `|Σ ρ_f F'_f| ≤ ½(ρ_hi − ρ_lo) Σ|F'_f| + ½(ρ_hi + ρ_lo) |Σ F'_f|`.
- Normalised by `ρ̄ U H`, this is `≈ Δρ/ρ` plus the volumetric imbalance: the low-Mach parameter,
  grid-adaptive and exact.
- It uses no production face-density code, only the EOS, which
  `EosConsistencyErrorIsAtFloatingPointTolerance` verifies separately.

## 3. Criteria

| id | criterion |
|---|---|
| **L1** | Only `tests/integration/compressible/test_low_mach_regression.cpp` changes, and it becomes byte-identical to the candidate. No production file changes. |
| **L2** | `CFDLowMachRegressionTests` passes 7/7 from a fresh authoritative build. |
| **L3** | Non-vacuity, as in the dry-run below: the outflow-density mutant is rejected by the bound, the uniform-field defect is rejected by the Mach check, and the original test reproduces the recorded failure. |

## 4. Pre-freeze dry-run ([tools/dryrun.sh](tools/dryrun.sh), logs `dry_*`)

| variant | result |
|---|---|
| cand (current library) | **7/7**. Imbalance 4.509e-5 ≤ bound 1.701e-4. |
| base (pre-DIFF-002) | **7/7**. 4.800e-5 ≤ 1.533e-4. |
| mutant (outflow density +0.1 %) | **REJECTED**: 9.549e-4 > 1.701e-4. |
| bugfield (uniform field reinstated) | **REJECTED** by the Mach check: 2.880e-3 is not > 3.600e-3. |
| orig (original test, current library) | reproduces the failure, 1.0343290470e-4 against 1e-4. |

**A vacuous control was caught and preserved.** The first mutant scaled *every* boundary face.
That scales inflow and outflow alike and leaves the balance intact, so it was accepted
(`logs/dry_mutant_VACUOUS_first_draft.log`). The control was corrected; the criterion was not.
