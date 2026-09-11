# P9 — v0.2.0 Release Qualification — Summary

**Status: 7 of 7 gates PASS.**

Commits pushed to `main` on `github.com/matlabuser123/CFDAPP`:
`4e5a4d23f6b36203be9ef46dd4ec74880b1a06fe` (P6 GPU / P7 performance / P8
hardening substantive work, 108 files), `21e493e7122fbc4ee3af93b555e1c65b826a80f1`
(version bump to 0.2.0 + release-script fixes), `267bbbd61268321527c80aa086e7e7e119fa93ec`
(release evidence + `.gitignore` fix), and `8a8af792060414dcd5d3bb28c7b9b9cac3814058`
(CI sanitizer-job scheduling fix — the exact commit gate 4 is verified against).

## Gate 1 — GPU production path is stable: ✅ PASS (carried forward from P6/P7)

Evidence: `TODO.md`'s P6-GPU-001/002/003 and P7-PERF-003 sections,
`results/performance/large_grid_stress/`. Persistent GPU field/matrix
residency, GPU CG/BiCGSTAB in the production SIMPLE path, GPU-resident Jacobi
preconditioning, robust CPU fallback, and large-grid stress testing (largest
stable GPU grid 640×640, zero NaN/Inf at every attempted grid including
failed ones, failures are clean `PressureCorrectionFailure` never blow-up)
were all verified on **WSL2/Ubuntu 22.04 with `-DCFDAPP_ENABLE_CUDA=ON` on
real GPU hardware** (see `TODO.md`'s "Verify Linux build" gate). No
GPU/solver code has changed since that evidence was generated — it and the
current working tree are the same commit's content — so it remains valid for
this release without a rerun.

**Caveat, verified directly in this session:** this native Windows
build/package (`build/windows-release`) has `CFDAPP_ENABLE_CUDA=OFF`. Per
`src/gpu/GpuLinearSolver.cpp`'s own header comment, `makeGpuCG`/
`makeGpuBiCGSTAB` return `nullptr` in that configuration and
`LinearSolverFactory` transparently falls back to the CPU solver — so
`SIMPLEGpuSolverTest.*` passing in `results/release/v0.2.0/
ctest_and_smoke_windows_release.log` verifies the CPU-fallback path only, not
real GPU hardware, on this machine. This Windows machine does have a real
NVIDIA GPU (RTX 5000 Ada) and a CUDA toolkit installed, but a native
Windows+CUDA build/test pass has never been performed for this project (only
Linux+CUDA has) — that remains a real, pre-existing gap, not something this
session introduced or is claiming to have closed.

## Gate 2 — CPU/GPU equivalence passes: ✅ PASS (carried forward from P6/P7)

Evidence: `tests/solver/simple/test_simple_gpu_solver.cpp`'s
`GpuBackendReproducesCpuCavitySolutionWithinTolerance` (production-SIMPLE
level, not isolated kernels) plus `TODO.md`'s P6-GPU-001's "Verify CPU/GPU
numerical equivalence" and P6-GPU-002's "Add convergence/equivalence tests",
run as part of the 1289/1289 full regression suite on the CUDA-enabled WSL2
build against real GPU hardware. Same carried-forward basis and same
Windows-CUDA-OFF caveat as gate 1 above.

## Gate 3 — Performance benchmarks are documented: ✅ PASS (carried forward from P7)

Evidence: `results/performance/cuda_end_to_end/`,
`results/performance/openmp_scaling/`, `results/performance/
large_grid_stress/`, `results/performance/preconditioner/` — all real,
measured data (20×20 through 320×320 CUDA end-to-end, 1/2/4/8+ thread OpenMP
scaling, large-grid stress to 640×640 GPU / 480×480 CPU), summarized in
`README.md`. No solver/GPU code changed since these were measured, so no
rerun was needed per this gate's own "only rerun if code changed" rule.

## Gate 4 — Full CI is green: ✅ PASS

`main` was pushed to `origin` (`git push origin main`), confirmed landing at
the exact pushed SHA via both `git fetch`/`rev-parse` and the GitHub API
(`gh api repos/matlabuser123/CFDAPP/commits/main`).

**First run** — `34601816351` at commit `267bbbd61268321527c80aa086e7e7e119fa93ec`:
**FAILED**. Root cause (confirmed by reading the job log directly, not
inferred): the `sanitizers` job ran `ctest --preset asan -j8 --output-on-failure
--timeout 900`, tuned against the 32-core local workstation this repo's P8
evidence was measured on. GitHub-hosted `ubuntu-latest` runners carry only 4
vCPUs (confirmed via the run's job metadata), so `-j8` oversubscribed 2×;
combined with ASan/UBSan's own 2-3× overhead, 6 of the heaviest validation
tests (SST convergence, natural-convection 15×15, channel-flow comparisons)
exceeded the 900s per-test timeout. Grepped the full failed-job log for
`sanitizer|abort|segmentation|SIGSEGV|SIGABRT|heap-buffer|stack-buffer|
use-after` (case-insensitive): zero matches outside the job/step name
strings themselves — a scheduling regression, not a memory-safety or UB bug.

**Fix** — commit `8a8af792060414dcd5d3bb28c7b9b9cac3814058`: changed
`.github/workflows/ci.yml`'s sanitizers job to `ctest --preset asan
-j$(nproc) --output-on-failure --timeout 1800` (same convention the Build
step already used, correctly sized to the actual runner; timeout raised for
headroom on shared/weaker CI hardware). No sanitizer flag, tolerance, or
disabled-test changed.

**Retest** — run `34605885487` at commit `8a8af792060414dcd5d3bb28c7b9b9cac3814058`:
**PASS**. Verified via `gh run view 34605885487 --json headSha,conclusion,status`
→ `{"conclusion":"success","headSha":"8a8af79...","status":"completed"}`, and
`headSha` matches local `HEAD`/`origin/main` exactly (nothing else was pushed
in between). All 7 jobs green:

| Job | Result | Duration |
|---|---|---|
| format | ✅ | 53s |
| sanitizers | ✅ | 59m17s |
| build-test (gcc, debug) | ✅ | 37m26s |
| clang-tidy | ✅ | 3m8s |
| python | ✅ | 26s |
| build-test (clang, debug) | ✅ | 39m10s |
| build-test (gcc, release) | ✅ | 8m5s |

Run URL: `https://github.com/matlabuser123/CFDAPP/actions/runs/34605885487`.

## Gate 5 — GUI acceptance passes: ✅ PASS

17/17 PASS, human-executed by the project owner against the rebuilt
`cfdapp_gui.exe`. Full results and the one bug found/fixed mid-run (quoted
Directory-field path) in `results/release/p8-hardening/gui_acceptance.md`.

## Gate 6 — Packaged CLI/GUI smoke tests pass: ✅ PASS

Built the actual release package (`build/windows-release`, Release config,
`CFDAPP_BUILD_GUI=ON`, `CFDAPP_ENABLE_PACKAGING=ON`,
`CFDAPP_VERSION_SUFFIX=""`) via `scripts/windows-release/{1,2,3,4}-*.ps1`
(two real, pre-existing bugs in these scripts fixed along the way — see
`environment.txt` / commit `21e493e`'s message). Full detail in
`ctest_and_smoke_windows_release.log` (1290/1290 active tests pass,
build-tree GUI/CLI smoke) and `packaged_smoke_test.log` (the actual extracted
package, tested from a PATH with no Qt/Visual Studio on it — CLI `--version`
reports exactly `0.2.0`, `--help` works, an invalid case path fails cleanly
(exit 2, no crash), a real packaged example case runs to convergence, and the
packaged GUI launches and stays alive).

One non-blocking finding: five QML console warnings
(`Unable to assign [undefined] to bool` in `PhysicsEditor.qml`/
`BoundaryEditor.qml`) appear on GUI startup before any case is open, in both
the build-tree and packaged GUI. The app stays alive and fully functional —
this is why the 17-step manual checklist (which only opens an existing case)
never caught it. Worth a follow-up QML binding-guard fix; not a release
blocker.

## Gate 7 — Release artifacts verified: ✅ PASS

- Version: exactly `0.2.0` (`CFDApp 0.2.0` from the packaged CLI; no `-dev`
  suffix, confirmed by the `CFDAPP_VERSION_SUFFIX=""` fix above).
- Filenames: `CFDApp-0.2.0-Windows-x64.zip`, `CFDApp-0.2.0-Windows-x64.exe`
  (NSIS installer) — correct, version-matched.
- CLI (`cfdapp.exe`) and GUI (`cfdapp_gui.exe`) both present in `bin/`.
- Full Qt 6.9.3 runtime + plugins (`platforms`, `qml`, `imageformats`, `tls`,
  `networkinformation`) and the MSVC redistributable installer bundled.
- `docs/` (7 files) and `licenses/LICENSE` included.
- `examples/` bundled, including `lid_driven_cavity` (the case actually run
  in the smoke test).
- Package extracts correctly (`Expand-Archive`, no errors) and both packaged
  smoke tests pass (see gate 6).
- SHA-256 checksums generated: `checksums.txt`.
- Commit SHA, build date, and full toolchain recorded: `environment.txt`.

## Bottom line

```
P9: 7/7 PASS

Gate 1 GPU production path stable:     PASS (Linux/CUDA evidence, carried forward)
Gate 2 CPU/GPU equivalence:            PASS (Linux/CUDA evidence, carried forward)
Gate 3 Performance benchmarks:         PASS (P7 data, unchanged)
Gate 4 Full CI green:                  PASS (run 34605885487 @ 8a8af79, all 7 jobs)
Gate 5 GUI acceptance:                 PASS (17/17, human-executed)
Gate 6 Packaged CLI/GUI smoke tests:   PASS
Gate 7 Release artifacts verified:     PASS
```

All 7 release gates pass. Final release-candidate commit:
`8a8af792060414dcd5d3bb28c7b9b9cac3814058`, with green CI verified directly
against that exact SHA (not an earlier or later commit). v0.2.0 is ready to
tag and publish from this commit.
