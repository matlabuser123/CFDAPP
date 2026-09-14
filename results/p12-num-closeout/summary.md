# P12-NUM closeout — evidence

**P12-NUM-001 through P12-NUM-007: COMPLETE.** This record covers the closeout only: audit, cleanup, documentation reconciliation, verification, commit, push and CI. The phase evidence stays in `results/p12-num-001/` … `results/p12-num-007/` and is referenced, not duplicated. No new feature and no new P12 scope was started.

- Date: 2026-09-14.
- Branch: `main`; upstream `origin` (github.com/matlabuser123/CFDAPP).
- Starting point: HEAD = `origin/main` = `fd9bae3416b7595b5a65f4b512f6cee6e3758419` (P11-GUI-005); all P12-NUM work uncommitted.
- Final: P12-NUM committed as `105383d`, CI fixes as `44b996a`, pushed; CI run 34839669398 is green on `44b996a39f3884bd64937c732a4c8c6e61723dd6` (§7).

## 1. Diff audit

Every changed or untracked file was classified before cleanup. There were 335 paths in total.

| class | modified | added |
|---|---|---|
| intended source (`src/`, `include/`, `apps/`) | 61 | 19 |
| intended tests (`tests/`, excluding data) | 49 | 36 |
| intended case/reference data | 2 (`validation/ghia/README.md`; `cases/heated_cavity/results/metadata.json`) | 2 (`validation/ghia/ghia_re1000_{u,v}.csv`) |
| regenerated test-case outputs (`tests/data/cases/*/results/metadata.json`) | 2 | — |
| intended documentation (`TODO.md`, `ROADMAP.md`, `README.md`, `docs/`) | 4 | 3 |
| intended phase evidence (`results/p12-num-00{1..7}/`, this directory) | — | 71 |
| test-output snapshots (`results/validation/{grid_convergence,mms,production,natural_convection/Ra1e3/20x20}`) | — | 84 |
| config/CI (`.gitignore`, `.github/workflows/ci.yml`) | 2 | — |
| generated build output, temporary/debug files, unrelated changes | none found | none found |

**The three `metadata.json` case outputs** differ only by the P12-NUM-004 `robustness` block, which the current code writes deterministically. They are updated reference outputs, not timing noise.

**The `results/validation/*` snapshots** follow the repository's existing convention of tracked test-output snapshots. They are deterministic except run times.

**Scans of the added lines found nothing to remove:**
- no scratch or debug code (`XXX`/`FIXME`/`HACK`, `getenv`, `#if 0`);
- no binaries;
- no file larger than 102 KB;
- no machine-specific paths in source.

## 2. Cleanup

- **Timing-only noise.** The verification runs rewrote 13 tracked `results/validation/**/validation.json` files. Only runtime fields changed (verified per file with `git diff -U0`), and all 13 were reverted with `git checkout`.
- **Evidence logs.** `focused_tests.log`/`explicit_runs.log` in `results/p12-num-00*` were silently ignored by the global `*.log` rule, although the phase summaries cite them. `.gitignore` gained `!results/p12-*/**/*.log`, mirroring the existing `!results/release/**/*.log` carve-out.
- **Machine paths in logs.** In the evidence logs, 74 occurrences of the local repo root were normalised to `<repo>`, and this directory's logs got the same treatment. No value was changed.
- **`git diff --check`** initially reported 985 findings, all in generated evidence (report Markdown and test logs), none in source. Root cause: the NUM-005/006/007 Markdown writers emitted trailing spaces in check details and a trailing blank line at EOF.
  - Fixed at the source: `src/validation/MarkdownText.hpp` (`detail::tidyMarkdown`) is applied by `gridConvergenceReportMarkdown`, `mmsReportMarkdown` and `validationReportMarkdown`. Content is unchanged.
  - Hygiene assertions were added to `ProductionValidationTest.ReportIsDeterministicExceptRuntime` and `MMSStudyTest.ReportIsDeterministicExceptRuntime`.
  - The 55 existing evidence files were normalised identically (whitespace only).
  - `git diff --check` is now clean.
- **clang-format.** CI's `format` job (`clang-format-18 --dry-run --Werror`) would have flagged 993 violations. All C++ under `include/`, `src/`, `apps/` and `tests/` was formatted with clang-format-18 (18.1.8, the CI-pinned major version), leaving 0 violations.
  - Seven reformatted files belong to already-committed P12-COMP work, not P12-NUM. They are proven whitespace-only (token stream identical to HEAD with all whitespace removed) and go into a separate style commit (§7).
- **Sign-conversion warnings.** A local clang Debug build found 10 `-Wsign-conversion` warnings in P12-NUM tests: range-for over an `int` initializer list bound to `Index`. They were fixed with `std::initializer_list<Index>{…}` or a `std::size_t` counter; no value changed.

## 3. Evidence audit

- `results/p12-num-001/` … `results/p12-num-007/` each hold a `summary.md`. Every repository path and evidence file cited in the seven summaries exists (automated check: 0 missing).
- **Per-phase full-regression counts** agree with the historical record: 1381, 1417, 1506, 1579, 1603, 1634 and 1654. NUM-004's "1578/1579" is its disclosed first run, fixed before close.
- **NUM-007 backward-facing-step refinement** (Re = 800, lengths in step heights h; `results/p12-num-007/summary.md` §8):

| cells/H | x_r/h | upper bubble (x_rs − x_s)/h |
|---|---|---|
| 20 | 10.085 | 12.143 |
| 30 | 11.405 | 11.863 |
| 40 | 11.780 | 11.626 (**failed** 10.60–11.52; recorded, `backward_facing_step_20_30_40.json`) |
| 50 | 11.932 | 11.506 |

  - Lower-wall reattachment against the published 11.48–12.20: PASS.
  - Upper-wall bubble against 10.60–11.52: PASS. The 30/40/50 triplet is asymptotic, with Richardson 11.238.
  - The 40 cells/H failure is preserved as history.

## 4. Documentation reconciliation

- `TODO.md` (owner's simplified structure) and `ROADMAP.md` (P12-NUM under Completed) agree: NUM-001 … NUM-007 are `[x]`; the P12-NUM scope ends with NUM-007; P12-MESH is proposed, not authorized; any further P12 work needs a new explicit scope decision.
- `README.md` corrections:
  - it now states that the P12 work is on `main` but not in a tagged release;
  - it lists the P12-NUM numerical methods (and GMRES);
  - the Validation section now points to `docs/validation/` and `results/` instead of the per-item TODO breakdown that no longer exists;
  - the known limitations now include no Rhie–Chow interpolation and Cartesian-only production meshes.
- `CLAUDE.md`, `docs/user_guide/case_format.md` (documents every P12-NUM `solver.json` option the parser accepts) and `docs/validation/` needed no further change.

## 5. Verification (exact tree to be committed)

**Toolchain:** WSL2 Ubuntu 22.04, GCC 11.4.0, CMake 3.22.1, Ninja 1.10.1. Also used: clang 14.0.0 (extra build) and clang-format 18.1.8.

| check | configuration | result |
|---|---|---|
| format (CI gate) | `clang-format-18 --dry-run --Werror` over include/src/apps/tests | 0 violations |
| clang build (CI compiles with clang) | clang 14, Debug, whole tree | builds, 0 errors; 0 warnings in P12-NUM files |
| Release + Debug builds | `build/release` (Release, GUI off), `build/debug` (Debug, GUI on) | both build; 0 warnings outside third-party/GUI code |
| focused tests | Release; validation units, MMS units, grid convergence, BFS, robustness | **52/52 passed** (56 listed, 4 disabled), 57.0 s (`focused_release.log`) |
| **full regression** | `build/release`, `ctest -j32` | **1614/1614 passed, 0 failed, 1639 listed, 25 disabled**, 150.5 s (`full_release.log`) |
| GUI tests (not built in the Release tree) | `build/debug`, `CaseEditingTest` + `SimulationControllerTest` | **40/40 passed** (`gui_tests_debug.log`) |
| sanitizers | `--preset asan` (ASan + UBSan), CI's ASAN/UBSAN options, `-j16` | **0 ASan/UBSan/leak diagnostics**. 1608/1614 passed within the 1500 s CTest default; the 6 longest exceeded it and then passed with no limit (`asan_full.log`, `asan_long.log`) |

- **The 1639 vs 1679 difference** is exactly the 40 GUI tests. The Release tree is configured without the GUI; the Debug tree includes it. So 1614 + 40 = 1654 distinct enabled tests passed, matching the previous 1654/1654 baseline (1679 listed, 25 disabled).
- **Longest tests under ASan** (local): `CompressibleCoupledProductionCaseTest.RepeatedRunIsDeterministic` 2220 s, `NaturalConvectionValidation.GridConvergence` 1623 s, `SIMPLEMMS.DistortedMesh` 1423 s, the other four coupled-compressible cases about 1280 s.

## 6. CI baseline and the two pre-existing failures

CI (`.github/workflows/ci.yml`) was already red before P12-NUM. On HEAD `fd9bae3` (run 34705436662):

| job | conclusion |
|---|---|
| build-test gcc-debug, gcc-release, clang-debug | success |
| clang-tidy | success |
| python | success |
| **format** | failure: 107 clang-format-18 violations in 11 P12-COMP-era files |
| **sanitizers** | failure: all 5 `CompressibleCoupledProductionCaseTest` cases hit `--timeout 1800`; zero sanitizer diagnostics |

The previous 5 CI runs (P12-COMP-001/002 and earlier) also failed.

- **The format fix is §2.**
- **The sanitizer fix is the per-test hang guard.** `--timeout` was raised from 1800 s to 7200 s, following the workflow's own precedent (900 → 1800 s for runner speed).
  - The runner measured 1.12–1.43× (median 1.29) slower than the local workstation on 49 common tests, so the longest test needs up to about 4000 s there.
  - 7200 s keeps at least 1.8× headroom.
  - The projected job time is about 3.5 h, within the 6 h limit.
  - No sanitizer flag, tolerance or enabled/disabled-test change was made, and every test still runs under ASan.
- **Debug jobs** run serially with the 1500 s CTest default. The baseline test steps took 76 min (clang) and 84 min (gcc). With the measured test-time growth (×1.91) they are projected at about 2.5–2.7 h, and the heaviest single Debug test stays under 1500 s.

## 7. Commit, push and CI

**Commits** (branch `main`, parent `fd9bae3`):

| commit | content |
|---|---|
| `105383d1c026f2cad1b19753250e4a931065cca0` | `feat(numerics): complete P12 numerical methods and validation`: all P12-NUM-001..007 work plus the closeout fixes (328 files) |
| `44b996a39f3884bd64937c732a4c8c6e61723dd6` | `ci: fix the pre-existing format and sanitizer-timeout CI failures`: 7 whitespace-only P12-COMP files and the ASan hang guard 1800 → 7200 s (8 files) |

**Push:** `git push origin main` (`fd9bae3..44b996a`, no force). Afterwards HEAD = `origin/main` = `44b996a39f3884bd64937c732a4c8c6e61723dd6`.

**CI:** workflow `CI`, run **34839669398** (<https://github.com/matlabuser123/CFDAPP/actions/runs/34839669398>), event `push`. The head SHA is **`44b996a39f3884bd64937c732a4c8c6e61723dd6`**, equal to the local closeout SHA. Conclusion: **success**, 2026-09-14 11:43 → 14:31 UTC.

| job (ID) | conclusion | result |
|---|---|---|
| format (103961443545) | success | clang-format-18 clean (was failing at baseline) |
| python (103961443754) | success | |
| clang-tidy (103961443841) | success | |
| build-test gcc-release (103961443923) | success | 1614/1614 passed, 25 disabled, 1082.9 s |
| build-test clang-debug (103961443819) | success | 1614/1614 passed, 25 disabled, 4334.5 s |
| build-test gcc-debug (103961443833) | success | 1614/1614 passed, 25 disabled, 8483.1 s |
| sanitizers (103961443848) | success | 1614/1614 passed, 25 disabled, 9846.0 s; 0 ASan/UBSan/leak diagnostics; 0 timeouts (was failing at baseline) |

The longest tests under CI ASan were `CompressibleCoupledProductionCaseTest.RepeatedRunIsDeterministic` (3570.7 s) and `NaturalConvectionValidation.GridConvergence` (2634.1 s). Both are within the §6 projection of up to about 4000 s, and both would have failed at the former 1800 s.

**Test counts:** CI builds without the GUI, so it lists 1639 tests (1614 enabled + 25 disabled), the same as the local Release tree. The 40 GUI tests passed locally (§5).

**This record itself** (TODO/ROADMAP/closeout updates) is a documentation-only follow-up commit on top of `44b996a`. Its own CI run is reported in the handoff, not here: recording it would need yet another commit.

## 8. Known remaining limitations

These are unchanged by the closeout; details are in each phase's evidence.

- **Pressure fields:** no Rhie–Chow interpolation, so pressure fields carry an undamped odd-even mode on open domains. The validation estimators are immune to it.
- **Grids:** production cases use uniform Cartesian grids. The non-orthogonal/skewness machinery is exercised only on test-generated meshes.
- **Benchmarks:** Ghia et al. (1982) is itself a numerical benchmark (Re = 100 floor about 2e-3). The turbulent-channel reference is DNS summary statistics plus the log law, not a complete DNS-profile validation, and the inlet/outlet channel is not momentum-developed.
- **Operators and schemes:** first-order boundary-ring truncation in some operators, and upwind-only scalar transport.
- **BFS evidence:** the upper-bubble pass at 50 cells/H is by a narrow margin (11.506 vs ≤ 11.52). The in-repository test covers 20/30/40 cells/H only; the 50 cells/H level is a recorded driver run through the unchanged implementation.
- **Deferred capabilities:** multigrid, additional preconditioners, a coupled pressure-based solver, compressible energy and higher Mach.
