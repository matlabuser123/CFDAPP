# P12-GRAD-002 — Amendments A2 and A3: execution record

This record covers A2 (`../acceptance_gate_A2.md`, sha256 `ea88f10f…`, frozen
2026-09-17T13:50:23Z) and A3 (`../acceptance_gate_A3.md`, sha256 `f4d579fa…`, frozen
2026-09-17T14:03:38Z). Failed runs are kept; nothing below rewrites them.

## 1. Chronology

```text
original gate            FAILED at C1                                    (summary.md)
A1                       FAILED at C2/C9 "3D deformed"                   (a1/summary_a1.md)
INV-001                  production degradations traced to the wall flux (investigation/)
VAL-001                  distorted-order band migrated                   (val-001/)
DIFF-002                 wall flux second order — COMPLETE 2026-09-17    (results/p12-diff-002/)
DRIFT-001                2D linear-flux odd-even mode; W8 cases → RC     (drift-001/)
A2 dry-run + freeze      13:50:23Z
A2 fresh                 C2-A2, C9, C10(a)(b)(c), C11(a) PASS
                         C10-A2(d) FAILED (self-test design error, visible in the frozen dry-run)
A3 dry-run + freeze      14:03:38Z (C10-A2(d) replaced by C10-A3(d)(e))
A3 fresh                 C10-A3(d)(e) PASS; carried-over C8, C11(b), C13 suites PASS
A3 full regression       14:11–15:49Z PASS: Release 1932/1932, Debug + GUI 1984/1984,
                         ASan + UBSan 1932/1932, 0 sanitizer diagnostics, format 0 of 568 (§4)
C14 re-verification      15:50Z PASS: the A1-form criteria rerun on the final library (§4b)
P12-GRAD-002             COMPLETE 2026-09-17 (../summary.md)
```

## 2. Results of the fresh execution

| criterion | result | log |
| --- | --- | --- |
| C2-A2(a) planar fixed point | PASS, all 3 libraries (e₆₄ ≤ floor on all 8 planar meshes) | `a2/logs/fresh_3d_*` |
| C2-A2(b) recursion bound | PASS: 0 violations on 21 meshes × 3 libraries; worst ratio 0.992 | same |
| C2-A2(c) warp order | PASS: 1.925, 1.982 | same |
| C2-A2(d) non-vacuity | PASS: PG resolved on 9/9 warped meshes, 0/9 planar; the PG := 0 control is violated on every warped mesh and on none of the planar ones; quadrature self-checks pass | same |
| C2, Q16 | PASS: 3.769e-10 ≤ 1e-9 | same |
| C9 on deformed 3D: C3(b), C4a, C6-A1 | PASS in repo: L∞ order 1.814 / 1.935, L2 1.905 / 1.953; ρ ≤ 0.069; spread ≤ 1.51. nograd and grad001 fail C3(b) (0.883 / 0.942), as pre-registered | `a2/logs/fresh_c9_*` |
| C10-A2(a) aligned interior bitwise | PASS (21 mesh-field rows) | `a2/logs/fresh_c10_repo_vs_nograd.log` |
| C10-A2(b) skewed: depth and coupling bound | PASS: 0 deep differences, 0 violations (worst 0.458), t in [0, 1] | same |
| C10-A2(c) nograd ≡ base | PASS: 45 of 45 comparable rows bitwise identical | `a2/logs/fresh_c10_nograd_vs_base.log` |
| **C10-A2(d)** | **FAILED as frozen**, see A3 §0 | `a2/logs/fresh_c10_selftest_mutated.log` |
| C10-A3(d) self-test by construction | PASS: 30/30 rows (deep flagged on 21, N/A on 9; bound flagged on 30) | `a3/logs/fresh_selftest.log` |
| C10-A3(e) operator mutant (3 sweeps) | PASS: rejected on Q16, NUM-003, the production quad and the warped cube, all fields; the other meshes are indistinguishable, as derived | `a3/logs/fresh_mutant_c10.log` |
| C11(a) aligned boundary gradients | PASS: max 5.84e-16 ≤ 1e-13 | `a2/logs/fresh_c10_repo_vs_nograd.log` |
| C11(b) generated outputs | PASS: aligned field exports within 1e-6 or one printed unit (13 files); 58 VALUES files categorized (6 A, 52 B); 119 identical, 10 timing-only | `a3/logs/a3_c11b_*` |
| C8 previous-phase verification | PASS: every suite passes in cur (1932/1932). The comparator has 1977 rows (`summary: SAME 1920`): 1932 executed tests plus 45 disabled ones, which are Not Run and print nothing in either tree. Of the 1932 executed tests, 1875 print identical numbers, 53 changed and 4 text-changed, all categorized (A 31, B 25, T 1); 0 unexplained | `a3/logs/a3_suite_cur.log`, `a3_c8_*` |
| C12 no geometric threshold branch | PASS: A1 audit. `Gradient.cpp` is unchanged since A1 (`d24882a9…`); the remaining exact-zero tests are the benign ones the audit classified | `a1/audit.md` |
| C13 new tests | PASS: 9/9 on the current library. The pre-GRAD-002 control fails exactly the two negative controls, and the nograd suite fails exactly the 4 pre-registered tests | `a2/logs/01_step2_build_focused.log`, `a3/logs/a3_suite_nograd.log` |
| C13 / C14 full regression, format, library | PASS: Release 1932/1932, Debug + GUI 1984/1984, ASan + UBSan 1932/1932 with 0 diagnostics and 0 timeouts; clang-format 0 of 568; Release library `143a1dda…` unchanged (§4) | `a2/logs/regr_*` |
| C14 A1-form criteria on the final library | PASS: C1-A1, C3(b), C4a/b, C5, C6-A1 and C9 translation are bitwise identical to A1; C7 changed by DIFF-002 and within 1e-9 (§4b) | `a3/logs/c14_*` |

**Production accuracy (A2 §6, report).** Reproduced fresh, bit for bit, from the dry-run
(`a2/logs/fresh_prod_*`). GRAD-002 against nograd, committed settings, W8 grids:

| quantity | change |
| --- | --- |
| StructuredQuad velocity L2 | −0.02 % at every grid |
| StructuredQuad dp/dx error | −0.09 %, +0.28 %, −1.29 % |
| MultiBlock velocity L2 | −0.64 %, −0.06 %, +0.12 % |
| MultiBlock G error | +0.04 %, +0.06 %, +0.09 % |
| iterations | +3 to +5 |

No change comes near the 10 % W8B iterative uncertainty. **INV-001's production-accuracy
obstacle is resolved.**

## 3. Incidents during execution (disclosed)

1. **First A2 step-4 attempt.** `run_suite.sh`'s mtime check failed closed before ctest: rsync
   preserves mtimes, so the copied new test file was newer than executables that do not compile
   it. `fresh.sh` then ran the C8/C11(b) comparisons on the dry-run's ctest output. Both sets of
   logs are kept as `*.INVALID-*`. Both control trees were then clean-rebuilt; the libraries are
   unchanged (`fresh_clean_rebuild.log`).
2. **First A3 step-4 attempt.** It failed closed again, this time on CLI result files that the
   previous suite run had written into `tests/data`. `fresh_a3.sh`'s guard held, and no comparison
   ran. The logs are kept as `*.FAIL-CLOSED-*`. The frozen `fresh.sh rebuild` step reset both
   trees' `tests/` from the repository, and the rerun passed the check.
3. **A2 step 2's mtime check** listed 41 executables as stale. They do not compile the one changed
   file; ninja's dry run reported "no work to do" (appended to the log).

## 4. Full regression (A2 step 5, A3)

**PASS.** The run is [a2/tools/run_regression.sh](../a2/tools/run_regression.sh), driven from
[logs/regr_driver.log](logs/regr_driver.log), 2026-09-17T14:11:50Z–15:49:13Z.

- The three builds ran one after another, each a clean-first rebuild.
- Nothing else heavy ran on the machine.
- Each stage fails closed on a stale executable. None was stale in any stage.

| stage | build | library | tests (exact) | ctest time |
| --- | --- | --- | --- | --- |
| integrity + format ([regr_01](../a2/logs/regr_01_integrity_format.log)) | src/ + include/ tree `42c3d9df…` (the A2 freeze value); test file `9cc2d163…`, CMakeLists `42f9beb5…` | `143a1dda…` before the rebuild | **clang-format-18 `--dry-run --Werror`: 0 violations in 568 files** | — |
| **Release**, GUI off ([regr_02](../a2/logs/regr_02_release.log)) | clean-first, 172 s, **0 warnings**; 42 executables, 0 stale | **`143a1dda…`**, unchanged | **1932 / 1932 passed**, 0 failed, 0 timeouts; 45 disabled not run | 198 s |
| **Debug + GUI**, Qt offscreen ([regr_03](../a2/logs/regr_03_debug_gui.log)) | clean-first, 245 s; 6 warnings, all the known `-Wconversion` debt in `apps/gui/SimulationControllerEditing.cpp:86-89`; 44 executables, 0 stale | `06d5567f…` (as in W10) | **1984 / 1984 passed**, 0 failed, 0 timeouts; 45 disabled not run | 1273 s |
| **ASan + UBSan**, CI settings ([regr_04](../a2/logs/regr_04_asan_ubsan.log)) | clean-first, 294 s, **0 warnings**; 42 executables, 0 stale | `aef5c4ee…` (as in W10) | **1932 / 1932 passed**, 0 failed, **0 timeouts**, 0 exceptions; 45 disabled not run | 3619 s |

**ASan + UBSan stage.**

- **Settings.** `ctest --preset asan -j32 --timeout 7200`, run from the source root with
  `ASAN_OPTIONS=detect_leaks=1:halt_on_error=0` and `UBSAN_OPTIONS=print_stacktrace=1:halt_on_error=0`.
- **Diagnostics: 0 AddressSanitizer, 0 UBSan runtime errors, 0 LeakSanitizer.**
- **Slowest test.** `CompressibleCoupledProductionCaseTest.RepeatedRunIsDeterministic` took 3618.6 s,
  50 % of the timeout. `NaturalConvectionValidation.GridConvergence` took 3285.1 s.
- **P12-ASAN-001 holds.** `MeshQualityReport.DisconnectedMeshIsFatal` passes under ASan.
- **C13 permitted failures.** C13 allows exactly one known failure, but none occurred.

**The 9 new C13 tests** (`GradientBoundaryConsistency.*`) pass in all three builds.

**Generated outputs** ([regr_05](../a2/logs/regr_05_generated_outputs.log),
[regr_06](../a2/logs/regr_06_restore.log), [logs/regr_07](logs/regr_07_finalize_outputs.log)):

- **The stage-0 snapshot** holds 270 files: `results/validation`, `cases/*/results` and
  `tests/data/cases/*/results`.
- **Release-stage outputs vs the snapshot:** 220 identical, 50 timing-only, **0 VALUES, 0 NEW,
  0 GONE**.
- **Final tree vs the snapshot:** 220 identical, 45 timing-only, 5 VALUES. That comparison's
  `exit 1` is explained, not a defect:
  - The final tree was last written by the ASan build (Debug, `-O0`, sanitized). Its floating-point
    results differ from Release by optimization-level round-off.
  - The 5 files are the same ones W10 recorded: `distorted_mesh_mms.json`,
    `distorted_mesh_momentum_mms.json`, `distorted_mesh_simple_mms.{json,md}` and
    `curved_channel_multiblock_grid_convergence.json`.
  - The differences are about 1e-9 relative, plus quantities at the iterative floor (32×32
    continuity L1 5.74e-10 → 5.42e-10).
  - The Release copies of these files are value-identical to the snapshot.
- **Restore (stage 6).** All 50 files were restored from the snapshot and 0 kept. Afterwards, 0
  snapshot files differ from the working tree.
- **`finalize_outputs.sh` had nothing to do** (no KEPT files). The 50 working-tree files that differ
  from the Release copies are exactly the 50 timing-only files.

**Result: C13 and C14 PASS.**

## 4b. C14: the A1-form criteria re-verified on the final library

A2 carried C1-A1, C3(b), C4a/C4b, C5, C6-A1, C7 and C9's translation clause over "in their A1 form".
A1 had measured them on `libcfdcore.a` `51e82ee8…`, before P12-DIFF-002 changed the library. C14
requires rerunning affected evidence after a source change, so the same A1 programs were rerun
unchanged on the final library `143a1dda…`
([tools/c14_reverify.sh](tools/c14_reverify.sh), [logs/c14_00_reverify.log](logs/c14_00_reverify.log),
2026-09-17T15:50Z). No criterion or threshold changed.

**Instrument identity.** `a1_envelope.cpp`, `diagnostics.cpp` and `run_a1.sh` match A1's freeze
hashes. A1 did not hash `a1_cavity.cpp`, `a1_convergence.cpp` or `a1_3d.cpp`. Each of the five
programs was therefore rerun on its A1 control library, and each **reproduced its A1 control log
exactly** (`04`, `00`, `06`, `08`, `13`).

**Final library against A1's run:**

| program | criteria | result |
| --- | --- | --- |
| `a1_envelope` | C1-A1, C4a, C4b, C6-A1 | **bitwise identical to A1** (0 differing lines): 68 PASS, 0 FAIL |
| `diagnostics` | C5 continuity sweep | **bitwise identical**: 8/8 PASS, Lipschitz quotient 1.563e-02 |
| `a1_convergence` | C3(b) | **bitwise identical** to A1 |
| `a1_3d` | C9, translation | **bitwise identical**: 14 PASS. The 2 FAIL lines are A1's recorded "3D deformed" failure (2.029e-03 and 5.341e-04), reproduced exactly; C2-A2 now governs that clause |
| `a1_cavity` | C7 static cavity translation | **changed, as DIFF-002's wall flux requires, and PASS**: worst 1.425e-13 at 16² and 5.206e-13 at 32², against 1e-9 (A1: 9.835e-14 and 4.889e-13). The LARGE-offset row is reported only, as frozen: 7.124e-05 (A1 7.193e-05). That mesh is outside C4b's valid-geometry domain |

`Gradient.cpp` is unchanged since A1 (`d24882a9…`). The gradient-only programs agree bit for bit,
and the one program that runs the flow solver still meets its bound.

## 5. The four-sweep convergence finding: classification

**B — known limitation / technical debt, not a GRAD-002 closeout blocker.**

- No frozen criterion requires the fixed four-sweep loop to converge beyond what it does. C2's
  production-sweep clause is Q16 at ≤ 1e-9, and it passes at 3.769e-10. C2-A2 is posed at the
  converged fixed point, which is exact.
- No committed case is affected. On the committed meshes the extra truncation is ≤ 6.4e-6 and
  falls with refinement (`fresh_prodmesh_*`).
- The fix, a convergence-controlled sweep loop, changes every skewed-mesh result. That is new
  numerical scope, not a correctness defect of this phase.
