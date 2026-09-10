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
* [ ] gcc/debug build-test green
* [ ] gcc/release build-test green
* [ ] clang/debug build-test green
* [ ] sanitizers green
* [ ] Full CI workflow green on `main`

**Known issue:** one transient parallel-fixture race has been observed in `LoadSnapshotFromResultsTest.ReloadsAPreviouslyWrittenResultsDirectory`; it passes in isolation but should not be treated as a fully clean local regression result until the race is resolved or eliminated.

## Release Gate

* [x] Keep existing tags immutable
* [x] Version/release fixes pushed to `main`
* [ ] CI Gate fully green
* [ ] Bump project version for the next clean release
* [ ] Create a new release tag
* [ ] Release workflow green end to end
* [ ] Windows build succeeds on GitHub runner
* [ ] Full release test suite passes
* [ ] ZIP artifact produced
* [ ] Installer artifact produced
* [ ] CI smoke tests pass on packaged artifacts
* [ ] Downloaded release artifact retested
* [ ] GitHub Release published with assets
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

**Finish the CI Gate on commit `1b6d350`; do not create another release tag until all required CI jobs are green.**
