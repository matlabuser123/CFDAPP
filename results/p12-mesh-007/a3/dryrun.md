# P12-MESH-007 Amendment A3 — dry-run record (before the freeze)

This record belongs to [../acceptance_gate_A3.md](../acceptance_gate_A3.md).

- **Driver:** [../tools/g9_a3.sh](../tools/g9_a3.sh) in `dry` mode.
- **Final dry sequence:** 2026-09-17T16:42Z–17:17Z, all stages, run with the exact scripts that are
  frozen ([../logs/3x_a3_dry_driver.log](../logs/3x_a3_dry_driver.log)).
- **Nothing here is a gate result.** The gate is executed fresh after the freeze (logs 40–45).

## 1. Final dry sequence

| stage | log | result |
| --- | --- | --- |
| nom7 build + fidelity | [30](../logs/30_a3_dry_build_nom7.log) | **FIDELITY PASS**: F1 (19 changed + 10 absent = the 29-file MESH-007 set), F2 (42 differences from BASE, all attributed to DIFF-002 / GRAD-002 / ASAN-001), F3 (two insertions carrying `boundaryLineIntersection` and `boundaryInwardStencil`). Build 0 warnings. `libcfdcore.a` `4421c826…`, the same hash in all three dry builds (F5). `cfdapp` `50d890a7…` |
| G9.1 | [31](../logs/31_a3_dry_g91_bitprobes.log) | **PASS**: all 15 probe runs exit 0 and are complete. bitprobe7, probe 5 and probe 6 are each **bitwise identical** nom7 vs NEW. N2: NEW twice identical for all three. **N1: cur vs nograd differ by 35 / 18 / 51 lines**; bitprobe7's geometry lines are identical (0) and its Q16 channel lines differ (11). Probes 5/6 ran on 16 of the 19 committed cases (the 2D ones), listed in the log |
| G9.1 fail-closed self-test | [31 selftest](../logs/31_a3_dry_g91_failclosed_selftest.log) | **PASS**: real outputs accepted. 4 corruptions rejected: a replica of the vacuous first run, a non-zero exit, a missing bitprobe7 item, a missing probe-6 case |
| G9.2 | [32](../logs/32_a3_dry_g92_cli.log) | **C1:** 35/35 IDENTICAL, 0 INVALID, so every `metadata.json` key is reproducible. **N3:** nograd vs NEW 14/35 IDENTICAL; 21 valid runs DIFFERENT, `poiseuille_distorted` among them in every exported file, metadata and stdout. **G9.2:** nom7 vs NEW **35/35 IDENTICAL, 0 INVALID** |
| G9.2 fail-closed self-test | [32 selftest](../logs/32_a3_dry_g92_failclosed_selftest.log) | **PASS**. Real runs: 4 IDENTICAL. Crashing binary: 4 INVALID. Lost `solution.vtk`: 2 INVALID, while the exit-2 and exit-3 fixtures stay valid. Metadata tampered on one side: 3 DIFFERENT, not IDENTICAL |
| G9.3 | [33](../logs/33_a3_dry_g93_inputs.log) | **PASS**. MESH-007's list holds no input file. nom7 vs current `cases/` + `tests/data/`: 0 of 211 differ. BASE vs current: 4 changed files (the two W8 cases' `case.json` and `solver.json`, DRIFT-001 as recorded by DIFF-002 W8B), all attributed. Self-test PASS: a planted nom7 change and an unattributed input are both flagged |
| G9.4 | [34](../logs/34_a3_dry_g94_outputs.log) | **PASS**. The NEW copy differs from the repository in 0 files, and its library equals `build/release` (`143a1dda…`). nom7 suite **1885/1885**; NEW suite **1932/1932**; the 47-test difference is MESH-007's 38 ALE tests plus GRAD-002's 9 C13 tests. Generated outputs: 270 checked, 220 identical, 50 timing-only, **0 VALUES, 0 NEW, 0 GONE** |

**Non-vacuity of G9.4's classifier.** It is not re-derived here. The same classifier reported
5 VALUES files at the ~1e-9 level in the GRAD-002 A3 regression
(`results/p12-grad-002/a2/logs/regr_05_generated_outputs.log`, the final tree against the snapshot),
and 58 VALUES files in GRAD-002 C11(b).

## 2. Attempts that exposed instrument defects (all preserved)

Each defect was found by the dry run itself, before any freeze, and fixed in the instrument only.
No production source, test, threshold or criterion changed. The G9.1 and G9.2 fail-closed changes
follow the user's instruction relayed on 2026-09-17: fix the instrument, re-dry-run, and freeze only
if everything passes.

1. **`30_…run1-verification-listed-generated-outputs.log`.** `build_nom7.sh`'s verification
   listing included `tests/data/cases/*/results/*`. Those are generated CLI outputs that the rsync
   deliberately excludes, so 16 files showed as "absent from nom7". Fix: the listing excludes
   `results/` directories, and `nom7_fidelity.py` now computes F1–F3 verdicts. It stops the build on
   failure.
2. **`31_…run1-probes5-6-aborted-on-3D-case.log`: vacuous.**
   - **What happened.** The driver passed every committed case to the MESH-005 and MESH-006
     probes, which are 2D probes. Both aborted at the first 3D case, and `abort()` discarded their
     buffered output. All five variants therefore printed the same 2-line error, and "BITWISE
     IDENTICAL" compared two crashes.
   - **How it was caught.** N1 showed 0 differing lines for probes 5 and 6.
   - **Fix.** The probes now run on the 2D committed cases only, as MESH-006's G10.1 ran them. Every
     run must exit 0 and be complete, N1 is checked per probe, and there is an overall verdict
     line.
   - **Later G9.1 attempts:**
     - `run2` (2D + exit check);
     - `run3` (+ completeness; before the check was moved into a function);
     - `31_…failclosed_selftest.run1-on-run3-outputs.log`.
3. **The inherited `g9_compat.sh` (MESH-005's procedure) was not fail-closed**
   (`32_…pre-final-script.log`).
   - **Crashes.** Two runs that crashed identically, or exported nothing, counted as IDENTICAL.
   - **Partial differences.** Its verdict started from "IDENTICAL" and appended
     " DIFFERENT(metadata)" or " DIFFERENT(stdout)". A case with identical files but a different
     `metadata.json` or stdout still matched `IDENTICAL*`. In that log's N3 block,
     `poiseuille_distorted` shows only the last differing file, because the file loop overwrote the
     verdict.
   - **Fix:**
     - differences are collected first;
     - each side must show the outcome the code and test suite imply: exit codes from
       `tests/CMakeLists.txt` and `test_case_lid_driven_cavity.cpp`, and exported files from
       `ResultExporter` / `ProjectRunner`;
     - otherwise the case is INVALID.
   - **Historical check** (by the read-only audit session). MESH-005 logs 12 and 21 and MESH-006
     `a3/logs/13_g10_2` contain **0** lines of the masked form "IDENTICAL DIFFERENT…"; every
     verdict there is a plain IDENTICAL with the expected exit codes and file counts. The defect
     therefore masked nothing in MESH-005's or MESH-006's recorded CLI compatibility.
4. **`33_…run1-no-selftest.log`.** G9.3 had no negative control. A `--self-test` was added.
5. **`*.pre-final-script.log`.** These are dry logs produced by earlier versions of `g9_a3.sh`. The
   final version added the fresh-mode checks: the frozen artifacts, the NEW library before the
   first stage, and the nom7 library after its rebuild, plus the fail-closed nom7 stage. The whole
   dry sequence was then rerun with the final scripts (section 1).

**Incident (disclosed).** `g9_a3.sh` was edited while its `g92` stage was running (the G9.3
self-test edit). Bash had already parsed the stage loop, so the running G9.2 was unaffected; its log
was complete and closed. After the loop, bash read the edited file at the old offset and ran a
fragment: a non-existent command (`ut_nom7_dry`, exit 127), two `echo`s to the task's stdout, and
then a syntax error. Nothing else ran. From then on, no tool file was edited while a stage was
running.

## 3. What is frozen

`logs/39_a3_freeze.log` hashes:

- the gate, its A3 amendment and this record;
- every instrument: `g9_a3.sh`, `g9_compat.sh`, `build_nom7.sh`, `nom7_fidelity.py`,
  `g93_inputs.py`, `bitprobe7.cpp`, the MESH-005/006 probes, GRAD-002's `classify_scope.py` and
  MESH-006's classifier;
- every dry log above, the preserved attempts included.

It also records the NEW library (`143a1dda…`) and the nom7 library (`4421c826…`) as `FROZEN-LIB`
lines. The fresh execution (`g9_a3.sh fresh`) re-verifies all of them and fails closed on any
mismatch.
