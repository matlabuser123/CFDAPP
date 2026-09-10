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
* [x] Multiphase `physics.json` parsing -- physics.json's "multiphase"
  object (phase1/phase2 name+density+viscosity, initial_alpha,
  transport_time_step) and boundaries.json's per-patch "alpha" object
  (fixed_value/fixed_gradient), enable-by-presence like species/thermal.
  Full parse-time validation: initial_alpha in [0,1], both phase
  viscosities > 0, distinct phase names, transport_time_step > 0,
  top-level `dynamic_viscosity` <= min(phase viscosities) (guarantees
  `MixtureViscosityModel`'s implied turbulent viscosity is never
  negative), multiphase rejected together with turbulence (both want
  SIMPLE's one turbulence-model slot).
* [x] Multiphase `ProjectRunner` dispatch -- `SimulationSetup::multiphase`
  built by `CaseBuilder`, run once SIMPLE's result is fully finite: one
  `VolumeFractionSolver::step()` (the equation module's own single
  implicit-Euler transient step -- no outer-Picard loop exists by
  design), then `TwoPhaseSystem::evaluateMixtureDensityField`/
  `evaluateMixtureViscosityField` at the final alpha. Mixture viscosity
  is fed back into SIMPLE's momentum assembly via a new
  `MixtureViscosityModel` adapter over the existing
  `cfd::turbulence::TurbulenceModel` interface; mixture density is
  intentionally NOT wired into continuity (`MultiphaseProperties.hpp`'s
  own documented scope boundary -- SIMPLE's density stays the case's
  single top-level constant). Exported to CSV/VTK/JSON
  (`volume_fraction`/`mixture_density`/`mixture_viscosity`, a
  "multiphase" metadata block with phase properties + phase1 volume
  conservation metric), reported by the CLI, and discoverable via
  `VisualizationSnapshot` (live and reloaded). New case
  `cases/multiphase_validation` (quiescent two-phase slab; exact
  hand-verified values: volume_fraction=0.5, mixture_density=500.5,
  mixture_viscosity=5.09e-4, phase1_volume=0.5). Verified with
  `tests/unit/io/test_multiphase_case.cpp` (11 tests) and
  `tests/integration/case/test_multiphase_production_case.cpp` (7
  tests) -- all passing.
* [x] Compressible `physics.json` parsing -- physics.json's
  "compressible" object (gas_constant, specific_heat_pressure,
  reference_pressure, exactly one of temperature/thermal_coupled).
  Validation: all positive, specific_heat_pressure > gas_constant,
  thermal_coupled requires the case's own "thermal" block to be enabled
  (reuses its converged temperature field instead of a constant).
* [x] Compressible `ProjectRunner` dispatch -- this foundation has no
  compressible pressure-velocity solver at all
  (`CompressibleContinuity.hpp`'s own header comment: "NOT a
  separately-iterated compressible pressure-correction solve"), so
  dispatch is an honest post-hoc reinterpretation pass, not a genuine
  solve, matching `test_low_mach_regression.cpp`'s own documented scope
  ("a post-hoc consistency demonstration"). Once SIMPLE's result is
  fully finite: absolute pressure from the case's reference pressure,
  EOS density via `IdealGasEOS::evaluateDensityField`, per-cell Mach
  number, `calculateCompressibleMassFlux`, and a diagnostic
  `cfd::physics::evaluateContinuity` imbalance -- no density/momentum/
  energy feedback into SIMPLE (unsupported coupling is not attempted).
  Exported to CSV/VTK/JSON (`density`/`pressure_absolute`/
  `compressible_temperature`/`mach_number`, a "compressible" metadata
  block with a `status` field distinguishing "Evaluated" from
  "NotRun"), reported by the CLI (`Compressible evaluated: yes` +
  `Compressible max Mach number: ...`), and discoverable via
  `VisualizationSnapshot`. New case `cases/compressible_validation`
  (isothermal channel flow, low Mach; hand-verified: density matches
  the ideal-gas law exactly, mach_max=0.0041, continuity
  imbalance=3.68e-5). Verified with
  `tests/unit/io/test_compressible_case.cpp` (11 tests) and
  `tests/integration/case/test_compressible_production_case.cpp` (7
  tests) -- all passing.
* [x] Export/output wiring for new physics -- CSV/VTK/JSON now generic
  across species/multiphase/compressible via `NamedScalarField` (a real
  bug was found and fixed here: CSVWriter/VTKWriter were hardcoding a
  `"concentration_"` prefix onto every extra field, which mis-prefixed
  multiphase/compressible fields; the prefix is now supplied by the
  caller per field). `VisualizationSnapshot` gained a generic
  `extraScalarFields` list so species/multiphase/compressible fields are
  discoverable via the existing `availableScalarFields()`/
  `scalarField()` GUI API with zero `apps/gui/` code changes; both
  `buildSnapshot()` (live) and `loadSnapshotFromResults()` (reloaded, no
  solver rerun) populate it, verified equal by
  `tests/unit/app/test_visualization_snapshot.cpp`'s three new
  `*ExposesConcentrationFieldLiveAndReloaded`/`*ExposesMixtureFields...`/
  `*ExposesThermodynamicFields...` tests. Existing incompressible cases
  keep their CSV/VTK format unchanged; JSON metadata now always includes
  `"multiphase"`/`"compressible"` `{"enabled": false}` blocks (a
  backward-compatible, intentional schema extension -- confirmed via
  `cases/heated_cavity`/`cases/species_diffusion`'s own committed
  `results/metadata.json` diffs).

## Parallel-Test Fixture Race -- CLOSED

* [x] Fixed the known shared-fixture race under `ctest -j8` -- root cause
  was broader than the single test originally observed
  (`LoadSnapshotFromResultsTest.ReloadsAPreviouslyWrittenResultsDirectory`
  against `tests/data/cases/valid_cavity`): three more repo-tracked
  production example cases (`cases/multiphase_validation`,
  `cases/compressible_validation`, `cases/species_diffusion`) are each
  run directly (writing into their own shared `results/` subtree) by
  5-6 `TEST`s apiece across `tests/integration/case/
  test_{multiphase,compressible,species}_production_case.cpp` *and*
  `tests/unit/app/test_visualization_snapshot.cpp` -- a second instance
  of the identical race, on different directories, spanning both
  binaries. Fix (`tests/support/CaseFixtureCopy.{hpp,cpp}`, an existing
  P7-TEST-001 helper already applied to `valid_cavity`): every one of
  those `ProjectRunner::run()`/`loadSnapshotFromResults()` call sites now
  gets its own private `CaseFixtureCopy` of the canonical case directory
  (a real recursive copy under the system temp directory, moved not
  duplicated, removed on scope exit) instead of the shared repo path --
  no serialization, no `RESOURCE_LOCK`, genuine per-test isolation.
  `tests/CMakeLists.txt`'s raw `CFDAppCliValidCase` ctest add_test
  (not a gtest case, so it cannot construct a C++ RAII fixture) points at
  a dedicated static `tests/data/cases/valid_cavity_cli_smoke` copy
  instead. A real, pre-existing MSVC-toolchain incompatibility was also
  hit and fixed while getting a clean local Windows build to verify any
  of this: `cfd::boundary::BoundaryConditionSet` (a `std::map<std::string,
  std::unique_ptr<BoundaryCondition>>` wrapper, correctly move-only)
  relied on its copy/move members being implicitly declared; at least one
  MSVC STL build (VC++ 14.51.36231 toolset) hard-errors instantiating
  `std::vector<T>::push_back()`'s reallocation path for a `T` whose move-
  only-ness comes from an *implicitly* (rather than explicitly)
  `noexcept`-declared move constructor (`std::vector<SpeciesSetup>` in
  `CaseBuilder.cpp`, `SpeciesSetup` holding a `BoundaryConditionSet` by
  value) -- reproduced in isolation with a 20-line minimal repro having
  nothing to do with this codebase, fixed by explicitly declaring
  `BoundaryConditionSet`'s copy ctor/assignment `= delete` and move ctor/
  assignment `noexcept = default` (`include/cfd/boundary/
  BoundaryCondition.hpp`) -- a behavior-preserving change (it was already
  implicitly move-only) that only makes the existing contract explicit.
  **Verified**: full local Windows Release build (`build/windows-release`,
  MSVC 14.51.36231 + Qt 6.9.3, `CFDAPP_BUILD_GUI=ON`) from clean, zero
  compiler errors; full serial `ctest` 1257/1257 passed; **19 consecutive
  `ctest -j8` runs** (6 immediately after the `valid_cavity`-only state
  confirmed the original flake was gone -- one of those 6 then surfaced
  the second, broader race via `MultiphaseProductionCaseTest.
  ExportsMixtureFieldsToCsvVtkAndJson`; 13 more after the full fix, 0
  failures) -- both the originally-reported flake and the newly-found one
  never reproduced again. Python suite 105 passed/1 skipped, unaffected.
  No solver behavior, tolerance, or validation criterion changed anywhere
  in this fix. **Cross-platform confirmation before pushing**: also
  configured, built, and tested the CPU-only path fresh under WSL Ubuntu
  22.04 (gcc 11.4.0, the same family CI's `build-test gcc/debug` job
  uses) -- zero compiler errors, `ctest -j$(nproc)` **1221/1221 passed**
  (fewer than the Windows/GUI total above only because
  `CFDAPP_BUILD_GUI` defaults OFF and no Qt6 is installed there, exactly
  matching every Linux CI job today). `clang-format-18` (the exact
  CI-pinned binary, via that same WSL install) `--dry-run --Werror`
  across `include src apps tests` found genuine formatting violations
  (not just version noise -- confirmed by re-checking with it directly)
  in 6 of the new/changed files; fixed in place with `clang-format-18
  -i`, then re-verified both the dry-run (clean) and a full Windows
  rebuild+retest (1260/1260, unaffected by the reformat).
  `run-clang-tidy -p build/wsl-check` (clang-tidy-14, the closest
  available match to CI's unpinned `clang-tools` package) on the two
  touched `src/io/case/*.cpp` files found zero findings on any changed
  line (the handful of pre-existing warnings elsewhere in those files
  predate this work and were left alone).

## GUI Improvements

One authoritative C++ editable-case model (`apps/gui/CaseModelAdapter.{hpp,cpp}`
-- pure `QVariantMap<->cfd::io::*` conversion, never validation) plus
`SimulationController`'s own editing surface (`SimulationControllerEditing.cpp`
-- get/set per section, `validateDraft()`, BC/turbulence vocabulary) were
found already implemented, uncommitted, from unfinished prior work (a
missing `apps/gui/tests/test_case_editing.cpp` left the whole tree unable
to even configure) -- reviewed in full, finished, and built on rather
than replaced, per its own "continue using/extending it rather than
introducing another competing model" guidance. Architecture is exactly
`QML -> SimulationController -> CaseSession -> ProjectRunner`; no case
schema, JSON parsing, or solver logic was added to QML anywhere.

* [x] Mesh editor -- `apps/gui/qml/MeshEditor.qml`: nx/ny/length/height
  bound to `meshConfig()`/`geometryConfig()`, committed via
  `setMeshAndGeometry()`; live derived cellCount/dx/dy plus a large-mesh
  warning from `meshCellInfo()`; a proportional Canvas grid-line preview
  (drawing, not meshing -- `MeshGeometry::createCartesian2D` still does
  the real meshing at run time); validation errors surfaced via the
  shared `ValidationPanel`. Verified by
  `CaseEditingTest.MeshEditRoundTripsThroughSaveReopenAndRunsThroughProjectRunner`:
  edits nx/ny/length/height on a real case, `validateDraft()` succeeds,
  `saveAs()`, a **second independent controller** reopens and confirms
  identical values, then runs to `Converged` via `ProjectRunner` -- the
  literal acceptance gate ("GUI edit -> save -> reopen -> values
  identical -> CLI solve").
* [x] Physics editor -- `apps/gui/qml/PhysicsEditor.qml`: core
  density/viscosity/Reynolds, plus toggle-enabled thermal/buoyancy/
  multiphase/compressible sections and an add/remove species list, all
  through `setPhysicsConfig()`. Only genuinely production-integrated
  modules are shown; the top-level "model" field is a read-only fact
  ("incompressible_laminar", the only value `PhysicsConfigParser.cpp`
  accepts), not a fabricated choice. Turbulence model list, and which
  velocity/temperature types carry a "value" field, come from
  `cfd::io::kTurbulenceModels`/`BoundaryVocabulary.hpp` (hoisted out of
  the parser's own anonymous namespace, P7-GUI-002/003) -- never a
  second hand-typed vocabulary. No cross-field rule (multiphase/
  turbulence exclusivity, buoyancy needing thermal, etc.) is re-checked
  in QML; "Apply && Validate" always calls the real `validateDraft()`
  round trip. Verified by `CaseEditingTest.
  PhysicsEditRoundTripsThroughSaveReopenAndRuns` (enables thermal, adds
  the now-required per-patch temperature BCs, save/reopen/run) plus the
  pre-existing `SetPhysicsConfigRoundTripsAThermalBlock`.
* [x] Boundary-condition editor -- `apps/gui/qml/BoundaryEditor.qml`:
  left/right/top/bottom patch selector with a highlighted-edge domain
  preview, then velocity/pressure/(if enabled)temperature/species/alpha
  editors for the selected patch, all through `setBoundaryConfig()`.
  Type dropdowns are `velocityBoundaryTypes()`/`pressureBoundaryTypes()`/
  `temperatureBoundaryTypes()` -- the exact vocabulary
  `BoundaryConfigParser.cpp`/`CaseBuilder.cpp` accept. Verified by
  `CaseEditingTest.BoundaryEditRoundTripsThroughSaveReopenAndRuns` (edits
  one patch's velocity value, save/reopen/run) plus the pre-existing
  `SetBoundaryConfigOnlyKeepsTheFourCanonicalPatches`.
* [x] Solver-settings editor -- `apps/gui/qml/SolverEditor.qml`: SIMPLE's
  max iterations/relaxation factors/velocity-pressure-continuity
  tolerances, plus momentum/pressure linear-solver tolerances and max
  iterations, through `setSolverConfig()` -- exactly `cfd::io::
  SolverConfig`'s own fields, no invented second default set. **Known,
  explicitly-documented limitation** (in both this file and the QML
  page's own on-screen text): PISO, timestep/end-time, CFL monitoring,
  and restart are real, tested library capabilities (TODO.md P2) but are
  genuinely *not* reachable from the production case-file format at all
  today -- `ProjectRunner.hpp`'s own header comment scopes it to "steady-
  incompressible-SIMPLE(+thermal+species+multiphase+compressible)", and
  `CaseBuilder`/`ProjectRunner` never dispatch to `TransientSolver`/PISO.
  Building GUI controls for settings the production pipeline would
  silently ignore was judged worse than not offering them -- the page
  says this outright instead of shipping a no-op control. Verified by
  `CaseEditingTest.SetSolverConfigRoundTripsToleranceAndLinearSolver` and
  `ValidatedEditSavesAndRunsThroughTheSamePipelineAsCli` (changes
  `maxIterations`, confirms the changed value is what actually runs).
* [x] Full case creation from GUI -- `New Case` (dirty-state-guarded,
  `apps/gui/qml/Main.qml`'s `requestNewCase()`) creates a fresh typed
  `CaseDefinition` (`CaseSession::newCase()`); a left-nav shell
  (Case/Mesh/Physics/Boundaries/Solver/Results, `StackLayout`) routes
  between editor pages, all bound to the one model; `CasePage.qml` adds
  case name/description editing and Save/Save-As; a shared
  `ValidationPanel.qml` (bound to the new `validationIssues` property,
  see below) renders wherever it's placed, and clicking an issue jumps
  to its section. **The literal end-to-end gate**, verified by
  `CaseEditingTest.FullCaseCreationFromScratchValidatesSavesRunsAndMatchesCli`:
  `newCase()` -> `setMeshAndGeometry`(4x4/1x1) -> `setPhysicsConfig`
  (incompressible_laminar) -> `setBoundaryConfig` (all 4 patches, a
  moving-wall-lid cavity) -> `setSolverConfig` (SIMPLE + BiCGSTAB) ->
  `validateDraft()` (zero errors) -> `saveAs()` (a fresh temp directory,
  never an existing fixture) -> `run()` -> `Converged`/`hasResults()` ->
  independently re-run via `cfd::app::ProjectRunner::run()` (the exact
  entry point `apps/cli/main.cpp` itself calls) against the saved
  directory -> `Converged` again. This is the CLI/GUI round-trip gate
  satisfied by construction, not by a second, GUI-only path.
  Also new: `validationIssues` (`Q_PROPERTY QVariantList`) wraps
  `validationStatus` as a `{severity, section, field, message}`-shaped
  0-or-1-element list for a central validation panel to `Repeater` over,
  and `field` is now parsed out of `throwConfigError()`'s own canonical
  message shape (`JsonUtil.cpp`) -- honestly always at most one element
  (CaseReader/CaseBuilder fail fast, same as the CLI); this is presentation
  structure, not a second, independent multi-error-finding validation
  pass, which would have reintroduced exactly the "GUI-only validation
  path" this task forbids.

**Verification method, stated plainly**: every claim above is backed by
an automated test exercising the *real* `CaseModelAdapter` ->
`CaseSession` -> `CaseWriter`/`CaseReader`/`CaseBuilder` ->
`ProjectRunner` pipeline (26 tests total in
`apps/gui/tests/test_case_editing.cpp`, all passing, part of the
1257/1257 full regression and the 19 clean `ctest -j8` runs above) --
not screenshots or widget inspection. The QML pages themselves were
confirmed to parse and load with zero errors (a full `cfdapp_gui.exe`
launch stayed alive and produced no stderr output before being killed),
but this session had no interactive display session to drive a real
mouse-click walkthrough of the "Final Acceptance Scenario" end to end --
that remains worth doing by a human once, though every step it would
exercise already has direct automated coverage above.

## Performance Follow-up

* [ ] Persistent GPU-resident field/matrix pipeline
* [ ] Production GPU CG/BiCGSTAB integration
* [ ] Improve preconditioning
* [ ] Measure true CUDA end-to-end benefit
* [ ] Revisit OpenMP scaling
* [ ] Larger CPU/GPU benchmarks

---

# Immediate Next Task

**CI Gate and Release Gate are both fully closed (`v0.1.5` genuinely released). P5 is complete. The full "Production Physics Integration" backlog is done: Species (committed `19e2300`), and now Multiphase + Compressible + generic export/output wiring + GUI-reload discoverability, all production-integrated (physics.json parsing, `CaseBuilder`/`ProjectRunner` dispatch, CSV/VTK/JSON export, CLI report, `VisualizationSnapshot` reload support, `cases/multiphase_validation`/`cases/compressible_validation` examples, 39 new passing tests, full regression 1234/1234).**

**The parallel-test fixture race is closed (see its own section above -- both the originally-reported `valid_cavity` flake and a second, broader race across `cases/multiphase_validation`/`cases/compressible_validation`/`cases/species_diffusion` are fixed via `CaseFixtureCopy`, verified by 19 clean consecutive `ctest -j8` runs) and the full "GUI Improvements" backlog is done: Mesh/Physics/Boundary-condition/Solver-settings editors plus a complete New-Case-through-Run-through-CLI-round-trip workflow, all built on the one authoritative `CaseModelAdapter`/`SimulationController` editing model, all verified against the real `CaseSession`/`ProjectRunner` pipeline (26 new passing tests in `apps/gui/tests/test_case_editing.cpp`; see the "GUI Improvements" section above for the full per-item evidence and the one explicitly-documented gap: PISO/transient/CFL/restart are not yet reachable from the case-file format at all). Full regression 1257/1257, Python 105 passed/1 skipped.**

**Next: nothing queued from "GUI Improvements" or the fixture race -- both fully verified, including against the exact CI-pinned `clang-format-18` and a fresh Linux/gcc cross-platform build/test pass (see their own sections above). Worth doing when convenient: a human interactive pass clicking through the GUI's own "Final Acceptance Scenario" once (this session verified every step at the automated model/controller/pipeline level but had no display to drive real mouse clicks). Then "Performance Follow-up" (persistent GPU-resident pipeline, etc. -- see "Next Backlog" above).**
