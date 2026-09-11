# CFDApp — TODO

**Current phase:** Performance & Production Hardening
**Released:** v0.1.5
**Priority:** GPU pipeline → GPU solvers → preconditioning → benchmarking → release

---

# P0–P5 — Core CFDApp ✅

* [x] Numerical foundation and SIMPLE
* [x] Validation and grid refinement
* [x] CLI and case system
* [x] Transient solver and PISO
* [x] Thermal and turbulence models
* [x] Species transport
* [x] Multiphase foundation
* [x] Compressible foundation
* [x] CPU/OpenMP/CUDA foundations
* [x] GUI case creation/editing
* [x] Visualization and post-processing
* [x] CI, packaging and release automation
* [x] Windows release `v0.1.5`

---

# P6 — GPU Performance ← CURRENT

## P6-GPU-001 — Persistent GPU Pipeline ✅

* [x] Keep fields resident on GPU
* [x] Keep sparse matrices resident on GPU
* [x] Minimize CPU ↔ GPU transfers
* [x] Reuse GPU buffers between iterations
* [x] Add transfer/timing instrumentation
* [x] Verify CPU/GPU numerical equivalence

**Acceptance:** production solve can execute repeated iterations without unnecessary host/device transfers.

## P6-GPU-002 — Production GPU Linear Solvers ✅

* [x] Integrate GPU CG
* [x] Integrate GPU BiCGSTAB
* [x] Connect solvers to production SIMPLE path
* [x] Implement robust CPU fallback
* [x] Add convergence/equivalence tests

**Acceptance:** production cases can select GPU CG/BiCGSTAB and reproduce CPU solutions within tolerance.

## P6-GPU-003 — Preconditioning ✅

* [x] Establish current solver baseline
* [x] Implement/improve Jacobi preconditioning
* [x] Investigate stronger GPU-friendly preconditioners
* [x] Measure iteration-count and runtime improvement
* [x] Add regression tests

**Acceptance:** GPU-resident Jacobi preconditioning (diag(A)^-1 built and applied entirely on
device, no per-iteration host round trip) is available to GPU CG/BiCGSTAB via
`LinearSolverSettings::preconditioner`, validated against CPU Jacobi and unpreconditioned
solves within tolerance, with regression tests and measured (not assumed) iteration-count and
wall-clock evidence in `results/performance/preconditioner/`. Stronger candidates (block Jacobi,
damped Jacobi, polynomial, approximate inverse, ILU(0)) were investigated and rejected for this
task's scope -- see the P6-GPU-003 final report for the evidence-based reasoning.

---

# P7 — Performance Validation

## P7-PERF-001 — CUDA End-to-End Benchmark ✅

* [x] Measure total CPU runtime
* [x] Measure total GPU runtime
* [x] Measure transfer overhead
* [x] Measure solver/assembly runtime
* [x] Calculate speedup
* [x] Identify GPU break-even grid size

Test at minimum:

* [x] 20×20
* [x] 40×40
* [x] 80×80
* [x] 160×160
* [x] 320×320
* [x] Larger grid if practical (640×640 attempted; CPU failed to converge within the shared
  linear-solver iteration budget at that scale -- documented as an invalid/excluded
  comparison, not fabricated, in `results/performance/cuda_end_to_end/summary.md`)

**Acceptance:** real end-to-end CPU-vs-GPU production SIMPLE timing measured at
`benchmarks/gpu/cfd_benchmark_cuda_end_to_end` -- never kernel-only. GPU break-even: **80×80**
(6,400 cells). Maximum measured speedup: **2.01x at 320×320**. Full results in
`results/performance/cuda_end_to_end/` (`runs.csv`, `summary.csv`, `metadata.json`,
`summary.md`, `environment_hardware.txt`).

## P7-PERF-002 — OpenMP Scaling ✅

* [x] Benchmark 1 thread
* [x] Benchmark 2 threads
* [x] Benchmark 4 threads
* [x] Benchmark 8+ threads where available
* [x] Measure speedup and efficiency
* [x] Identify remaining serial bottlenecks

**Acceptance:** real end-to-end production-SIMPLE OpenMP scaling measured at
`benchmarks/cpu/cfd_benchmark_openmp_scaling` (160x160, 60 outer iterations), corroborated by
the pre-existing pure-SpMV microbenchmark (`cfd_benchmark_spmv_scaling`). Audit finding: exactly
one `#pragma omp` exists in the whole codebase (`SparseMatrix::multiply`), which bounds and
explains every result. Best thread count: **4** (1.33x speedup, 9.28s median vs 12.33s at
1 thread). Scaling saturates by 4 threads and **regresses severely at 16/32 threads** (32
threads = 197s, 16x *slower* than 1 thread) -- a reproducible WSL2 thread-spawn-overhead effect
across the many small, frequent parallel regions a full SIMPLE solve triggers, corroborated by
the isolated-kernel microbenchmark showing the same shape. Numerical equivalence: exactly 0.0
max velocity difference from the 1-thread baseline at every thread count (bit-identical, not
just within tolerance) -- zero race-condition evidence. Full results and estimated ~78%
effective serial fraction (Amdahl, from measured data) in
`results/performance/openmp_scaling/summary.md`.

## P7-PERF-003 — Large-Grid Stress Tests ✅

* [x] Test memory usage
* [x] Test solver stability
* [x] Test CPU scaling
* [x] Test GPU scaling
* [x] Check NaN/Inf
* [x] Check mass conservation
* [x] Record convergence history

**Acceptance:** progressively larger lid-driven-cavity stress tests measured at
`benchmarks/gpu/cfd_benchmark_large_grid_stress`. **Largest stable CPU grid: 480x480**
(230,400 cells); **largest stable GPU grid: 640x640** (409,600 cells, one tier further than
CPU). Limiting factor at both backends is solver capacity, not memory: unpreconditioned
BiCGSTAB's pressure-correction solve fails to converge within the shared 5,000-iteration
budget at CPU 640x640 and GPU 800x800 (`SIMPLEStatus::PressureCorrectionFailure`,
reproducing P7-PERF-001's own independent 640x640 CPU finding). Zero NaN/Inf and zero mass
imbalance at every attempted grid, including the failed ones -- failures are clean
"iteration budget exceeded," never numerical blow-up. Host memory peaks at under 1GB even at
800,000+ cells (32GB machine) -- memory was never the limiting factor in the tested range.
Full results, per-grid convergence histories, and known limitations (including a documented
GPU-memory-measurement caveat) in `results/performance/large_grid_stress/summary.md`.

---

# P8 — Production Hardening ✅

* [x] Run full regression suite -- 1289/1289 (100%) on Linux (WSL2) and on a genuine native
  Windows MSVC build; 13 pre-existing `DISABLED_` slow grid-refinement cases excluded
  (documented, unrelated to this task).
* [x] Run parallel `ctest` -- `ctest -j8`: 1289/1289 (100%), 277.82s vs 1398.47s serial, no
  race conditions, no test-order/shared-resource issues.
* [x] Run sanitizers -- AddressSanitizer + UndefinedBehaviorSanitizer (`--preset asan`,
  `ctest -j8 --timeout 900`): 1250/1250 (100%; asan preset builds CPU-only, so the
  CUDA-only test binary is absent from this count), **zero** ASan/UBSan diagnostics
  anywhere in the log (not just zero ctest failures -- greped for
  `ERROR: AddressSanitizer|ERROR: LeakSanitizer|runtime error:|SUMMARY:.*Sanitizer`
  directly).
* [x] Run clang-format -- found 6 files with violations (`clang-format-18 --dry-run
  --Werror`, CI's exact recipe), formatted them in place, re-verified 0 violations across
  all 455 production files.
* [x] Run clang-tidy -- `run-clang-tidy -p build/debug '^(include|src)/.*\.(cpp|hpp)$'`
  (CI's exact recipe): **zero findings** in production code.
* [x] Verify Windows build -- a genuine native Windows build (MSVC 19.44, Visual Studio
  2022, Qt 6.9.3, `-DCFDAPP_BUILD_GUI=ON`), not a WSL build relabeled: clean configure,
  clean build (0 errors, only minor pre-existing `[[nodiscard]]`-discard warnings in test
  files), 1289/1289 ctest, CLI smoke (`--help`/`--version`/invalid-path all correct), and a
  real production case (`cases/lid_driven_cavity`) run to convergence through
  `cfdapp.exe`.
* [x] Verify Linux build -- WSL2/Ubuntu 22.04, described accurately as such (not claimed as
  a native-Linux-distribution matrix): clean configure/build, 1289/1289 serial and
  parallel ctest, CPU-only and with `-DCFDAPP_ENABLE_CUDA=ON` on a real GPU.
* [x] Perform manual GUI acceptance test -- **17/17 PASS**, human-executed by the project
  owner against the rebuilt `cfdapp_gui.exe` (self-reported result; Claude Code has no
  screen-capture/input-automation tool for a native Qt window and did not perform the
  interaction itself). One real bug was found and fixed during the run -- the Case page's
  Directory field rejected a path pasted via Windows Explorer's "Copy as path" (wrapped in
  literal double quotes); fixed with `sanitizeCaseDirectory()` in
  `apps/gui/SimulationController.cpp` plus a regression test
  (`SimulationControllerTest.OpenCaseAcceptsQuotedAndPaddedPath`), rebuilt, retested PASS.
  Full 17-row results in `results/release/p8-hardening/gui_acceptance.md`.
* [x] Update performance documentation -- `README.md` rewritten with accurate current GPU/
  CUDA status, OpenMP scaling, and large-grid stress findings from P7 (previously stale --
  predated the persistent GPU pipeline and reported "GPU measured slower than CPU," now
  false); `README.md` was also found and fixed as a UTF-16-encoded file (unreadable by most
  tools) and re-saved as UTF-8.

**Overall: COMPLETE, all 9 gates** -- see `results/release/p8-hardening/` for full evidence
(build logs, ctest logs, ASan/UBSan log, clang-format/clang-tidy logs, environment.txt, the
GUI checklist with 17/17 PASS results).

---

# P9 — Next Release

Target: **v0.2.0**

Release only when:

* [x] GPU production path is stable -- carried forward from P6-GPU-001/002/003 and
  P7-PERF-003, verified on WSL2/Linux with real CUDA hardware (`CFDAPP_ENABLE_CUDA=ON`);
  unchanged since, no relevant code has changed. Native-Windows CUDA remains untested (this
  machine's build has `CFDAPP_ENABLE_CUDA=OFF`) -- a pre-existing gap, not new. See
  `results/release/v0.2.0/summary.md` gate 1.
* [x] CPU/GPU equivalence passes -- carried forward, same WSL2/CUDA basis
  (`tests/solver/simple/test_simple_gpu_solver.cpp`). See summary.md gate 2.
* [x] Performance benchmarks are documented -- `results/performance/{cuda_end_to_end,
  openmp_scaling,large_grid_stress,preconditioner}/`, unchanged since P7 (no relevant code
  change). See summary.md gate 3.
* [ ] Full CI is green -- **BLOCKED**: commits `4e5a4d2` (P6/P7/P8 work) and `21e493e`
  (version bump to 0.2.0 + release-script fixes) exist locally on `main` but have not been
  pushed to `origin` (deliberate -- see summary.md), so `.github/workflows/ci.yml` has never
  run against them. Minimum to unblock: push and record the CI run result.
* [x] GUI acceptance passes -- 17/17 PASS, human-executed; see
  `results/release/p8-hardening/gui_acceptance.md`.
* [x] Packaged CLI/GUI smoke tests pass -- real `CFDApp-0.2.0-Windows-x64` package built and
  smoke-tested (CLI `--help`/`--version`/invalid-path/real-case-run, GUI launch), including
  from a PATH stripped of Qt/Visual Studio. See `results/release/v0.2.0/
  packaged_smoke_test.log`.
* [x] Release artifacts verified -- version exactly `0.2.0`, correct filenames, CLI+GUI+Qt
  runtime+docs+examples+LICENSE all present, package extracts and smoke-tests clean,
  SHA-256 checksums generated. See `results/release/v0.2.0/{summary.md,checksums.txt,
  environment.txt}`.

---

# Immediate Next Task

**P9 — v0.2.0 release qualification: 6 of 7 gates PASS, blocked on Full CI only**

P8 is complete (all 9 gates). Of P9's 7 gates, 6 have real, verified evidence (GPU
stability/equivalence/benchmarks carried forward from P6/P7 with no relevant code change
since; GUI acceptance, packaged smoke tests, and release-artifact verification all freshly
verified this session -- see `results/release/v0.2.0/summary.md`). The one remaining gate,
Full CI, is blocked purely on a git-push decision: commits `4e5a4d2` and `21e493e` are on
`main` locally but not pushed to `origin`, so CI has never run against them. Do not mark P9
complete or cut the release until CI has actually run green against the release commit.
