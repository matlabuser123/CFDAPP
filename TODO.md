# CFDApp — TODO

**Current status:** P0/P1/P2/P3/P4 complete; P5 nearly complete (case manager, GUI solver workflow, field visualization/contours/vector plots/post-processing all rendered and tested in the GUI, residual monitoring incl. full history + reload, ParaView workflow, user documentation, and Windows packaging -- built/packaged/smoke-tested with real evidence -- are all done; the one remaining item is the actual GitHub Actions release-automation workflow run, blocked on this environment having no git remote/CI access -- see P5's own status note below)
**Current phase:** P5 — Application 🚧
**Priority:** Numerical correctness → validation → performance → features

---

# P0 — Numerical Foundation ✅

* [x] Core infrastructure
* [x] Structured 2D mesh
* [x] Fields
* [x] Boundary conditions
* [x] Sparse linear algebra
* [x] FVM operators
* [x] Incompressible momentum/continuity
* [x] SIMPLE
* [x] Pressure correction
* [x] Mass conservation
* [x] Poiseuille validation
* [x] Lid-driven cavity validation
* [x] Grid refinement
* [x] Determinism

---

# P1 — Application Foundation ✅

* [x] Case system
* [x] CLI case execution
* [x] CSV export
* [x] JSON metadata
* [x] VTK export
* [x] Python validation tooling
* [x] Regression suite
* [x] CI
* [x] ASan / UBSan
* [x] clang-format
* [x] clang-tidy
* [x] Quality gate

---

# P2 — Transient CFD ✅

* [x] Time controller
* [x] Implicit Euler
* [x] CFL monitoring
* [x] `TransientSolver`
* [x] PISO
* [x] Restart capability
* [x] Transient validation

---

# P2 — Thermal ✅

* [x] Thermal properties
* [x] Energy equation
* [x] Thermal BCs
* [x] Heated-cavity conduction validation
* [x] Conjugate heat-transfer foundation

---

# P2 — Turbulence ✅

* [x] Turbulence model interface
* [x] Laminar implementation
* [x] RANS framework
* [x] k-ε
* [x] k-ω
* [x] SST
* [x] Turbulence benchmark validation

**Gate:** 845/845 tests passing.

---

# P3 — Advanced Physics 🚧

## P3-PHYS-001 — Boussinesq Buoyancy ✅

* [x] Add thermal-expansion coefficient `β`
* [x] Add reference temperature `T_ref`
* [x] Implement Boussinesq density variation
* [x] Implement gravity vector
* [x] Add buoyancy source to momentum
* [x] Couple temperature → momentum
* [x] Preserve zero-buoyancy equivalence
* [x] Add equation-level buoyancy tests
* [x] Verify hydrostatic/source sign
* [x] Verify deterministic coupling


## P3-PHYS-002 — Natural Convection Validation ✅

* [x] Upgrade heated cavity to natural convection
* [x] Define Rayleigh/Prandtl numbers
* [x] Run multiple grids
* [x] Compare against published benchmark data
* [x] Validate velocity field
* [x] Validate temperature field
* [x] Validate Nusselt number
* [x] Verify heat balance
* [x] Verify mass conservation
* [x] Verify grid refinement
* [x] Verify determinism

## P3-PHYS-003 — Variable Properties ✅

* [x] Temperature-dependent viscosity
* [x] Temperature-dependent conductivity
* [x] Temperature-dependent heat capacity
* [x] Temperature-dependent density where appropriate
* [x] Property interpolation
* [x] Validation tests

## P3-PHYS-004 — Species Transport ✅

* [x] Species-field infrastructure
* [x] Advection-diffusion equation
* [x] Species BCs
* [x] Diffusivity model
* [x] Conservation checks
* [x] Analytical validation


## P3-PHYS-005 — Multiphase Foundation ✅

* [x] Phase representation
* [x] Volume-fraction field
* [x] Mixture properties
* [x] Interface-transport foundation
* [x] Conservation tests
* [x] Minimal validation case

## P3-PHYS-006 — Compressible Foundation ✅

* [x] Compressible fluid properties
* [x] Equation of state
* [x] Density coupling
* [x] Compressible continuity
* [x] Compressible momentum foundation
* [x] Energy coupling (via existing EnergyEquation/ThermalSolver -- see status note)
* [x] Low-Mach regression
* [x] Compressible validation case

---

# P4 — Performance ✅

* [x] Establish profiling baseline
* [x] Optimize matrix assembly
* [x] Optimize linear solves (investigated -- see status note)
* [x] OpenMP scaling
* [x] Memory/layout optimization (investigated -- see status note)
* [x] CUDA integration
* [x] CPU/GPU equivalence
* [x] Large-grid benchmarks

---

# P5 — Application 🚧

* [x] Production case manager
* [x] GUI solver workflow
* [x] Field visualization (scalar field map, rendered in the GUI)
* [x] Contours (rendered in the GUI, reusing `MarchingSquares` unchanged)
* [x] Vector plots (rendered in the GUI, reusing `VectorSampling` unchanged)
* [x] Residual monitoring (live panel + full multi-series history plot + reload from a completed run, all in the GUI)
* [x] Post-processing (probe + line sampling + CSV export, in the GUI, reusing `FieldProbe` unchanged)
* [x] ParaView workflow
* [x] User documentation
* [x] Packaging (built, packaged, and smoke-tested -- including the packaged GUI -- on a real Windows machine; real evidence in `results/release/0.1.0/`, see status note)
* [ ] Release automation (the actual GitHub Actions workflow has never run -- no git remote/CI access from this environment -- see status note)

**Gate:** 1149/1149 tests passing on the default (CPU-only, no GUI)
build (13 pre-existing disabled tests, same convention as every prior
gate); 1162/1162 on the GUI-enabled build (`-DCFDAPP_BUILD_GUI=ON`, +13
Qt controller tests). Confirmed on both configurations, on **both**
Linux/WSL and a real Windows machine (MSVC 19.51/VS 2026, Qt 6.9.3),
after every change below, not just once at the end -- both counts only
ever grew across this whole phase, never shrank.

**GUI visualization integration (completed):** the gap disclosed
earlier (viz/post-processing algorithms implemented and tested but not
rendered anywhere) is closed -- see the P5-B/P5-C/D/E/G/F notes below.

**Windows packaging (completed, real evidence):** built, packaged
(ZIP+NSIS), and smoke-tested -- CLI and GUI both -- on a real Windows
machine; see the P5-J/K status note below and
`results/release/0.1.0/`. Release *automation* (the actual GitHub
Actions workflow execution) is the one item still not done, disclosed
there.

**Architecture (section 0's "ONE SOLVER BACKEND"):** `apps/cli/main.cpp`
was refactored to a thin wrapper around a new `cfd::app::ProjectRunner`
(`include/cfd/app/ProjectRunner.hpp`) -- the exact CaseReader ->
CaseBuilder -> turbulence dispatch -> SIMPLE::solve() -> optional
thermal -> ResultExporter pipeline the CLI always ran, now a reusable
library function instead of logic inlined in `main()`. The GUI's
`SimulationController` (`apps/gui/`) calls this same function, on a
worker thread, through `cfd::app::CaseSession` (below) -- there is no
second, GUI-only solve path. `SIMPLE::solve()` itself gained two purely
additive, default-empty parameters (`SIMPLEProgressCallback`,
`SIMPLECancellationCheck` -- `include/cfd/pressure_velocity/
SIMPLEProgress.hpp`) plus a new `SIMPLEStatus::Cancelled`, since live
progress reporting and cooperative cancellation cannot be built any
other way without a second solve loop -- verified behavior-preserving
for every existing call site by the unchanged full regression count
above.

**P5-A -- Production case manager:** `cfd::app::CaseSession`
(`include/cfd/app/CaseSession.hpp`) -- an explicit state machine
(Empty/Loaded/Modified/Validated/Running/Completed/Failed/Cancelled,
section 5) wrapping CaseReader/CaseWriter (new -- the write-side
counterpart to CaseReader, `include/cfd/io/CaseWriter.hpp`)/CaseBuilder/
ProjectRunner: new/open/save/save-as/reload/validate/run/requestCancel.
CLI/GUI case-format round-trip (section 4) verified directly:
`CaseWriterTest.*` (read a case, write it to a fresh directory, read it
back, compare every field, including every optional thermal/turbulence/
buoyancy block and every BC type) and `CaseSessionTest.SaveAsWritesA-
RoundTripLoadableCase` (one session saves, an independent second session
opens what it wrote). 17 tests (`tests/unit/app/`).

**P5-B -- GUI solver workflow:** a real Qt 6.2/QML application,
`apps/gui/` (built only with `-DCFDAPP_BUILD_GUI=ON`; the default build
needs no Qt and is unaffected either way -- section 67). `Simulation-
Controller` (Qt QObject, `apps/gui/SimulationController.{hpp,cpp}`) runs
`CaseSession::run()` on a worker `std::thread` so the UI thread never
blocks (section 12); `run()`'s progress/cancellation callbacks only
`emit` Qt signals, relying on Qt's own automatic cross-thread queued
delivery (no manual mutex/queue). `qml/Main.qml`: New/Open/Save,
Validate, Run/Stop, a live iteration/residual progress panel, an error
banner -- every action calls straight into `SimulationController`, no
solver logic in QML (section 11). Verified two ways: (1) 6 controller-
level tests under a bare `QCoreApplication` (`apps/gui/tests/
test_simulation_controller.cpp`, `QSignalSpy` pumping the real Qt event
loop across the actual worker-thread hand-off, including a genuine
cancel-mid-solve -> `Cancelled` state test -- section 59's own "test
application logic separately from presentation"); (2) a real headless
launch (`QT_QPA_PLATFORM=offscreen ./cfdapp_gui`) confirmed the QML
loads and the event loop runs (not just "the binary links"). One real
bug caught and fixed by test (1): `canStop` briefly read `false`
immediately after clicking Run, because it delegated to the session's
own state instead of the controller's own synchronously-set flag.

**P5-B follow-up -- GUI visualization integration:** a new
`cfd::app::VisualizationSnapshot` (`include/cfd/app/
VisualizationSnapshot.hpp`) is the one "solver results -> C++
visualization model" bridge a live, just-completed run
(`buildSnapshot(ProjectRunResult)`) and a reloaded, previously-written
`results/` directory (`loadSnapshotFromResults()`, parsing
`metadata.json`/`fields.csv`/`residuals.csv` -- section 9's own "without
rerunning the solver") both populate identically; every visualization
Q_INVOKABLE on `SimulationController` reads this one snapshot, never the
live solver state a run in progress might still be mutating (section
10). `ProjectRunResult` gained one purely additive field (`mesh`, the
already-built `cfd::mesh::Mesh`) so a live run can build a snapshot
without re-parsing anything. `include/cfd/viz/FieldProbe.hpp` and
`VectorSampling.hpp` each gained a raw-array-based overload
(`probeScalarRaw`/`sampleLineRaw`/`sampleVectorFieldRaw`, alongside the
existing Mesh-based ones, unchanged) so probe/line-sample/vector-sample
work identically whether the snapshot is live or reloaded -- no
algorithm was rewritten, only extended (verified equivalent to the
existing Mesh-based results directly, `*Raw` tests in
`tests/unit/viz/`). `qml/Main.qml` now has a real result viewport
(Canvas-rendered scalar field map + contour overlay + vector glyphs,
one Canvas total, never one QML item per cell -- section 3), a click-to-
probe interaction, a line-sample panel with CSV export
(`exportLineSampleCsv`), and a full multi-series residual-history plot
(log-scale, values clamped to a small positive epsilon before
log-safety -- section 8) that also works against a reloaded completed
run via a new "Reload results" button
(`SimulationController::loadCompletedResults()`, also called
automatically after `openCase()` finds an existing `results/`
directory). Verified: 13 controller-level tests (`test_simulation_
controller.cpp`, exercising the exact data every QML view reads --
field list, scalar grid, contours, vectors, probe, line sample, CSV
export, residual history, auto-reload, version metadata) plus a
headless `QT_QPA_PLATFORM=offscreen` launch confirming the new QML has no
binding/type errors. CLI/GUI numerical equivalence re-confirmed
unaffected (the solver pipeline itself was not touched).

**P5-C/D/E/G -- Field visualization, contours, vector plots, post-
processing:** `include/cfd/viz/` -- pure C++, no Qt, no mesh-drawing
code (section 22's own "separate contour extraction from Qt
rendering"): deterministic 16-case marching-squares contour extraction
+ automatic level selection (`MarchingSquares.hpp`, including the
classic saddle-case ambiguity resolved by a documented, fixed
tie-break), nearest-point probe + line sampling (`FieldProbe.hpp`,
Mesh-based and raw-array-based), every-Nth-point vector-field
subsampling (`VectorSampling.hpp`, same two forms), and derived fields
-- velocity magnitude, a Green-Gauss vorticity, and min/max/average
field statistics (`DerivedFields.hpp`). Tested against synthetic fields
with known analytical answers (section 60's own example: `phi(x,y)=x`'s
contour at 0.5 lands at x=0.5, checked directly; a solid-body-rotation
field's vorticity matches its exact constant). 38 tests
(`tests/unit/viz/`). **Now rendered in the GUI** -- see the P5-B
follow-up note above; this was the single largest disclosed gap from
this task's own prior status note, now closed.

**P5-F -- Residual monitoring:** live panel (iteration count, progress
bar, continuity residual, straight from `SIMPLEProgress` -- never a
second GUI-computed residual, section 27) **plus** a full multi-series
residual-history plot and completed-run reload (see the P5-B follow-up
note above) -- both now in the GUI, reading the solver's own canonical
`results/residuals.csv`/in-memory history, never a second, GUI-computed
series.

**P5-H -- ParaView workflow:** the pre-existing `VTKWriter` already
exported a real `VECTORS velocity` field (section 36's own requirement
was already met, not new this task). New: an automated smoke test
(`tests/integration/io/test_paraview_smoke.cpp`, section 37) runs a real
case through `ProjectRunner` and checks the resulting `solution.vtk` for
mesh structure (`DATASET UNSTRUCTURED_GRID`/`POINTS`/`CELLS`/
`CELL_TYPES`) and every expected field name -- not just documented,
checked by every `ctest` run. `docs/user_guide/paraview.md` documents
the workflow.

**P5-I -- User documentation:** `docs/user_guide/{getting_started,
installation,cli,gui,visualization,paraview,troubleshooting}.md` (plus
the pre-existing `case_format.md`) -- accurate to what's actually
implemented today, including each doc's own explicit "what's not built
yet" section rather than describing aspirational features as done.

**P5-J -- Packaging: complete, real Windows evidence.** This
development environment gained direct PowerShell access to an actual
Windows machine (Visual Studio 2026/MSVC 19.51, installed earlier in
this project) -- Qt 6.9.3 (`win64_msvc2022_64` kit, via `aqtinstall`)
and NSIS (via `winget`) were installed, and `cmake/Packaging.cmake`
(CPack ZIP+NSIS, `-DCFDAPP_ENABLE_PACKAGING=ON`) was built, packaged,
and smoke-tested end to end -- see `docs/developer_guide/packaging.md`
and `results/release/0.1.0/` for the full log. Three genuine defects
were found and fixed only because this ran on real Windows (never
visible from Linux, where `cmake/QtDeploy.cmake` short-circuits
before reaching any of them):
1. Qt 6.5.3's originally-planned `win64_msvc2019_64` kit fails to
   compile against this machine's MSVC 19.51 toolset (`error C3861:
   'stdext': identifier not found` in Qt's own header) -- fixed by
   using Qt 6.9.3/`win64_msvc2022_64` instead.
2. `add_custom_command(TARGET cfdapp_gui POST_BUILD ...)`
   (`cmake/QtDeploy.cmake`) was `include()`'d from the wrong directory
   scope (`cmake/Packaging.cmake`, root-level) -- CMake requires that
   call in the same scope that created the target; fixed by moving the
   include into `apps/gui/CMakeLists.txt` itself.
3. The first real package built was only ~440KB -- `install(TARGETS
   ...)` never captures files a separate custom command (windeployqt)
   drops next to the executable, so the Qt runtime/plugins were
   silently absent. Fixed with an explicit `install(DIRECTORY
   $<TARGET_FILE_DIR:cfdapp_gui>/ ...)` rule; the corrected package is
   ~50-61MB and was confirmed, by direct execution, to run standalone.

Verified: full Windows `ctest` (1162/1162), packaged CLI (`--version` +
a real `lid_driven_cavity` run, from the extracted package), packaged
GUI (launched, stayed alive, real "windows" platform plugin), a
clean-machine-dependency approximation (packaged exes launched from a
process whose `PATH` excludes this machine's own Qt/Visual-Studio
install), CPU-only fallback (trivial -- CUDA was never enabled), and
version-metadata consistency (a real gap -- the GUI had no version
display at all -- fixed by adding `SimulationController::
applicationVersion`/`applicationName`, a window-title binding, and an
About dialog). SHA256 checksums generated. The exact, reproducible
recipe lives in `scripts/windows-release/` (four numbered PowerShell
scripts).

**P5-K -- Release automation: still not done.** The actual
`.github/workflows/release.yml` execution requires pushing a tag to a
GitHub remote -- this repository has no git remote configured and no
`gh` CLI available in this environment, so the workflow could not be
triggered or its result inspected from here. `release.yml` itself was
corrected to match everything verified above (Qt 6.9.3/`msvc2022_64`
instead of the untested original 6.5.*/`msvc2019_64` guess, the real
NSIS output filename with no `-Setup` suffix, and a packaged-GUI
smoke-test step it was missing) -- the best available starting point,
but genuinely unexecuted. Per this project's own "do not mark release
automation complete until the produced artifact is tested" rule, this
checkbox stays unchecked until someone with push/CI access actually
runs it and (per the task's own section 20) downloads and retests the
resulting artifact.

**Deliberately not attempted this task:** wiring species/multiphase/
compressible physics into `ProjectRunner`'s production dispatch (still
blocked on `physics.json` case-config parsing for those modules, a
P3-PHYS-006/P4-disclosed prerequisite, not a P5 regression); in-GUI
mesh/physics/BC editing (today's GUI opens/saves/validates/runs a case
exactly as its JSON already describes it -- editing still means editing
the JSON, same as CLI-only use).

---

# Current Work Queue

```text
DONE
├── P3-PHYS-001 — Boussinesq Buoyancy
├── P3-PHYS-002 — Natural Convection Validation
├── P3-PHYS-003 — Variable Properties
├── P3-PHYS-004 — Species Transport
├── P3-PHYS-005 — Multiphase Foundation
├── P3-PHYS-006 — Compressible Foundation
└── P4 — Performance

NOW
└── P5 — Application (in progress -- see its own status note above)

THEN
└── (none)

LATER
└── (none)
```

## Immediate Next Task

**Finish P5 — Application's one remaining disclosed item:**

1. Get this repository pushed to a GitHub remote with `gh`/CI access
   (neither exists in this environment) and actually run
   `.github/workflows/release.yml` once, end to end, from a `v0.1.0`-
   style tag -- only then mark P5-K (Release automation) complete, per
   `docs/developer_guide/packaging.md`'s own disclosed checklist. This
   is now the *only* unchecked item in P5's own completion checklist --
   everything else, including Packaging itself (built/packaged/smoke-
   tested with real evidence on a real Windows machine -- see
   `results/release/0.1.0/`), is done and tested.
2. Only after (1): mark P5 complete, delivering its own Final Report,
   and stop -- do not begin a new solver-development phase
   automatically (per P5's own closing instruction).

Also worth doing, not blocking: in-GUI mesh/physics/BC editing (today's
GUI opens/saves/validates/runs/post-processes a case exactly as its
JSON already describes it -- editing still means editing the JSON by
hand, same as CLI-only use); wiring species/multiphase/compressible
physics into `ProjectRunner`'s production dispatch (blocked on
`physics.json` case-config parsing for those modules, a
P3-PHYS-006/P4-disclosed prerequisite).

Separately disclosed, not blocking any of the above: P4 left a
persistent device-resident `DeviceArray`/`CudaMatrix` layer,
`CudaCG`/`CudaBiCGSTAB`, an ILU-style CPU preconditioner, and a
warp-per-row SpMV kernel variant; P3-PHYS-006 left a compressible
pressure-correction/SIMPLE variant, a dedicated conservative-enthalpy
energy equation, compressible inlet/outlet boundary-condition classes,
`physics.json` case-config parsing for compressible/species/multiphase
blocks (also what currently blocks wiring those physics modules into
`ProjectRunner`'s dispatch), and CSV/VTK/JSON export wiring for those
newer physics modules.
