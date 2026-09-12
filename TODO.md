# CFDApp — TODO

**Current phase:** P9 — v0.2.0 Release
**Released:** v0.1.5
**Next release:** v0.2.0
**Immediate task:** Verify final CI pass, then tag v0.2.0 (see "Immediate Next Task" below)

## Rules

1. Work top-to-bottom within the current phase.
2. Do not start the next phase until the current phase's required gates pass.
3. `[ ]` = not completed or not verified. `[x]` = implemented AND verified with real evidence.
4. Never mark `[x]` for planned, assumed, partially implemented, or unverified work.
5. A successful build alone does not prove numerical correctness.
6. Automated tests do not substitute for explicitly required manual tests.
7. Local tests do not substitute for explicitly required CI results.
8. Never fabricate test, benchmark, CI, GUI, hardware, or release evidence.
9. Record failures honestly; do not weaken acceptance criteria to make a gate pass.
10. Fix the root cause, then rerun the affected verification before marking `[x]`.
11. Performance claims must come from measured end-to-end results, not assumptions or
    kernel-only timings unless explicitly labelled as microbenchmarks.
12. CPU/GPU equivalence requires explicit numerical tolerances and measured comparisons.
13. Hardware-specific claims must identify the environment actually tested.
14. A release gate applies to the exact release candidate commit; evidence from older
    commits may only be reused when clearly justified and unaffected by later changes.
15. Do not create a release/tag while a required release gate is blocked.
16. Update `TODO.md` immediately after verified work changes project status.
17. Keep detailed logs/results in `results/`; keep `TODO.md` concise and link to the
    evidence instead of copying large reports into it.
18. Preserve known limitations and failed/excluded tests rather than hiding them.
19. Do not start unrelated future-phase work while the current immediate task is blocked.

## Status

- `[ ]` — incomplete or unverified
- `[x]` — implemented and verified
- `← CURRENT` — active phase
- `⏳` — in progress
- `⚠️ BLOCKED` — cannot proceed until stated gate/dependency clears
- `✅` — phase complete

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

# P6 — GPU Performance ✅

* [x] **P6-GPU-001 — Persistent GPU pipeline.** Fields/matrices GPU-resident across
  iterations, minimal host/device transfers, CPU/GPU equivalence verified.
* [x] **P6-GPU-002 — Production GPU linear solvers.** GPU CG/BiCGSTAB in the
  production SIMPLE path, robust CPU fallback.
* [x] **P6-GPU-003 — Preconditioning.** GPU-resident Jacobi (diag(A)⁻¹, fully
  on-device). Stronger candidates (block/damped Jacobi, polynomial, approximate
  inverse, ILU(0)) investigated and rejected for scope.

**Acceptance:** production solve runs repeated iterations without unnecessary
host/device transfers; GPU CG/BiCGSTAB reproduce CPU solutions within tolerance;
preconditioning validated against CPU Jacobi and unpreconditioned solves within
tolerance, with measured (not assumed) iteration-count/wall-clock evidence.

**Evidence:** `include/cfd/gpu/`, `src/gpu/`, `src/algebra/LinearSolverFactory.cpp`,
`tests/solver/simple/test_simple_gpu_solver.cpp`, `results/performance/preconditioner/`.

---

# P7 — Performance Validation ✅

* [x] **P7-PERF-001 — CUDA end-to-end.** Real production-SIMPLE CPU-vs-GPU timing
  (never kernel-only), 20×20–320×320. **GPU break-even: 80×80. Best speedup: 2.01×
  at 320×320.** 640×640 excluded: CPU failed to converge within the shared
  iteration budget.
* [x] **P7-PERF-002 — OpenMP scaling.** 1–32+ threads, 160×160. **Best: 4 threads,
  1.33× speedup.** Regresses severely at 16/32 threads (WSL2 thread-spawn
  overhead). Bit-identical results across thread counts — zero race-condition
  evidence.
* [x] **P7-PERF-003 — Large-grid stress.** Memory/stability/NaN-Inf/mass-conservation
  checked at every grid. **Largest stable CPU grid: 480×480. Largest stable GPU
  grid: 640×640.** Limiting factor is solver iteration budget, not memory (under
  1GB host memory even at 800k+ cells); failures are clean
  `PressureCorrectionFailure`, never blow-up.

**Evidence:** `results/performance/{cuda_end_to_end,openmp_scaling,large_grid_stress}/`.

---

# P8 — Production Hardening ✅

* [x] Regression suite: 1289/1289
* [x] Parallel `ctest -j8`: 1289/1289, no race conditions
* [x] ASan/UBSan: PASS, zero diagnostics (grepped directly, not inferred)
* [x] `clang-format`: PASS (0/455 files)
* [x] `clang-tidy`: PASS (zero findings)
* [x] Native Windows build (MSVC 19.44, Qt 6.9.3): PASS
* [x] WSL2/Linux build, CPU-only and CUDA-enabled on real GPU: PASS
* [x] Manual GUI acceptance: **17/17 PASS**, human-executed. One real bug found and
  fixed (Explorer "Copy as path" quoted-path rejection) — see evidence.
* [x] Performance documentation updated (`README.md`)

**Acceptance:** all 9 hardening gates pass with real, verified evidence.

**Evidence:** `results/release/p8-hardening/` (build/ctest/ASan-UBSan/clang-format/
clang-tidy logs, `gui_acceptance.md`).

---

# P9 — v0.2.0 Release ← CURRENT

**Status:** 7/7 gates PASS
**Candidate:** `1e960c7` (verified green CI at this exact SHA — see note below)

* [x] GPU production path stable — carried forward from P6/P7 (WSL2/CUDA hardware
  evidence, unchanged since)
* [x] CPU/GPU equivalence passes — carried forward, same basis
* [x] Performance benchmarks documented — carried forward, same basis
* [x] Full CI green — run `34605885487` @ `8a8af79`, all 7 jobs PASS. Previous
  run (`34601816351` @ `267bbbd`) failed on a sanitizer-job scheduling/timeout
  bug (tuned for 32 cores, ran on a 4-vCPU runner), fixed in `8a8af79`.
* [x] GUI acceptance — 17/17 PASS
* [x] Packaged CLI/GUI smoke tests — PASS (`CFDApp-0.2.0-Windows-x64`, isolated PATH)
* [x] Release artifacts verified — version exactly `0.2.0`, correct filenames,
  contents, checksums

**Known limitation:** Native Windows + CUDA remains untested (this build is
`CFDAPP_ENABLE_CUDA=OFF`; only WSL2/Linux+CUDA has been verified).

**Note:** rule 14 satisfied for the exact commit tagged — `1e960c7` (adds
`RELEASE_NOTES_v0.2.0.md`) has verified green CI: run `34617756507`, all 7
jobs PASS (format, python, clang-tidy, build-test gcc/release, build-test
clang/debug, build-test gcc/debug, sanitizers). `v0.2.0` tag pushed to
origin, dereferences to `1e960c7` (verified via `git ls-remote --tags`).
Release workflow triggered on the tag push: run `34675450224` — in
progress, not yet verified complete.

**Evidence:** `results/release/v0.2.0/`.

---

# Immediate Next Task

## Verify final CI pass, then tag v0.2.0

- [x] Push this evidence-update commit. — `1e960c7`, `main` == `origin/main`.
- [x] Wait for CI on the new commit; verify all mandatory jobs pass. — run
  `34617756507`, 7/7 jobs PASS.
- [ ] Failure → diagnose → fix → commit → push → rerun. — N/A, no failure.
- [x] Success → tag `v0.2.0` at that exact commit, push the tag. — tag
  object `191d2cc0`, dereferences to `1e960c7`; pushed to origin.
- [ ] Create the GitHub Release and attach verified `results/release/v0.2.0/`
  artifacts. — Release workflow run `34675450224` triggered by the tag push;
  in progress, not yet verified.

**Guard:** Do not tag or publish until the commit being tagged itself has
green CI.
