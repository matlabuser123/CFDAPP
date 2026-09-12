# CFDApp — TODO

**Current phase:** P12-COMP-001 ✅ done (compressible boundary-density
model) — see its own section below. P10/P11 closeout is otherwise
complete except P11-GUI-005 (human GUI verification, pending).
**Released:** v0.2.0
**Next release:** TBD — no new release planned
**Immediate task:** P11-GUI-005 manual verification (see "Immediate Next
Task" below) is the one remaining P10/P11 item; `P12-COMP-002` (the
coupled compressible solver) is audited/planned but explicitly not
started

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

- [x] **P10-APP-004 — Compatibility matrix.** Consolidated the ad hoc
  cross-checks into one authoritative function,
  `validatePhysicsCompatibility` in `src/io/case/PhysicsConfigParser.cpp`.
  Added the one missing rejection (multiphase excludes compressible).
  Confirmed GUI/ProjectRunner already reach this same function with no
  duplicate logic to remove (GUI validation round-trips through
  `cfd::io::CaseBuilder{}.build(...)`, same as CLI). New test file
  `tests/unit/io/test_physics_compatibility.cpp`, 10/10 PASS (5 supported
  combinations incl. the maximal "everything at once" case, 4 unsupported
  combinations with message-content checks, 1 conflicting-config case).
  Fresh full verification: `CFDIoTests` 194/194, `CFDCaseIntegrationTests`
  22/22, `CFDGuiControllerTests` 40/40, full regression **1300/1300** (up
  from 1290/1290, zero regressions). Evidence:
  `results/p10-app-004/summary.md`.
- [x] **Combined-physics example case.** Added `cases/heated_species_diffusion`
  (thermal + species together, same quiescent 20x4 slab geometry as
  `cases/species_diffusion`, thermal values from `cases/heated_cavity`) and
  `tests/integration/case/test_heated_species_diffusion_production_case.cpp`
  (5 tests: converges, both fields match their independent analytical
  profiles simultaneously, both conserve flux independently, both export
  to CSV/VTK/JSON, deterministic). Real CLI-generated results committed
  under `cases/heated_species_diffusion/results/` (species converges in
  593 iterations, identical to standalone `cases/species_diffusion` --
  confirms the two modules run independently, neither perturbing the
  other). Full regression **1305/1305** (up from 1300/1300), parallel
  `ctest -j8` clean (no fixture race).
- [x] **Documentation gap.** `docs/user_guide/case_format.md` now
  documents thermal/turbulence/buoyancy/species/multiphase/compressible
  (each with an example and field-by-field constraints), their per-patch
  `boundaries.json` requirements (temperature/species/alpha), and a
  "Physics compatibility" section with the same matrix table
  `validatePhysicsCompatibility` enforces. `schemas/README.md`'s pointer
  was already correct, no change needed. Also documents units (none
  enforced anywhere in this codebase -- SI by convention only) and an
  invalid-configuration example for every cross-field/compatibility rule,
  each error message captured verbatim from a real `cfdapp` run, not
  hand-derived. Commits `d704e89`, `525b5a0`. Verified:
  `CFDIoTests` 194/194, `CFDCaseIntegrationTests` 27/27, unchanged.
- [ ] **P11-GUI-005 manual verification.** One human-executed GUI step:
  create a brand-new case from scratch (not opening an existing one),
  save it, run it. Record PASS/FAIL as a dated addendum to
  `results/release/p8-hardening/gui_acceptance.md` (the existing 17-step
  checklist only opened an existing case). Confirm whether template
  selection exists; if not, note it as a disclosed gap rather than
  claiming it.
- [x] **Compressible scope decision.** Recorded in `ROADMAP.md`: a
  genuinely coupled compressible pressure-velocity solver (P12-COMP-002)
  is deferred, not committed to under P10 — the current honest
  post-hoc-reinterpretation scope is what P10-APP-003 is considered
  complete against. The real boundary-density model half of this
  decision has since been implemented under `P12-COMP-001` (below), by
  explicit instruction — this bullet's "deferred" scope now refers only
  to the coupled solver.

---

# P12-COMP-001 — Compressible boundary-density model

Explicitly authorized and scoped separately from `P12-COMP-002` (the
coupled solver, not started) — see `ROADMAP.md`'s `P12-COMP` section for
the full audit this was staged from.

Replaced `CompressibleMassFlux`'s owner-cell-reuse boundary-density
simplification with a real EOS-evaluated boundary density: each boundary
face's own boundary-interpolated absolute pressure/temperature (via the
already-existing generic `cfd::discretization::interpolateFace` and the
case's own already-existing pressure/temperature boundary conditions —
no new boundary-condition types or `physics.json`/`boundaries.json` keys
were needed). `calculateCompressibleMassFlux`'s signature was extended
accordingly; `ProjectRunner.cpp`'s one call site and every existing test
that called it were updated to match.

**Evidence:** `results/p12-comp-001/summary.md`.

- [x] Audited the existing boundary-condition architecture before
  changing anything (`ScalarBoundaryCondition::boundaryValue()`,
  `cfd::discretization::interpolateFace`/`interpolateBoundaryFace` —
  confirmed these already generically handle every existing
  velocity/pressure/temperature BC type, so no new infrastructure was
  needed).
- [x] Explicit behavior confirmed per boundary type: Outlet (typically
  Dirichlet/`fixed_value` gauge pressure) gets the exact
  reference-pressure-consistent density, not the interior's; Inlet/Wall
  (typically zero-gradient pressure) reduce to the old owner-cell result
  exactly when the gradient is genuinely zero; Wall's own zero-velocity
  BC makes the boundary-density choice irrelevant to conservation there
  either way (confirmed by a dedicated test, not assumed).
- [x] 9 new/rewritten unit tests in `tests/unit/compressible/test_compressible_mass_flux.cpp`
  (the old `BoundaryFaceUsesOwnerCellsOwnDensity` test, which asserted
  exactly the behavior this supersedes, was replaced): internal-face
  interpolation (unchanged), outlet/inlet/wall boundary behavior, direct
  EOS-consistency check, non-positive-boundary-temperature rejection,
  and a uniform-state/low-Mach limiting-behavior check.
- [x] New integration test proving the treatment is exercised by the
  *production* dispatch path, not just the equation-level function:
  `CompressibleProductionCaseTest.OutletBoundaryMassFluxUsesReferencePressureDensity`
  in `tests/integration/case/test_compressible_production_case.cpp`,
  using the real `cases/compressible_validation` case — confirms the
  outlet's mass flux now uses the reference-pressure-consistent EOS
  density, and explicitly confirms this differs from the (superseded)
  owner-cell value in this real case.
- [x] `include/cfd/app/ProjectRunner.hpp`'s `CompressibleRunResult` gained
  a `massFlux` field (the value was already computed internally; it just
  wasn't being kept) so this new treatment is independently inspectable
  by tests/future GUI use, not only implicit in the continuity diagnostic.
- [x] Stale "owner-cell reuse" comments updated in
  `CompressibleMassFlux.hpp` (kept as a historical note, not deleted) and
  `ROADMAP.md`'s P10-APP-003/P12-COMP sections.
- [x] Fresh verification, this session: incremental rebuild clean;
  `CFDCompressibleTests` **50/50** PASS; `CFDLowMachRegressionTests`
  **7/7** PASS (unchanged — confirms the existing low-Mach regression
  still holds under the new boundary treatment); `CFDCaseIntegrationTests`
  filtered to `CompressibleProductionCaseTest.*` **8/8** PASS;
  `CFDIoTests` **194/194** PASS (unchanged, confirming no case-format
  impact); full regression suite **1313/1313** PASS (up from 1305/1305 —
  net +8 tests: +9 new/rewritten in the mass-flux suite, −1 removed
  stale test, +1 new production-case test — zero regressions), parallel
  `ctest -j8` clean.
- [x] No case-format expansion was needed (the one condition under which
  this task's instructions required stopping before implementing) — every
  boundary-density input already existed via the case's own existing
  pressure/temperature boundary conditions.

**P12-COMP-001 acceptance:** met — real EOS-based boundary density
implemented, explicit per-type behavior confirmed, comprehensive new test
coverage, production-path integration proven, zero regressions.

**Guard:** `P12-COMP-002` (the coupled compressible solver) and P13 remain
not started, per explicit instruction to stage P12-COMP-001 and
P12-COMP-002 separately (rule 19).
