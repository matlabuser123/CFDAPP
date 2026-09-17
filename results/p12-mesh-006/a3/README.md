# P12-MESH-006 — Amendment A3 evidence

The amendment itself: [../acceptance_gate_A3.md](../acceptance_gate_A3.md) (sha256 `ad19946c…`,
frozen 2026-09-15T12:36:10Z). The original gate [../acceptance_gate.md](../acceptance_gate.md) and
its failure are unchanged. The investigation that led to A3:
[../g5-investigation/README.md](../g5-investigation/README.md).

```text
Original pre-registered gate (acceptance_gate.md)
        ↓
G5.1 / G6.2 FAILED (logs/05d, 06 of the phase; summary.md §17, §18, §30)
        ↓
implementation stopped (MESH-006 BLOCKED / FAILED GATE)
        ↓
independent investigation (g5-investigation/)
        ↓
classification D — PRE-REGISTERED THRESHOLD DESIGN DEFECT
        ↓
user-authorized A3 amendment (2026-09-15)
        ↓
A3 frozen before the rerun (logs/00_freeze.log)
        ↓
fresh acceptance run: A3 G5 PASS, A3 G6 PASS (logs/04_a3_gate.log)
        ↓
remaining MESH-006 work, verification, final decision (../summary.md §31–§34)
```

## Freeze and fresh run

| file | content |
| --- | --- |
| [logs/00_freeze.log](logs/00_freeze.log) | sha256 of acceptance_gate_A3.md, the frozen expected values, the evaluator, the run script and the investigation tools used, taken at 2026-09-15T12:36:10Z; no fresh-run directory existed |
| [logs/00a_evaluator_dry_run_NOT_acceptance.log](logs/00a_evaluator_dry_run_NOT_acceptance.log) | declared dry run of the evaluator on the investigation's outputs, to catch coding errors before the freeze (not the acceptance run) |
| [data/frozen_expected.json](data/frozen_expected.json) | the independent expected values: discrete solutions, analytical point values, predicted errors, n = 24 envelopes (tools/freeze_expected.py; no CFDApp code or output) |
| [logs/01_build_and_freeze_binaries.log](logs/01_build_and_freeze_binaries.log) | Release rebuild (0 warnings); frozen `cfdapp` sha256 `df4ee6d0…` and `CFDCaseIntegrationTests` sha256 `3f039122…` |
| logs/02_cli_{x8,x16,x24,y16,z16}.log | the fresh production CLI runs (commands, timing, fields.csv sha256) |
| logs/03_level_n{8,16,24}.log | the fresh ProjectRunner level runs of the unchanged G5 test code, relocated so the original data are never touched |
| [logs/04_a3_gate.log](logs/04_a3_gate.log), [data/a3_gate_result.txt](data/a3_gate_result.txt), [data/a3_gate_result.json](data/a3_gate_result.json) | the A3 evaluation: **46/46 items pass — A3 G5 PASS, A3 G6 PASS** |
| [logs/05_fresh_vs_investigation_fields_sha256.log](logs/05_fresh_vs_investigation_fields_sha256.log) | the fresh fields.csv are byte-identical to the investigation's (determinism; reported only) |

## Completion work after A3 PASS

| file | content |
| --- | --- |
| [logs/10_qmllint_gui.log](logs/10_qmllint_gui.log) | qmllint (Qt 6.2.4) of the three changed QML pages: 0 errors, no new warning kind |
| [logs/11_clang_format_apply.log](logs/11_clang_format_apply.log), [logs/11b_clang_format_check_after.log](logs/11b_clang_format_check_after.log) | clang-format-18 on the 16 flagged MESH-006 files (layout only: whitespace, comment re-wrap, literal splits, two include moves); afterwards 0 of 555 files would change |
| [logs/12_final_builds.log](logs/12_final_builds.log) | final Release and Debug+GUI builds, 0 warnings. The final `cfdapp` is byte-identical (sha256 `df4ee6d0…`) to the binary of the A3 acceptance run |
| [logs/13_g10_1_bit_identity_final.log](logs/13_g10_1_bit_identity_final.log), [logs/13_g10_2_cli_compat_final.log](logs/13_g10_2_cli_compat_final.log), [logs/13_g10_4_inputs_final.log](logs/13_g10_4_inputs_final.log) | G10 2D compatibility on the final sources |
| [logs/14_cpu_baseline.log](logs/14_cpu_baseline.log) | CPU baseline (lid cube Re = 100, 16³/32³/64³, single thread) |
| [logs/15_focused_tests_final.log](logs/15_focused_tests_final.log) | staged focused tests on the final binaries: gtest stages 1229/1229, GUI 52/52, CLI ctest 18/18 |
| [logs/16_full_regression_release.log](logs/16_full_regression_release.log) | G11 Release (-O3): **1862/1862 passed**, 44 disabled not run |
| [logs/17_full_regression_debug_gui.log](logs/17_full_regression_debug_gui.log) | G11 Debug + GUI (offscreen): **1914/1914 passed**, 44 disabled not run |
| [logs/18_full_regression_asan_ubsan.log](logs/18_full_regression_asan_ubsan.log) | supplementary (not part of G11): ASan + UBSan, `--timeout 1800`: 1858/1862; 3 Timeout, 1 heap-use-after-free — kept as run |
| [logs/18a_asan_diag_use_after_free.log](logs/18a_asan_diag_use_after_free.log) | the use-after-free is in the test code of `MeshQualityReport.DisconnectedMeshIsFatal` (P12-MESH-004); the test and `MeshQuality` sources are byte-identical to the pre-MESH-006 snapshot, and the snapshot built with the same sanitizer flags reproduces it — pre-existing, not fixed (out of scope) |
| [logs/18b_asan_rerun_timeouts_ci_timeout.log](logs/18b_asan_rerun_timeouts_ci_timeout.log) | the 3 timed-out tests rerun with the CI sanitizer job's own `--timeout 7200`: 3/3 passed (1107 s, 1260 s, 1670 s), 0 sanitizer diagnostics |
| [logs/19_g10_5_generated_outputs.log](logs/19_g10_5_generated_outputs.log), [logs/19b_g10_5_restore.log](logs/19b_g10_5_restore.log), [logs/19c_g10_5_after_restore.log](logs/19c_g10_5_after_restore.log) | G10.5: 580 regenerated outputs vs the pre-MESH-006 snapshot: 532 identical, 48 runtime-only, 0 values, 0 new; the 48 restored from the snapshot; afterwards 580/580 identical |
| [logs/20_files_changed_final.log](logs/20_files_changed_final.log) | every file changed or added vs the pre-MESH-006 snapshot (`../tools/m6files.sh`; its "A" entries include ignored/empty directories the snapshot does not contain) |
| [logs/21_final_integrity.log](logs/21_final_integrity.log) | after the documentation closeout: the original gate and original G5/G6 evidence unchanged (the investigation's start list); the A3 freeze list and frozen expected values unchanged; g5-investigation/ last modified 12:25:01Z, before the A3 freeze; clang-format 0 of 555 |

## Tools

- `freeze_expected.py`, `a3_gate.py`, `a3_run.sh`, `a3_dry_run.sh`, `fresh_vs_investigation.sh` — A3.
- `qmllint_gui.sh`, `format_check.sh`, `apply_format.sh`, `format_token_diff.sh`, `final_build.sh`,
  `g10_final.sh`, `cpu_baseline.sh`, `focused.sh`, `full_regression.sh`, `asan_diagnosis.sh`,
  `g10_5_classify.sh`, `classify_generated_outputs.py`, `status_check.sh`, `final_integrity.sh` —
  completion and verification.
