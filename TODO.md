# CFDApp — TODO

**Current phase:** P10/P11 — Production Physics Integration & GUI Case
Authoring closeout (reconciliation phase — see "P10/P11 Reconciliation"
below: most of both phases were already implemented and shipped in
v0.2.0, just never marked done here)
**Released:** v0.2.0
**Next release:** TBD — no new release planned until P10-APP-004 and the
P11-GUI-005 manual-verification gap close
**Immediate task:** close the genuinely-open P10/P11 gaps (see "Immediate
Next Task" below): compatibility matrix, `case_format.md` documentation,
combined-physics example case, one manual GUI case-creation check

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

# P9 — v0.2.0 Release ✅

**Status:** 7/7 gates PASS — tagged, released, and independently verified
**Released commit:** `1e960c7eb8070a1292e07e66ea45b3a6b2c0a7db`
**Tag:** `v0.2.0` (annotated, object `191d2cc0`, dereferences to `1e960c7` —
confirmed both locally and via `git ls-remote --tags origin`)
**Release URL:** <https://github.com/matlabuser123/CFDAPP/releases/tag/v0.2.0>

* [x] GPU production path stable — carried forward from P6/P7 (WSL2/CUDA hardware
  evidence, unchanged since)
* [x] CPU/GPU equivalence passes — carried forward, same basis
* [x] Performance benchmarks documented — carried forward, same basis
* [x] Full CI green on the exact tagged commit — run `34617756507` @
  `1e960c7`, all 7 jobs PASS (format, python, clang-tidy, build-test
  gcc/release, build-test clang/debug, build-test gcc/debug, sanitizers).
  (Earlier candidate `8a8af79` also passed CI — run `34605885487` — but
  `1e960c7` superseded it and needed, and got, its own fresh green run per
  rule 14.)
* [x] GUI acceptance — 17/17 PASS
* [x] Packaged CLI/GUI smoke tests — PASS, re-verified twice: standalone
  isolated-PATH smoke test (`CFDApp-0.2.0-Windows-x64`) and independently
  again inside the release workflow's own "Smoke-test the packaged
  artifact" step on run `34675450224`.
* [x] Release artifacts verified — GitHub Release `v0.2.0` published
  (run `34675450224`, job `release`, all 14 steps PASS incl. "Publish
  GitHub Release"): `isDraft=false`, `isPrerelease=false`, tag `v0.2.0`.
  Assets present and `state=uploaded`: `CFDApp-0.2.0-Windows-x64.exe`
  (50,656,427 bytes), `CFDApp-0.2.0-Windows-x64.zip` (62,180,196 bytes),
  `SHA256SUMS.txt` (192 bytes). Checksums cross-verified: `SHA256SUMS.txt`
  hashes match GitHub's independently-computed asset digests exactly for
  both binaries. Release body diffed against `RELEASE_NOTES_v0.2.0.md` —
  identical (only difference: CRLF checkout line endings and one trailing
  blank line, both cosmetic).

**Known limitation:** Native Windows + CUDA remains untested (this build is
`CFDAPP_ENABLE_CUDA=OFF`; only WSL2/Linux+CUDA has been verified).

**Evidence:** `results/release/v0.2.0/` (local logs); GitHub Actions runs
`34617756507` (CI) and `34675450224` (release, incl. Windows build, full
regression suite, packaging, smoke test, checksums, publish); published
release at the URL above.

---

# P10/P11 Reconciliation — audit finding

Auditing `ROADMAP.md` after v0.2.0 to find "the next phase" turned up a
bookkeeping defect, not a coding gap: **P10 (Production Physics
Integration) and P11 (GUI Case Authoring) were already implemented and
shipped in v0.2.0**, under an earlier internal numbering
(`P6-PHYS-001/002/003`), in commits `19e2300` (species), `5603621`
(multiphase + compressible), `3d26930` (GUI editors) — all ancestors of
the released `1e960c7`. `ROADMAP.md`'s P10/P11 sections had every item
unchecked because a later refactor (`a5af328`) renumbered a stale
future-phase template onto this already-completed work without checking
it against the source tree (root cause traced via `git log`/`git show`,
not guessed).

**Consequence, disclosed rather than silently fixed:** the already-
published v0.2.0 GitHub Release's "Known limitations" section states
species/multiphase/compressible are "not yet reachable through
`physics.json`/`ProjectRunner`'s production dispatch" — false against the
source tree that was actually released. The published release stays
untouched (immutable evidence); this is recorded here as a known
documentation defect to avoid repeating in the next release's notes.

**Fresh re-verification performed before touching any checklist** (rule
10 — root-cause/fix before marking `[x]`; this was a doc-vs-reality gap,
so re-verification stood in for a fix):

- Fresh incremental build, `build/debug` (WSL2/gcc), zero errors.
- Full regression: **100% passed, 0 failed, 1290/1290** active tests (13
  pre-existing disabled, unchanged) — matches the documented baseline
  exactly, no regression.
- `CFDCaseIntegrationTests` (run from source root, correct working
  directory): **22/22 PASS** — `SpeciesProductionCaseTest`×7,
  `MultiphaseProductionCaseTest`×7, `CompressibleProductionCaseTest`×7,
  `CaseLidDrivenCavityIntegrationTest`×1.
- `CFDIoTests` filtered to `*Species*:*Multiphase*:*Compressible*`:
  **48/48 PASS**.
- `CFDGuiControllerTests`: **40/40 PASS**, including
  `CaseEditingTest.FullCaseCreationFromScratchValidatesSavesRunsAndMatchesCli`.

`ROADMAP.md`'s P10/P11 sections have been corrected to `[x]` against this
evidence, with the genuinely-open items kept `[ ]` (see below).

---

# Immediate Next Task

## Close the real remaining P10/P11 gaps

- [ ] **P10-APP-004 — Compatibility matrix.** Consolidate the ad hoc
  cross-checks in `PhysicsConfigParser.cpp::parsePhysicsConfig` into one
  named validation section. Add missing cross-checks: reject
  multiphase+compressible together (both claim "the" authoritative
  density field); decide and implement, or explicitly allow-and-document,
  species+multiphase, species+compressible, compressible+buoyancy,
  compressible+turbulence. Add rejection tests per new check (pattern:
  `tests/unit/io/test_multiphase_case.cpp`/`test_compressible_case.cpp`).
- [ ] **Combined-physics example case.** No case today exercises more
  than one advanced-physics module. Add one (thermal+species is the
  physically sensible first combination) with its own
  `test_<x>_production_case.cpp`-style end-to-end test.
- [ ] **Documentation gap.** `docs/user_guide/case_format.md` documents
  only `model`/`density`/`dynamic_viscosity`/`reynolds_number` in
  `physics.json` and `velocity`/`pressure` in `boundaries.json`; add
  thermal, turbulence, buoyancy, species, multiphase, compressible, and
  their per-patch boundary keys. Write the compatibility matrix down
  there too. Refresh `schemas/README.md`'s pointer if needed.
- [ ] **P11-GUI-005 manual verification.** One human-executed GUI step:
  create a brand-new case from scratch (not opening an existing one),
  save it, run it. Record PASS/FAIL as a dated addendum to
  `results/release/p8-hardening/gui_acceptance.md` (the existing 17-step
  checklist only opened an existing case). Confirm whether template
  selection exists; if not, note it as a disclosed gap rather than
  claiming it.
- [ ] **Compressible scope decision.** Record explicitly (this file +
  `ROADMAP.md`, already done in `ROADMAP.md`) that a real
  boundary-density model and a genuinely coupled compressible solver are
  deferred to `ROADMAP.md`'s `P12-COMP`, not committed to under P10 — the
  current honest post-hoc scope is what P10-APP-003 is considered
  complete against.

**Guard:** do not start P12/P13 until the items above close (rule 19).
