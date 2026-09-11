# CFDApp — Roadmap

## Project Status

**Released:** v0.1.5
**Current:** P9 — v0.2.0 Release
**Next:** P10 — Production Physics Integration

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
v0.2.0 release  ← current
    ↓
Production physics integration
    ↓
GUI case authoring
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
- Species transport foundation (advection-diffusion, analytically validated)
- Multiphase foundation (volume-fraction transport, conservation/boundedness
  checks)
- Compressible foundation (ideal-gas EOS, low-Mach regression)
- Production CLI/case system (JSON case format, New/Open/Save/Validate/Run,
  CLI↔GUI compatibility)
- GUI workflow (Qt/QML app, shared production solver backend, worker-thread
  execution, run/stop/progress/failure handling)
- Visualization/post-processing (scalar maps, contours, vector glyphs,
  residual monitoring, probes/line-sampling, VTK/ParaView export)
- Packaging/release foundations (CPack, Qt deployment, GitHub release
  workflow; first genuine release v0.1.5)

**Note:** species/multiphase/compressible above are equation-level
foundations, validated but not yet reachable through the production
case/CLI/GUI dispatch path — that is P10's scope.

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

# P9 — v0.2.0 Release ← CURRENT

Goal: qualify and publish the first v0.2 production release after GPU
performance validation and production hardening.

**Current execution state:** See `TODO.md`.

**Release evidence:** `results/release/v0.2.0/`.

P10 may begin only after the required P9 release gates are complete.

---

# P10 — Production Physics Integration

Begin only after P9 release qualification is complete.

**Goal:** move the existing advanced-physics foundations (species,
multiphase, compressible — see P0–P5 note above) into the normal production
case/configuration/dispatch/export workflow, in that priority order.

## P10-APP-001 — Species Production Integration

- [ ] `physics.json` species configuration
- [ ] `CaseBuilder` integration
- [ ] `ProjectRunner` dispatch
- [ ] Production example case
- [ ] CLI verification
- [ ] GUI verification
- [ ] Species-field export
- [ ] End-to-end regression

## P10-APP-002 — Multiphase Production Integration

- [ ] Multiphase configuration parsing
- [ ] Phase construction from case configuration
- [ ] Volume-fraction transport wired into production runner
- [ ] Mixture-field exposure
- [ ] Production example case
- [ ] CLI verification
- [ ] GUI verification
- [ ] Restart/export verification
- [ ] End-to-end regression

## P10-APP-003 — Compressible Production Integration

- [ ] Compressible configuration parsing
- [ ] EOS configuration
- [ ] Pressure/reference-pressure configuration
- [ ] Production solver dispatch
- [ ] Compressible boundary conditions
- [ ] Compressible energy coupling
- [ ] Density/Mach/absolute-pressure export
- [ ] Production low-Mach example
- [ ] CLI verification
- [ ] GUI verification
- [ ] End-to-end regression

## P10-APP-004 — Production Physics Compatibility Matrix

- [ ] Define supported physics combinations
- [ ] Reject unsupported combinations cleanly
- [ ] Document compatibility matrix
- [ ] Production-dispatch tests
- [ ] Representative integrated validation cases

**P10 Acceptance:** species, multiphase, and compressible capabilities must
be configurable and runnable through the production application without
custom test-only code paths, with end-to-end regression and
numerical/physical validation.

---

# P11 — GUI Case Authoring

**Goal:** allow ordinary users to create and configure production cases
without manually editing JSON. Current GUI can open, save, validate, run and
post-process cases, but configuration still depends on hand-edited case
files.

## P11-GUI-001 — Mesh Editor

- [ ] Mesh dimensions, domain dimensions, mesh preview, input validation

## P11-GUI-002 — Physics Editor

- [ ] Flow-regime selection, material properties, thermal configuration,
  turbulence selection, species/multiphase/compressible controls

## P11-GUI-003 — Boundary-Condition Editor

- [ ] Boundary/BC-type selection; edit velocity, pressure, temperature,
  species, volume fraction; validation/error reporting

## P11-GUI-004 — Solver Settings Editor

- [ ] SIMPLE/PISO configuration, linear-solver selection, tolerances,
  relaxation factors, time-step/CFL controls, CPU/OpenMP/GPU backend
  selection

## P11-GUI-005 — Case Creation Wizard

- [ ] New case without manual JSON editing, template selection, guided
  setup, pre-save validation, CLI-compatible output

**P11 Acceptance:** a representative supported case can be created,
configured, validated, saved, reopened, and executed entirely through the
GUI without manual JSON editing, while remaining CLI-compatible.

---

# P12 — Solver & Physics Expansion

Begin after production integration is stable. These are planned capability
directions, not current commitments.

## P12-NUM — Numerics

- [ ] Higher-order convection schemes
- [ ] Additional preconditioners
- [ ] Multigrid
- [ ] Fully coupled pressure-based solver

## P12-COMP — Compressible CFD

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
