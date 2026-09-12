# CFDApp — Roadmap

## Project Status

**Released:** v0.2.0
**Current:** P10 — Production Physics Integration closeout (most of P10 and
P11 were actually completed *before* v0.2.0 shipped; this phase reconciles
the record and closes the real remaining gaps — see P10 below)
**Next:** P11 — GUI Case Authoring closeout (same situation as P10), then
P12 — Solver & Physics Expansion

**Execution status:** See `TODO.md`.
**Verification evidence:** See `results/`.

Development sequence:

```text
Numerical foundation
    ↓
Validation
    ↓
Transient / thermal / turbulence
    ↓
Advanced physics foundations
    ↓
Performance (CPU/OpenMP/CUDA foundations)
    ↓
Production application (CLI + GUI)
    ↓
GPU/performance validation
    ↓
Production hardening
    ↓
v0.2.0 release
    ↓
Production physics integration + GUI case authoring
  (both substantially done pre-release; closeout in progress ← current)
    ↓
Solver/physics expansion
    ↓
Production maturity
```

---

# Completed Development

## P0–P5 — Core CFDApp ✅

- Numerical foundation and SIMPLE (structured mesh, finite-volume operators,
  sparse linear algebra, incompressible momentum/continuity, pressure
  correction, deterministic execution)
- Validation and grid refinement (Poiseuille, lid-driven cavity, analytical
  comparison tooling, regression suite, CI, sanitizers, static
  analysis/formatting gates)
- Transient CFD and PISO (implicit Euler, CFL monitoring, restart capability)
- Thermal transport (energy equation, thermal BCs, conjugate heat-transfer
  foundation)
- Turbulence models (laminar, k-ε, k-ω, SST — validated against channel-flow
  log-law/Re_τ benchmarks)
- Buoyancy and natural convection (Boussinesq, validated against De Vahl
  Davis 1983)
- Variable properties (temperature-dependent viscosity/conductivity/heat
  capacity)
- Species, multiphase, and compressible physics: equation-level foundation
  **and** production integration (`physics.json`/`boundaries.json` parsing,
  `CaseBuilder`/`ProjectRunner` dispatch, CLI reporting, GUI editing,
  CSV/VTK/JSON export, production example cases) — see P10 below for the
  evidence; this was completed pre-v0.2.0 but not reflected here until
  this correction (see P10's "Reconciliation note")
- Production CLI/case system (JSON case format, New/Open/Save/Validate/Run,
  CLI↔GUI compatibility)
- GUI workflow (Qt/QML app, shared production solver backend, worker-thread
  execution, run/stop/progress/failure handling) **and** GUI case-authoring
  editors (mesh/physics/boundary/solver, full case-creation-from-scratch) —
  see P11 below for the evidence; also completed pre-v0.2.0
- Visualization/post-processing (scalar maps, contours, vector glyphs,
  residual monitoring, probes/line-sampling, VTK/ParaView export)
- Packaging/release foundations (CPack, Qt deployment, GitHub release
  workflow; first genuine release v0.1.5)

## P6 — GPU Performance ✅

- Persistent GPU pipeline: fields/matrices GPU-resident across iterations,
  minimal host/device transfers, verified CPU/GPU numerical equivalence
- Production GPU CG/BiCGSTAB in the SIMPLE path, with robust CPU fallback
- GPU-resident Jacobi preconditioning (diag(A)⁻¹, fully on-device); stronger
  candidates (block/damped Jacobi, polynomial, approximate inverse, ILU(0))
  investigated and rejected for scope

**Evidence:** `results/performance/preconditioner/`.

## P7 — Performance Validation ✅

- CUDA end-to-end benchmarking: real production-SIMPLE CPU-vs-GPU timing,
  20×20–320×320. GPU break-even **80×80**; best measured speedup **2.01× at
  320×320**.
- OpenMP scaling: 1–32+ threads, 160×160. Best **4 threads, 1.33× speedup**;
  regresses severely at 16/32 threads (WSL2 thread-spawn overhead).
  Bit-identical results across thread counts.
- Large-grid stress testing: largest stable CPU grid **480×480**, GPU
  **640×640**; limiting factor is solver iteration budget, not memory.

**Evidence:** `results/performance/{cuda_end_to_end,openmp_scaling,large_grid_stress}/`.

## P8 — Production Hardening ✅

- Full regression suite, parallel `ctest`, AddressSanitizer/UndefinedBehaviorSanitizer
  (zero diagnostics), `clang-format`/`clang-tidy` (zero violations)
- Native Windows build verified (MSVC, Qt 6.9.3) and WSL2/Linux build
  verified (CPU-only and CUDA-enabled on real GPU hardware)
- Manual GUI acceptance: 17/17 PASS, human-executed (one real bug found and
  fixed along the way)
- Production documentation updated

**Evidence:** `results/release/p8-hardening/`.

---

# P9 — v0.2.0 Release ✅

Goal: qualify and publish the first v0.2 production release after GPU
performance validation and production hardening.

**Release evidence:** `results/release/v0.2.0/`; tagged/published at
`1e960c7` (run `34617756507` CI, run `34675450224` release). See `TODO.md`
for the full closure record.

**Known documentation defect in the published release notes (disclosed,
not corrected in place):** the shipped v0.2.0 GitHub Release's "Known
limitations" section states species/multiphase/compressible are "not yet
reachable through `physics.json`/`ProjectRunner`'s production dispatch."
That statement is false against the source tree that was actually
released — see P10 below. The published release itself is left untouched
(release evidence is immutable); the correction applies going forward
(this document, `TODO.md`, and the next release's notes).

---

# P10 — Production Physics Integration ✅ (reconciled)

**Reconciliation note (read this first):** this phase's checklist below
was, until now, entirely unchecked — but the underlying work was actually
implemented and shipped in v0.2.0, under an earlier internal numbering
(`P6-PHYS-001/002/003`), in commits `19e2300` (species), `5603621`
(multiphase + compressible), both ancestors of the released `1e960c7`. A
later documentation refactor (`a5af328`) renumbered a stale future-phase
template onto this already-completed work without checking it against the
source tree, and an earlier squash (`4e5a4d2`) had compressed the original
detailed evidence back down to generic "foundation" bullets — see P0–P5
above. This entry now reflects the source tree as independently
re-verified (fresh build + full regression, see `TODO.md`), not the
original aspirational draft.

**Goal:** move the advanced-physics foundations (species, multiphase,
compressible) into the normal production case/configuration/dispatch/export
workflow, in that priority order.

## P10-APP-001 — Species Production Integration ✅

- [x] `physics.json` species configuration — `PhysicsConfigParser.cpp`
- [x] `CaseBuilder` integration — `SimulationSetup::species`
- [x] `ProjectRunner` dispatch — one `SpeciesSolver::solve()` per species
- [x] Production example case — `cases/species_diffusion`
- [x] CLI verification — `apps/cli/main.cpp` species report block
- [x] GUI verification — `PhysicsEditor.qml` species editor; P8 manual GUI
  acceptance (`results/release/p8-hardening/gui_acceptance.md`) exercised
  the Physics editor on a real case
- [x] Species-field export — `concentration_<name>` in CSV/VTK/JSON
- [x] End-to-end regression — `tests/integration/case/test_species_production_case.cpp`,
  7/7 PASS (freshly re-run, see `TODO.md`)

## P10-APP-002 — Multiphase Production Integration ✅

- [x] Multiphase configuration parsing — `PhysicsConfigParser.cpp`
- [x] Phase construction from case configuration — `TwoPhaseSystem`
- [x] Volume-fraction transport wired into production runner — one
  `VolumeFractionSolver::step()` per run
- [x] Mixture-field exposure — `volume_fraction`/`mixture_density`/
  `mixture_viscosity`
- [x] Production example case — `cases/multiphase_validation`
- [x] CLI verification — `apps/cli/main.cpp` multiphase report block
- [x] GUI verification — `PhysicsEditor.qml` multiphase editor
- [x] Restart/export verification — CSV/VTK/JSON export confirmed
- [x] End-to-end regression — `tests/integration/case/test_multiphase_production_case.cpp`,
  7/7 PASS (freshly re-run)

**Disclosed, deliberate scope limit (not a gap to close silently):**
mixture density is intentionally **not** coupled into the continuity
equation (`MultiphaseProperties.hpp`'s own documented scope) — only
mixture viscosity feeds SIMPLE's momentum assembly. This should stay an
explicit documented decision (see "Still open" below), not be treated as
an oversight.

## P10-APP-003 — Compressible Production Integration ✅ (post-hoc scope)

- [x] Compressible configuration parsing — `PhysicsConfigParser.cpp`
- [x] EOS configuration — `IdealGasEOS`/`ThermodynamicProperties`
- [x] Pressure/reference-pressure configuration
- [x] Production solver dispatch — post-hoc EOS/Mach/continuity-imbalance
  pass, honestly labeled as such in the GUI and in `ProjectRunner.hpp`
- [x] Density/Mach/absolute-pressure export
- [x] Production low-Mach example — `cases/compressible_validation`
- [x] CLI verification — `apps/cli/main.cpp` compressible report block
- [x] GUI verification — `PhysicsEditor.qml` compressible editor
- [x] End-to-end regression — `tests/integration/case/test_compressible_production_case.cpp`,
  7/7 PASS (freshly re-run)
- [x] **Compressible boundary conditions** — closed under `P12-COMP-001`
  (not this phase, since it needed dedicated audit/design work — see
  P12-COMP below): a real EOS-evaluated boundary-density model now
  replaces the owner-cell-reuse simplification. Evidence:
  `results/p12-comp-001/summary.md`.
- [ ] **Compressible energy coupling** — genuinely open in the "real
  two-way coupling" sense: there is no coupled compressible
  pressure-velocity solver, only a one-way, post-hoc reinterpretation.
  **Scope decision, recorded here:** a real coupled compressible solver
  is large new numerics — deferred to P12-COMP rather than committed to
  under P10. The current honest post-hoc scope is what P10-APP-003 is
  considered complete against.

## P10-APP-004 — Production Physics Compatibility Matrix ✅

- [x] Audited all cross-block combinations of thermal/turbulence/buoyancy/
  species/multiphase/compressible against `ProjectRunner.cpp`'s actual
  dispatch order and each module's own documented scope
- [x] Consolidated the previously ad hoc, scattered cross-checks into one
  authoritative function, `validatePhysicsCompatibility` in
  `src/io/case/PhysicsConfigParser.cpp`, called once after every block is
  parsed
- [x] Added the one missing rejection: multiphase excludes compressible
  (a linear-mixture density/viscosity model and an ideal-gas EOS
  reinterpretation describe incompatible fluids) — previously silently
  allowed and untested
- [x] Confirmed, not assumed: GUI validation already reaches this same
  function (no duplicate rules existed in `PhysicsEditor.qml`/
  `CaseModelAdapter.cpp` — GUI "Validate"/"Save" round-trips through
  `cfd::io::CaseBuilder{}.build(...)`, the same call CLI/`ProjectRunner`
  use), so connecting GUI/`ProjectRunner`/JSON validation to one authority
  required no additional wiring, only the consolidation above
- [x] New test file `tests/unit/io/test_physics_compatibility.cpp` (10
  tests): every supported combination not covered elsewhere
  (species+multiphase, species+compressible, compressible+buoyancy,
  compressible+turbulence, and the maximal "everything compatible at
  once" case), every unsupported combination with diagnostic-message
  content checks, and one conflicting/malformed-configuration case
- [x] Fresh full verification: `CFDIoTests` 194/194, `CFDCaseIntegrationTests`
  22/22, `CFDGuiControllerTests` 40/40, full regression 1300/1300 (up
  from 1290/1290 — 10 new tests, zero regressions)

**Evidence:** `results/p10-app-004/summary.md`.

## Still open beyond P10-APP-004

- Document the compatibility matrix in `docs/user_guide/case_format.md`
  (today it exists only as `validatePhysicsCompatibility`'s own header
  comment) — tracked in `TODO.md`.
- `docs/user_guide/case_format.md`/`schemas/README.md` document only the
  bare-minimum `physics.json` (`model`/`density`/`dynamic_viscosity`/
  `reynolds_number`) and `boundaries.json` (`velocity`/`pressure`) —
  thermal, turbulence, buoyancy, species, multiphase, and compressible are
  all undocumented there despite being fully implemented. This predates
  P10 but should close alongside it.
- ~~At least one representative combined-physics production example
  case~~ — done: `cases/heated_species_diffusion` (thermal+species) +
  `tests/integration/case/test_heated_species_diffusion_production_case.cpp`
  (5 tests, hand-verified against both fields' closed-form analytical
  profiles). Full regression 1305/1305.
- No standalone grid-refinement/analytical validation study exists under
  `results/validation/multiphase/` (Poiseuille/cavity/turbulence all have
  one) — evidence-parity gap, not a correctness concern.
- Multiphase's density-non-coupling and compressible's post-hoc-only scope
  (both noted above) should be written down as explicit decisions, not
  left implicit in code comments only.

**P10 Acceptance:** species, multiphase, and compressible capabilities are
configurable and runnable through the production application without
custom test-only code paths (✅); the compatibility matrix is implemented,
connected, and tested (✅, P10-APP-004); its user-facing documentation and
a combined-physics example case remain open (tracked in `TODO.md`).

---

# P11 — GUI Case Authoring ✅ (reconciled, one manual-verification gap open)

**Reconciliation note:** same situation as P10 — this checklist was
entirely unchecked, but the work was implemented and shipped pre-v0.2.0 in
commit `3d26930` ("...add GUI case editors (mesh/physics/boundary/solver/
full-case-creation)"), verified there at 1260/1260 (Windows) and 1221/1221
(Linux) full regression, 19 consecutive clean `ctest -j8` runs, and clean
clang-format/clang-tidy. Freshly re-confirmed: `CFDGuiControllerTests`
40/40 PASS, including `CaseEditingTest.FullCaseCreationFromScratchValidatesSavesRunsAndMatchesCli`.

**Goal:** allow ordinary users to create and configure production cases
without manually editing JSON.

## P11-GUI-001 — Mesh Editor ✅

- [x] Mesh dimensions, domain dimensions, mesh preview, input validation —
  `apps/gui/qml/MeshEditor.qml`

## P11-GUI-002 — Physics Editor ✅

- [x] Flow-regime selection, material properties, thermal configuration,
  turbulence selection, species/multiphase/compressible controls —
  `apps/gui/qml/PhysicsEditor.qml`

## P11-GUI-003 — Boundary-Condition Editor ✅

- [x] Boundary/BC-type selection; edit velocity, pressure, temperature,
  species, volume fraction; validation/error reporting —
  `apps/gui/qml/BoundaryEditor.qml`

## P11-GUI-004 — Solver Settings Editor ✅

- [x] SIMPLE/PISO configuration, linear-solver selection, tolerances,
  relaxation factors, time-step/CFL controls, CPU/OpenMP/GPU backend
  selection — `apps/gui/qml/SolverEditor.qml`

## P11-GUI-005 — Case Creation Wizard ⏳ (backend done, manual GUI step open)

- [x] New case without manual JSON editing, guided setup, pre-save
  validation, CLI-compatible output — backend logic verified
  (`CaseEditingTest.FullCaseCreationFromScratchValidatesSavesRunsAndMatchesCli`)
- [ ] **Manual, human-executed GUI verification of creating a brand-new
  case from scratch** — the P8 manual GUI acceptance checklist
  (`results/release/p8-hardening/gui_acceptance.md`) only opened an
  *existing* case (step 2); it never exercised the new-case-creation flow
  end to end by hand. Genuinely open — needs one more human-executed
  checklist addendum.
- [ ] Template selection — not confirmed present; verify alongside the
  manual step above.

**P11 Acceptance:** a representative supported case can be created,
configured, validated, saved, reopened, and executed entirely through the
GUI without manual JSON editing, while remaining CLI-compatible. Backend
logic meets this (✅); the human-executed manual confirmation specifically
for *case creation* (as opposed to opening an existing case) is the one
remaining gap.

---

# P12 — Solver & Physics Expansion

Begin after production integration is stable. These are planned capability
directions, not current commitments.

## P12-NUM — Numerics

- [ ] Higher-order convection schemes
- [ ] Additional preconditioners
- [ ] Multigrid
- [ ] Fully coupled pressure-based solver

## P12-COMP — Compressible CFD ⏳ (P12-COMP-001 done, P12-COMP-002 not started)

**Scope decision (made during P10-APP-003 closeout, recorded here):** the
two items below were deliberately deferred out of P10 rather than
committed to under it — P10-APP-003 is considered complete against the
current, honestly-disclosed post-hoc-reinterpretation scope (see P10
above), and these are real, large new numerics, not wiring.

**Audit (done before any implementation):** every compressible capability
(EOS, density calc, continuity, momentum coupling, energy coupling,
pressure-density coupling, boundary density, solver architecture,
low-Mach regression, exports, GUI) was classified against source/tests,
not ROADMAP's own prior checkboxes. Two non-obvious findings: (a)
`CompressibleMomentum.{hpp,cpp}` is a fully-implemented, unit-tested,
variable-density momentum assembler that is completely unwired from
production — a ready-to-integrate building block for P12-COMP-002, not
something to write from scratch; (b) the existing low-Mach regression
test is self-consistency only (its headline "EOS consistency" assertion
is tautological), not validation against an independent analytical/
literature reference. Evidence: `results/p12-comp-001/summary.md`.

### P12-COMP-001 — Boundary-density model ✅

- [x] Replaced the owner-cell-reuse simplification with a real
  EOS-evaluated boundary density: `calculateCompressibleMassFlux` now
  evaluates each boundary face's own boundary-interpolated absolute
  pressure/temperature (via the *existing* generic
  `cfd::discretization::interpolateFace` and the case's own existing
  pressure/temperature boundary conditions — no new BC types or
  case-format keys needed).
- [x] Explicit behavior confirmed for every existing boundary-condition
  type: Outlet (Dirichlet pressure) gets the exact reference-pressure-
  consistent density, not the interior's; Inlet/Wall (typically
  zero-gradient pressure) reduce to the previous owner-cell result when
  the gradient is genuinely zero; Wall's own zero-velocity BC makes the
  boundary-density choice irrelevant to conservation there either way.
  9 new/rewritten unit tests in `test_compressible_mass_flux.cpp`
  (internal-face interpolation unchanged, inlet/outlet/wall behavior,
  EOS consistency, invalid/non-finite boundary state, low-Mach limiting
  behavior) — 50/50 `CFDCompressibleTests` PASS.
- [x] New integration test proving the production path exercises the new
  treatment (`test_compressible_production_case.cpp`) — see
  `results/p12-comp-001/summary.md` for the exact assertion.
- [x] Fresh verification: `CFDCompressibleTests` 50/50,
  `CFDLowMachRegressionTests` 7/7 (unchanged, confirming the existing
  low-Mach regression still holds under the new treatment),
  `CFDCaseIntegrationTests`, `CFDIoTests`, and the full regression suite
  all re-run — see `TODO.md` for exact counts.

### P12-COMP-002 — Coupled compressible pressure-velocity solver (not started)

- [ ] A genuinely coupled compressible pressure-velocity solver (today,
  compressible is a one-way, post-hoc reinterpretation of an already-
  converged incompressible SIMPLE result -- never a second, coupled flow
  solve). Proposed formulation, dependencies, and an ordered task list
  already exist in this session's approved plan (Patankar-style
  compressible-SIMPLE, reusing the already-built but currently-unwired
  `CompressibleMomentum` and `IdealGasEOS::dDensityDPressure`) — not
  started per explicit instruction to stage P12-COMP-001 and
  P12-COMP-002 separately.
- [ ] Advanced compressible-energy formulation
- [ ] Higher-Mach capability

## P12-TURB — Turbulence

- [ ] Advanced turbulence validation
- [ ] Additional production turbulence capabilities where justified

## P12-SPECIES — Species & Reactions

- [ ] Additional species models
- [ ] Reaction/source-term framework

## P12-MULTI — Multiphase

- [ ] Advanced interface methods
- [ ] Surface tension
- [ ] Interface reconstruction

## P12-MESH — Geometry

- [ ] Moving/deforming meshes
- [ ] 3D foundation

---

# P13 — Production Maturity

Long-term hardening for broader production use.

## Reliability

- [ ] Crash reporting
- [ ] Structured diagnostic logging
- [ ] Long-duration stability tests
- [ ] Large-case stress tests

## Results & Benchmarking

- [ ] Result-comparison tools
- [ ] Automated benchmark dashboard

## Compatibility

- [ ] Backward-compatible case-schema migration
- [ ] Plugin/model extension architecture

## Distribution

- [ ] Cross-platform packaging
- [ ] Linux release
- [ ] Automated installer testing

## Release Engineering

- [ ] Formal release-candidate qualification process

---

# Dependencies

```text
P9  v0.2.0 Release
 ↓
P10 Production Physics Integration
 ↓
P11 GUI Case Authoring
 ↓
P12 Solver & Physics Expansion

P10/P11 ↘
          P13 Production Maturity
```

P11 infrastructure work may overlap with late P10 only if production
schemas/interfaces are stable and `TODO.md` explicitly authorizes it.

P13 is not strictly gated behind all of P12: reliability, compatibility, and
distribution items (crash reporting, diagnostic logging, schema migration,
Linux packaging, installer testing) may proceed alongside later P10/P11 work
once those phases are far enough along for such hardening to be meaningful,
without waiting for the full P12 solver/physics-expansion scope. Do not
silently start a later phase — any overlap still needs `TODO.md` to
explicitly authorize it.

---

# Roadmap Principles

1. Numerical correctness before optimization.
2. Validation before performance claims.
3. Production integration before GUI exposure.
4. CPU reference implementation remains authoritative unless explicitly
   superseded.
5. GPU/OpenMP paths require numerical equivalence.
6. New physics requires physical validation, not only unit tests.
7. New features require production-path integration.
8. Failed/excluded cases remain documented.
9. Performance claims require reproducible measured evidence.
10. A roadmap item becomes complete only after implementation, testing,
    validation, integration, documentation, and production usability.

---

# Definition of Done

```text
Implementation
    ↓
Focused tests
    ↓
Numerical/physical validation
    ↓
Regression
    ↓
Determinism/equivalence
    ↓
Production integration
    ↓
Documentation
    ↓
Usable through supported application path
    ↓
COMPLETE
```

Never mark a feature complete because code merely exists.

---

# Current Execution

See `TODO.md` for:

- current phase
- active gate
- blockers
- release candidate
- CI state
- immediate next task

`TODO.md` is the only live execution checklist. This file does not track
day-to-day status, so it never goes stale when a commit or CI run changes.
