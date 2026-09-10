# CFDApp — TODO

**Current phase:** P5 — Application / Release
**Priority:** correctness → validation → release → future features

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
* [x] CI
* [x] Sanitizers
* [x] clang-format / clang-tidy
* [x] Quality gate

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
* [x] Species transport
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
* [x] CUDA integration
* [x] CPU/GPU equivalence
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
* [ ] Release automation

## CI gate (checked separately from the release gate -- ci.yml, not release.yml)

* [x] clang-tidy clean
* [x] python suite clean
* [x] clang-format clean -- was genuinely failing (all P3-P5 code added this
  session was never run through it); fixed in `cfe6606` (repo-wide
  `clang-format -i`, verified 0 violations, full local rebuild + regression
  1162/1162 unaffected), pushed to `main`. That fix alone was still
  insufficient: GitHub's `ubuntu-latest` runner had moved from 22.04 to
  24.04 mid-session, shipping a newer default `clang-format` that
  disagrees with clang-format-14/15 on short-lambda-in-function-call
  wrapping (2 pre-existing files affected: `test_divergence.cpp`,
  `test_continuity_equation.cpp`) -- confirmed the two tool versions are
  mutually incompatible (clang-format-18's output has 45 violations under
  clang-format-14). Fixed by (1) reformatting those 2 files with
  clang-format-18 and (2) pinning `ci.yml`'s `format` job to install and
  use clang-format-18 explicitly (via apt.llvm.org's script) instead of
  the runner image's drifting default. Verified 0 violations locally
  under clang-format-18; full local rebuild + regression 1161/1162 passed
  (1 known transient parallel-fixture-race failure,
  `LoadSnapshotFromResultsTest.ReloadsAPreviouslyWrittenResultsDirectory`,
  reconfirmed passing in isolation). Not yet reconfirmed by an actual
  completed GitHub Actions `format` job on this fix.
* [ ] build-test (gcc/debug, gcc/release, clang/debug) all green in CI
* [ ] sanitizers green in CI (every observed run has taken 35-45+ minutes
  with no completion seen this session -- genuinely slow or stuck, not
  yet distinguished)

## Release gate

* [x] Keep existing published tags immutable (v0.1.0/v0.1.1/v0.1.2 untouched)
* [x] Update project version before the next release tag (0.1.2, matches `v0.1.2`)
* [x] Push version/release fixes to `main` (version bump `ccab01d`; MSVC
  dev-env activation fix `1195a6c`; clang-format fix `cfe6606`)
* [ ] Create the next clean release tag -- awaiting go-ahead on a name
  (e.g. `v0.1.3`) for `cfe6606`; not created without explicit confirmation
* [ ] Run GitHub Actions Release workflow end to end -- **5/5 attempts
  failed so far** (v0.1.0 x3, v0.1.1 x1, v0.1.2 x1), each exposing a real,
  previously-invisible defect, each fixed on `main`: invalid Qt module ->
  stale checkout working-directory -> missing `#include <algorithm>` ->
  tag/project-version mismatch -> MinGW-vs-MSVC linker mismatch (just
  fixed, unverified)
* [ ] Confirm build succeeds on GitHub Windows runner
* [ ] Confirm full tests pass in CI
* [ ] Confirm ZIP / installer artifacts are produced in CI
* [ ] Smoke-test generated release artifacts in CI
* [ ] Confirm GitHub Release is published (none exists yet -- `gh release
  list` is empty)
* [ ] Mark P5 complete

---

# Next Backlog

## Production physics integration

* [ ] Add `physics.json` parsing for species
* [ ] Add multiphase case configuration
* [ ] Add compressible case configuration
* [ ] Wire species into `ProjectRunner`
* [ ] Wire multiphase into `ProjectRunner`
* [ ] Wire compressible solver path into `ProjectRunner`
* [ ] Add output/export support for new physics

## GUI improvements

* [ ] Mesh editing
* [ ] Physics-property editing
* [ ] Boundary-condition editing
* [ ] Case creation entirely from GUI

## Performance follow-up

* [ ] Persistent GPU-resident field/matrix pipeline
* [ ] Integrate GPU CG/BiCGSTAB into production solve path
* [ ] Improve preconditioning
* [ ] Revisit OpenMP scaling
* [ ] Larger CPU/GPU benchmarks

---

# Immediate Next Task

Finish the release gate.

Do not begin another major solver-development phase until the GitHub release workflow passes end to end and the generated release artifacts are verified.
