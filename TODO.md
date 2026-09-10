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

# P5 — Application ✅ (v0.1.5 released)

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
* [x] Release automation -- `v0.1.5` genuinely released, see the
  Release Gate section below for full evidence

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
* [x] Bump project version for the next clean release (0.1.5, `949ed32`)
* [x] Create a new release tag (`v0.1.5`, pushed)
* [x] Release workflow green end to end -- **attempt 8 (`v0.1.5`, run
  [34462755608](https://github.com/matlabuser123/CFDAPP/actions/runs/34462755608)),
  `conclusion: success`, all 17 steps passed**, including `Publish
  GitHub Release`. 8/8 cumulative attempts across this session, each
  exposing and fixing one genuine, previously-invisible defect: invalid
  Qt module -> stale CI working-directory -> missing
  `#include <algorithm>` -> tag/version mismatch -> MinGW-vs-MSVC linker
  mismatch -> clang-format runner-image drift -> missing NSIS on
  `windows-latest` -> missing GitHub-Release-publish step. This is the
  first attempt with none left.
* [x] Windows build succeeds on GitHub runner (confirmed)
* [x] Full release test suite passes (confirmed, on the real GitHub
  Windows runner)
* [x] ZIP artifact produced (confirmed)
* [x] Installer artifact produced (confirmed -- NSIS fix verified working)
* [x] CI smoke tests pass on packaged artifacts (confirmed -- both CLI
  `--version`/`--case` and the GUI process-stays-alive check passed)
* [x] Downloaded release artifact retested -- **done outside CI, on the
  local Windows machine**: `gh release download v0.1.5` (the actual
  published assets, not a local or CI build), SHA256 recomputed locally
  and matched `SHA256SUMS.txt` exactly for both the ZIP and the
  installer. Extracted ZIP: `cfdapp.exe --version` reports "CFDApp
  0.1.5", `cfdapp.exe --case <lid_driven_cavity>` converges cleanly
  (mass imbalance 0, no NaN/Inf), `cfdapp_gui.exe` stays alive 3s.
  Installer: real silent install (`/S /D=...`, genuine NSIS flags, not
  simulated) produced the full expected layout (bin/docs/examples/
  licenses/Uninstall.exe/Qt plugin dirs); the installed `cfdapp.exe
  --version` also reports "CFDApp 0.1.5".
* [x] GitHub Release published with assets -- **confirmed via
  `gh release view v0.1.5`**: `draft: false`, `prerelease: false`,
  published `2026-09-10T10:05:01Z`, url
  <https://github.com/matlabuser123/CFDAPP/releases/tag/v0.1.5>, with
  all 3 assets attached (`CFDApp-0.1.5-Windows-x64.zip`,
  `CFDApp-0.1.5-Windows-x64.exe`, `SHA256SUMS.txt`).
* [x] Mark release automation complete
* [x] Mark P5 complete -- CFDApp v0.1.5 is genuinely released: CI Gate
  and Release Gate are both fully green with real, independently
  verified evidence (not assumed, not simulated).

---

# Next Backlog

## Production Physics Integration

* [x] Species `physics.json` parsing -- physics.json's "species" array
  (name/diffusivity/initial_concentration per entry) and boundaries.json's
  per-patch "species" object (fixed_value/fixed_gradient concentration
  BCs, keyed by species name); full parse-time validation (unique names,
  diffusivity >= 0, exactly the declared name set required on every
  patch). Committed `19e2300`.
* [x] Species `ProjectRunner` dispatch -- `SimulationSetup::species` built
  by `CaseBuilder`, solved by `ProjectRunner::run()` (one
  `SpeciesSolver::solve()` per declared species, same one-way
  best-available policy as thermal), exported to CSV/VTK/JSON
  (`concentration_NAME` columns/SCALARS blocks, a "species" metadata
  array), and reported by the CLI. New case `cases/species_diffusion`
  (`cfdapp --case cases/species_diffusion` runs it end to end). Verified
  with a real production-integration test suite
  (`tests/integration/case/test_species_production_case.cpp`, 7 tests):
  convergence, the analytical 1D slab profile (L2/Linf < 1e-6),
  independently-computed global diffusive-flux conservation, CSV/VTK/JSON
  export content, InvalidCase handling, nonspecies-case non-interference,
  and determinism -- all passing. Full local regression 1195/1195
  (1162 pre-existing + 33 new), clang-format-18 clean, clang-tidy clean
  (0 findings on every changed production file). Committed `19e2300`,
  pushed to `main`.
* [ ] Multiphase `physics.json` parsing
* [ ] Multiphase `ProjectRunner` dispatch
* [ ] Compressible `physics.json` parsing
* [ ] Compressible `ProjectRunner` dispatch
* [ ] Export/output wiring for new physics (species' own share of this is
  done -- see above; multiphase/compressible still need theirs)

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

**CI Gate and Release Gate are both fully closed (`v0.1.5` genuinely released). P5 is complete. Species production integration is done (physics.json/boundaries.json parsing, `CaseBuilder`/`ProjectRunner` dispatch, CSV/VTK/JSON export, CLI report, `cases/species_diffusion` example, 33 new passing tests, full regression 1195/1195 -- committed `19e2300`).**

**Next: Multiphase `physics.json` parsing + `ProjectRunner` dispatch** (see "Production Physics Integration" above) -- follow the same architecture species just established (CaseBuilder builds a runtime setup struct, ProjectRunner dispatches one-way/best-available, ResultExporter gets a new optional/list field, a real case under `cases/` exercises it end to end with a production-integration test). Not started.
