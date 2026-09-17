# P12-DIFF-002 W10 — full regression: result

**Verdict: W10 PASSES.** It ran on 2026-09-17 from 11:58Z to 13:46Z. Driver:
[tools/run_w10.sh](tools/run_w10.sh); each stage has its own log.

W10's frozen requirement (`../acceptance_gate.md`) is Release and Debug + GUI with exact counts,
sanitizers at CI settings, and clang-format clean. The only failure it permits is the known
MESH-004 test defect. **That defect no longer fails:** P12-ASAN-001 fixed it, and
`MeshQualityReport.DisconnectedMeshIsFatal` passes under ASan. **No failure of any kind occurred.**

## Integrity ([logs/01_integrity_format.log](logs/01_integrity_format.log))

- The `src/` + `include/` tree hash is `42c3d9df…`, the FORMAT-001 value, at the start. It is the
  same after the run.
- Starting library: `143a1dda…`.
- **clang-format-18 `--dry-run --Werror`:** 567 files, exit 0, **0 violations**.
- **Every stage is a clean-first rebuild.** After each build, every test and CLI executable was
  checked to be newer than every source file: 0 stale.
- **One file under `src/`, `include/`, `tests/`, `apps/` or `cases/` changed during the run:** the
  GRAD-002 candidate `tests/unit/discretization/test_gradient_boundary_consistency.cpp`, created
  around 12:28Z. It is in no CMake target, so no W10 binary compiles it.

## Results

| stage | build | library | tests (exact) | ctest time |
|---|---|---|---|---|
| **Release**, GUI off ([02](logs/02_release.log)) | clean-first, 142 s, **0 warnings**; 42 executables, 0 stale | `143a1dda…`, the frozen library | **1923 / 1923 passed**, 0 failed, 0 timeouts; 45 disabled not run (1968 registered) | 184 s |
| **Debug + GUI**, Qt offscreen ([03](logs/03_debug_gui.log)) | clean-first, 239 s, 6 warnings, all the known `-Wconversion` debt in `apps/gui/SimulationControllerEditing.cpp:86-89`; 44 executables, 0 stale | `06d5567f…` | **1975 / 1975 passed**, 0 failed, 0 timeouts; 45 disabled not run (2020 registered) | 1683 s |
| **ASan + UBSan**, CI settings ([04](logs/04_asan_ubsan.log)) | clean-first, 293 s, **0 warnings**; 42 executables, 0 stale | `aef5c4ee…` | **1923 / 1923 passed**, 0 failed, **0 timeouts**, 0 exceptions; 45 disabled not run | 3806 s |

The ASan stage ran `ctest --preset asan -j32 --timeout 7200` from the source root, with
`ASAN_OPTIONS=detect_leaks=1:halt_on_error=0` and
`UBSAN_OPTIONS=print_stacktrace=1:halt_on_error=0`. Sanitizer diagnostics:

| sanitizer | diagnostics |
|---|---|
| AddressSanitizer | **0** |
| UBSan runtime errors | **0** |
| LeakSanitizer | **0** |

Tests of note, passing in all three builds:

- `LowMachRegressionTest.*`, **7/7**: the migrated LOWMACH-001 test;
- `StructuredQuadProductionCase.*` and `MultiBlockProductionCase.*`: the W8B tests;
- `MeshQualityReport.DisconnectedMeshIsFatal`: passes under ASan in 0.40 s, which verifies
  P12-ASAN-001.

## Generated outputs ([05](logs/05_generated_outputs.log), [06](logs/06_restore.log))

- **Stage 0** snapshotted the 167 tracked files under `results/validation/`.
- **The classifier checked those 167:**
  - **117 identical;**
  - **45 differ only in timing fields;**
  - **5 differ in values** (`distorted_mesh_mms.json`, `distorted_mesh_momentum_mms.json`,
    `distorted_mesh_simple_mms.{json,md}`, `curved_channel_multiblock_grid_convergence.json`).
    - The changes are about 1e-9 relative.
    - W10 runs Release, then Debug, then ASan, so the ASan (sanitized Debug) build wrote these
      files last, and its floating-point results differ from Release.
    - The snapshot copies are value-identical in every non-timing entry to what the current Release
      library writes. That was checked against the isolated Release copy of GRAD-002 A2 (tree
      `cur`, library `143a1dda…`).
- **Stage 6 ([tools/restore.sh](tools/restore.sh)) restored all 50 files** from the snapshot:
  - the 45 timing-only files with the classifier's `--restore`;
  - the 5 value files after an automatic Release-identity check.
- **Afterwards, 0 of the 167 snapshot files differ** from the working tree.
- **Instrument scope, disclosed.** The adapted MESH-006 classifier also lists 1040 "NEW" files.
  - Most are evidence files under `results/` (e.g. `results/p12-grad-002/…`) and untracked
    `cases/*/results` outputs.
  - The stage-0 snapshot did not capture them: its `cases/*/results` pathspec matched nothing.
  - They are not W10 value changes that could be classified. Tracked case outputs differ from
    HEAD by the accumulated, gated P12-MESH/GRAD/DIFF numerics (DIFF-002 W9A covers the committed
    cases).
