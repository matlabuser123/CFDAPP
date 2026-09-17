# P12-GRAD-002 — Amendment A3 (pre-registered)

Written on 2026-09-18 under the standing authorization of 2026-09-17: re-derive invalid acceptance
criteria, create justified gate amendments, preserve every failure. The DIFF-002 W9A amendment was
made the same way.

## 0. What happened under A2, recorded and not rewritten

A2 (`acceptance_gate_A2.md`, sha256 `ea88f10f…`) was frozen at 2026-09-17T13:50:23Z
(`a2/logs/00_freeze.log`) and executed fresh in its §7 order.

- **Passed fresh, before the failure point:**
  - C2-A2(a)–(d), and C2 on Q16, in repo, nograd and grad001 (`a2/logs/fresh_3d_*.log`);
  - C9 on the deformed family: repo passes, and nograd and grad001 fail C3(b) as pre-registered
    (`fresh_c9_*`);
  - C10-A2(a), (b) and (c), and C11(a) (`fresh_c10_*`);
  - step 2: the new tests 9/9, `CFDDiscretizationTests` 178/178, library `143a1dda…`
    (`01_step2_build_focused.log`).
- **FAILED: C10-A2(d)** ([a2/logs/fresh_c10_selftest_mutated.log](a2/logs/fresh_c10_selftest_mutated.log)).
  The frozen text requires the mutated comparison to "flag both on every skewed mesh".
  - **The layer-2 mutation was not flagged on `skewed warped3d 8|quadratic`** (worst ratio 0.772).
  - **The deep-cell mutation cannot be applied on three skewed meshes** (production quad 64×8,
    translated cube 8³, warped cube 8³), which have no cell of layer ≥ 6.
  - **The frozen record was wrong before the freeze.** The pre-freeze log
    `a2/logs/dry_c10_selftest_mutated.log` shows **the same rows**, so `a2/dryrun.md`'s entry
    "flagged on every skewed mesh" was already false when frozen: every row of the dry-run was not
    checked. This is the same class of error A1 recorded, and it is recorded again here.
- **By A2's stop rule: `P12-GRAD-002 A2 — FAILED GATE at C10-A2(d)`.** That verdict stays on
  record.
  - C8, C11(b), C13's suites and the full regression were **not reached** under A2.
  - A second attempt at A2 step 4 was already running when the failure was found. Its results are
    information only.
  - The first step-4 attempt failed closed: the frozen suite runner's stale check stopped it
    before ctest (`*.INVALID-stale-check-failed-closed-no-ctest.log`). `fresh.sh` then ran the
    C8/C11(b) comparisons on the dry-run's ctest output. Those logs are kept as
    `*.INVALID-compared-dryrun-ctest-output.log`. A disclosed clean-first rebuild of both control
    trees followed (`a2/tools/clean_rebuild.sh`, `fresh_clean_rebuild.log`; libraries unchanged).

## 1. Why C10-A2(d) is invalid, and what a valid check is

C10-A2(d) exists to show that the C10-A2(b) instrument can detect the two things (b) asserts
cannot happen:

1. a difference in a cell beyond the propagation depth;
2. an interior difference larger than the sweep-coupling bound.

The frozen design was wrong on both counts:

- **The depth check can only be exercised where cells of layer ≥ K + 2 = 6 exist.** On shallower
  meshes, (b)(i) is itself vacuous. That should be reported, not demanded.
- **A fixed perturbation of 1e-3 of the gradient scale does not necessarily exceed the bound.** The
  bound is a per-cell quantity that can be larger: on the warped cube it allows about 1.3e-3 there.
  A non-vacuity injection must exceed the quantity it tests **by construction**.

**Stronger evidence is also available and is added as (e).** A real, wrong operator must be
rejected by (b). A library whose interior skew correction differs, for a reason unrelated to the
boundary coupling, must fail the depth rule and the coupling bound wherever that difference is
non-zero.

## 2. Replacement criteria (C10-A2(d) only; everything else in A2 stands unchanged)

| id | criterion | pre-freeze dry-run (on the A2 **pre-freeze** dumps, `$HOME/g2/c10`) |
|---|---|---|
| **C10-A3(d)** | **Instrument self-test, by construction** ([a3/tools/selftest_c10.py](a3/tools/selftest_c10.py), which imports `compare_c10.py` unchanged). For every skewed mesh and field of the repo-vs-nograd dumps: **(control)** the unmutated evaluation shows 0 deep differences and 0 bound violations. **(deep)** Where cells of layer ≥ 6 exist, moving the first by one ulp is flagged as exactly 1 deep difference; otherwise N/A, reported. **(bound)** Moving the first layer-2 cell by 3 × (its own bound + floor) is flagged as exactly 1 violation. The proof: afterwards \|Δg4\| ≥ 3(b + f) − (b + f) = 2(b + f) > b + f. | [a3/logs/dry_selftest.log](a3/logs/dry_selftest.log): **30/30 rows PASS**. Deep is flagged on all 21 rows that have layer-6 cells and is N/A on 9. Bound is flagged on all 30. |
| **C10-A3(e)** | **Operator-mutant control** ([a3/tools/mutant_c10.sh](a3/tools/mutant_c10.sh)). The mutant is the nograd tree with one change, `kGreenGaussSkewCorrectionSweeps` 4 → 3. Compared by C10-A2(b) as OLD against repo, it **must be rejected**, with ≥ 1 bound violation and (where layer-6 cells exist) ≥ 1 deep difference, **on every mesh with genuinely skewed interior faces whose fourth sweep changes the gradient**: Q16, NUM-003 20×20 at 0.45 h, the production quad 64×8 and the warped cube 8³. On the other meshes a three-sweep operator is indistinguishable at interior cells: the translated meshes (round-off skew) and the C5 shear (tiny, already converged skew). Their outcome is reported, not required. | [a3/logs/dry_mutant_c10.log](a3/logs/dry_mutant_c10.log), mutant library `036a329e…`. **Rejected on all four**, for all three fields: Q16 deep 36/36, 104–106 violations; NUM-003 deep 100/100, 195–196; production quad 169–177 (no layer-6 cells); warped cube 8 (no layer-6 cells). Translated meshes and C5 shear: 0, as derived. |

## 3. Carried over

Everything else in A2 is carried over **unchanged**, with its frozen thresholds and definitions.
Because C8, C11(b), C13 and the full regression were not reached under A2, **they are executed
fresh after this freeze**, in A2 §7 order:

1. step 4, the cur/nograd suites and comparisons (`a2/tools/fresh.sh suites`, logs re-prefixed
   by `a3/tools/fresh_a3.sh` so A2's attempt stays);
2. step 5, the full regression (`a2/tools/run_regression.sh`, written after the A2 freeze with W10's
   procedure; hashed with this amendment);
3. step 6, the closeout.

The criteria that passed fresh under A2 before the failure point are **not** re-scored. Their A2
fresh logs are the record.

**Pre-registered fresh outcomes under A3.**

| run | expected |
|---|---|
| C10-A3(d) on freshly regenerated dumps | as the dry-run: 30/30 |
| C10-A3(e) | as the dry-run |
| C8, C11(b), C13 suites | as A2 §5: cur 1932/1932; nograd exactly the 4 named failures; every C8 change categorized; C11(b) aligned field exports PASS |
| full regression | Release, Debug + GUI and ASan + UBSan all 100 %; 0 sanitizer diagnostics; format clean; library `143a1dda…` |

## 4. Stop rule

Stop at the first failed A3 criterion or carried-over criterion. Record
`P12-GRAD-002 BLOCKED / FAILED GATE`, and do not amend after seeing a fresh result.

A3 changes no production source, no test, and no threshold of any other criterion.
