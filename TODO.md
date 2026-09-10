# CFDApp — TODO

**Current phase:** P5 — Application / Release
**Priority:** correctness → validation → CI → release → future features

---

# P0 — Numerical Foundation ✅

* [x] Structured 2D mesh
* [x] Fields and boundary conditions
* [x] Sparse linear algebra
* [x] FVM discretization
* [x] Incompressible momentum/continuity
* [x] SIMPLE + pressure correction
* [x] Mass conservation
* [x] Poiseuille validation
* [x] Lid-driven cavity validation
* [x] Grid refinement
* [x] Determinism

---

# P1 — Application Foundation ✅

* [x] Case system
* [x] CLI execution
* [x] CSV / JSON / VTK output
* [x] Python validation tooling
* [x] Regression suite
* [x] CI infrastructure
* [x] Sanitizers infrastructure
* [x] clang-format / clang-tidy infrastructure
* [x] Quality gate framework

---

# P2 — Transient / Thermal / Turbulence ✅

* [x] Time controller
* [x] Implicit Euler
* [x] CFL monitoring
* [x] TransientSolver
* [x] PISO
* [x] Restart capability
* [x] Transient validation
* [x] Energy equation + thermal BCs
* [x] Conjugate heat-transfer foundation
* [x] RANS framework
* [x] k-ε
* [x] k-ω
* [x] SST
* [x] Turbulence validation

---

# P3 — Advanced Physics ✅

* [x] Boussinesq buoyancy
* [x] Natural-convection validation
* [x] Variable properties
* [x] Species transport foundation
* [x] Multiphase foundation
* [x] Compressible foundation
* [x] Low-Mach / conservation validation

---

# P4 — Performance ✅

* [x] Profiling baseline
* [x] Matrix-assembly optimization
* [x] Linear-solver investigation
* [x] OpenMP
* [x] Memory/layout investigation
* [x] CUDA foundation/integration
* [x] CPU/GPU equivalence tests
* [x] Large-grid benchmarks

---

# P5 — Application 🚧

* [x] Production case manager
* [x] GUI solver workflow
* [x] Field visualization
* [x] Contours
* [x] Vector plots
* [x] Residual monitoring
* [x] Post-processing
* [x] ParaView workflow
* [x] User documentation
* [x] Windows packaging
* [x] Fixed a real Windows-only runtime defect: `CFDGuiControllerTests.exe`
  (links `Qt6::Test`) had no `windeployqt` step of its own, so it had no
  local `Qt6Test.dll`; Windows' loader then fell through to PATH and
  could load an unrelated/older Qt6Test.dll (reproduced with both a
  Miniconda-bundled Qt 6.7.3 and a leftover Qt 6.5.3 SDK on this
  machine), crashing with `STATUS_ENTRYPOINT_NOT_FOUND` ("Entry Point
  Not Found" on `QSignalSpy::wait(chrono::duration<...>)`, an overload
  only exported by Qt >= ~6.8) instead of running. Fixed by refactoring
  `cmake/QtDeploy.cmake` into a reusable `cfdapp_deploy_qt_runtime()`
  function and calling it for `CFDGuiControllerTests` too, not just
  `cfdapp_gui`. Verified: full reconfigure+rebuild of
  `build/windows-release` via `scripts/windows-release/
  1-configure-and-build.ps1`; `Qt6Test.dll` (6.9.3.0, matching the
  compile kit) now present next to the test exe; the exe and the
  `SimulationControllerTest.*` ctest suite (13/13) both pass even with
  the stale Qt 6.5.3 SDK and conda's Qt 6.7.3 deliberately placed ahead
  on PATH (the exact failure condition). No solver code changed, no
  test weakened.
* [ ] Release automation

## CI Gate

* [x] clang-format configuration fixed and pinned
* [x] clang-tidy clean
* [x] Python suite clean
* [x] gcc/debug build-test green
* [x] gcc/release build-test green
* [x] clang/debug build-test green
* [x] sanitizers green -- **real GitHub Actions evidence, not assumed.**
  Run [34444117973](https://github.com/matlabuser123/CFDAPP/actions/runs/34444117973)
  on commit `1b6d350`: all 7 jobs `completed`/`success` (format 25s,
  python 23s, clang-tidy 2m37s, build-test gcc/release 7m52s, build-test
  clang/debug 28m49s, build-test gcc/debug 39m7s, **sanitizers
  1h48m02s**). The sanitizers job's long duration was investigated
  directly (not assumed safe): reproduced the identical `ctest` run
  locally (WSL, same `asan` preset) and confirmed via `ps` that
  individual slow tests (e.g. `TwoMaterialConductionValidation.
  Grid80MatchesAnalyticalSolution`, `NaturalConvectionValidation.
  Grid15x15MatchesDeVahlDavisRa1e3`) were genuinely at ~99% CPU the
  whole time, not hung -- these are outer-Picard-loop validation tests
  with maxIterations in the thousands even without ASan (see
  `tests/integration/thermal/test_two_material_conduction.cpp`'s own
  comments), and ASan's malloc/free instrumentation disproportionately
  slows allocation-heavy iterative-solver code. Root cause: `ctest` runs
  fully serially in CI (`CMakePresets.json`'s testPresets have no
  `"jobs"` field, and `ci.yml` doesn't pass `-j`) -- a real CI
  wall-clock inefficiency worth fixing later (parallelize + add a
  per-test TIMEOUT safety net), but not a correctness defect, so nothing
  was changed to force this job green.
* [x] Full CI workflow green on `main` -- run 34444117973 (commit
  `1b6d350`), `conclusion: success`. Follow-up commit `b3ec9b9` (Qt
  deploy fix for `CFDGuiControllerTests`, Windows-only) has its own run
  34448903502, also `conclusion: success`, all 7 jobs green (sanitizers
  1h47m). Independently corroborated by a full local ASan/UBSan rerun
  (WSL, same `asan` preset): 100% tests passed, 0 failed out of 1149.
  `main` tip (`8bdba5d`, TODO.md-only) has no functional changes since
  the last green commit.

**Known issue (unchanged, not yet fixed):** one transient parallel-fixture race has been observed in `LoadSnapshotFromResultsTest.ReloadsAPreviouslyWrittenResultsDirectory`; it passes in isolation but should not be treated as a fully clean local regression result until the race is resolved or eliminated.

## Release Gate

* [x] Keep existing tags immutable (`v0.1.0`-`v0.1.4` all untouched)
* [x] Version/release fixes pushed to `main`
* [x] CI Gate fully green (see above -- verified via run 34444117973)
* [x] Bump project version for the next clean release (currently 0.1.4,
  `a3117f5`; another bump pending, see below)
* [x] Create a new release tag (`v0.1.4`, pushed)
* [x] Release workflow green end to end -- **attempt 7 (`v0.1.4`, run
  [34461039922](https://github.com/matlabuser123/CFDAPP/actions/runs/34461039922)),
  `conclusion: success`, all 16 steps passed**: tag-version check,
  MSVC/Qt setup, Build, full regression suite, NSIS install, CPack
  packaging (ZIP and NSIS installer both), the packaged-artifact smoke
  test (CLI *and* GUI), checksums, and artifact upload. First fully
  green Release run out of 7 attempts.
* [x] Windows build succeeds on GitHub runner (confirmed)
* [x] Full release test suite passes (confirmed, on the real GitHub
  Windows runner)
* [x] ZIP artifact produced (confirmed)
* [x] Installer artifact produced (confirmed -- NSIS fix verified working)
* [x] CI smoke tests pass on packaged artifacts (confirmed -- both CLI
  `--version`/`--case` and the GUI process-stays-alive check passed)
* [ ] Downloaded release artifact retested -- not yet done outside CI
* [ ] GitHub Release published with assets -- **a run with
  `conclusion: success` still did NOT publish one**: `gh release list`
  stayed empty after attempt 7. Root cause: `actions/upload-artifact`
  only attaches files to the *workflow run*, not to a Release object --
  nothing in `release.yml` ever created one. Fixed in `645aecb`
  (`softprops/action-gh-release@v2` + job-level `contents: write`
  permission), pushed to `main`. Awaiting user go-ahead on a new tag
  (`v0.1.5`) to verify this actually publishes a Release -- `v0.1.4`
  stays as-is per the "never move a pushed tag" rule.
* [ ] Mark release automation complete
* [ ] Mark P5 complete

---

# Next Backlog

## Production Physics Integration

* [ ] Species `physics.json` parsing
* [ ] Species `ProjectRunner` dispatch
* [ ] Multiphase `physics.json` parsing
* [ ] Multiphase `ProjectRunner` dispatch
* [ ] Compressible `physics.json` parsing
* [ ] Compressible `ProjectRunner` dispatch
* [ ] Export/output wiring for new physics

## GUI Improvements

* [ ] Mesh editor
* [ ] Physics editor
* [ ] Boundary-condition editor
* [ ] Solver-settings editor
* [ ] Full case creation from GUI

## Performance Follow-up

* [ ] Persistent GPU-resident field/matrix pipeline
* [ ] Production GPU CG/BiCGSTAB integration
* [ ] Improve preconditioning
* [ ] Measure true CUDA end-to-end benefit
* [ ] Revisit OpenMP scaling
* [ ] Larger CPU/GPU benchmarks

---

# Immediate Next Task

**CI Gate is fully green (verified). Release Gate: `v0.1.4` (attempt 7) is the first fully green Release run end to end (build, tests, packaging, smoke test, checksums) -- but a `conclusion: success` run still did not publish an actual GitHub Release (`actions/upload-artifact` only attaches to the workflow run, not a Release object). Fixed in `645aecb` (`softprops/action-gh-release@v2`). Awaiting user go-ahead on a new tag (e.g. `v0.1.5`) to verify a Release actually gets published -- `v0.1.4` stays untouched per the "never move a pushed tag" rule.**
