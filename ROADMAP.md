# CFDApp — Roadmap

## Project Status

**Current state:** P0/P1/P2/P3/P4 complete; P5 in final release-validation stage.

**Current priority:**

```text
Numerical correctness
    ↓
Validation
    ↓
Performance
    ↓
Application
    ↓
Release
    ↓
Production physics integration
```

---

# P0 — Numerical Foundation ✅

* [x] Structured mesh
* [x] Scalar/vector fields
* [x] Boundary conditions
* [x] Sparse linear algebra
* [x] Finite-volume operators
* [x] Incompressible momentum equation
* [x] Continuity equation
* [x] SIMPLE
* [x] Pressure correction
* [x] Canonical mass-flux treatment
* [x] Convergence criteria
* [x] Mass conservation
* [x] Deterministic execution

---

# P1 — Validation & Quality ✅

* [x] Poiseuille validation
* [x] Lid-driven cavity validation
* [x] Grid refinement
* [x] Analytical comparison tooling
* [x] CSV export
* [x] JSON metadata
* [x] VTK export
* [x] Regression suite
* [x] Continuous integration
* [x] ASan / UBSan
* [x] clang-format
* [x] clang-tidy
* [x] Quality gates

---

# P2 — Transient CFD ✅

* [x] Time controller
* [x] Implicit Euler
* [x] CFL monitoring
* [x] `TransientSolver`
* [x] PISO
* [x] Restart capability
* [x] Transient validation
* [x] Deterministic restart behaviour

---

# P2 — Thermal ✅

* [x] Thermal properties
* [x] Energy equation
* [x] Thermal boundary conditions
* [x] Heated-cavity conduction
* [x] Conjugate heat-transfer foundation
* [x] Thermal validation

---

# P2 — Turbulence ✅

* [x] Turbulence-model interface
* [x] Laminar model
* [x] RANS framework
* [x] k-ε
* [x] k-ω
* [x] SST
* [x] Turbulence benchmark validation

---

# P3 — Advanced Physics ✅

## P3-PHYS-001 — Boussinesq Buoyancy ✅

* [x] Thermal-expansion coefficient
* [x] Reference temperature
* [x] Gravity vector
* [x] Boussinesq density variation
* [x] Buoyancy source in momentum
* [x] Temperature → momentum coupling
* [x] Zero-buoyancy equivalence
* [x] Sign/source validation
* [x] Deterministic coupling

## P3-PHYS-002 — Natural Convection ✅

* [x] Natural-convection cavity
* [x] Rayleigh number
* [x] Prandtl number
* [x] Multi-grid validation
* [x] Velocity validation
* [x] Temperature validation
* [x] Nusselt-number validation
* [x] Heat balance
* [x] Mass conservation
* [x] Grid refinement
* [x] Determinism

## P3-PHYS-003 — Variable Properties ✅

* [x] Temperature-dependent viscosity
* [x] Temperature-dependent conductivity
* [x] Temperature-dependent heat capacity
* [x] Temperature-dependent density where appropriate
* [x] Property interpolation
* [x] Validation tests
* [x] Constant-property equivalence

## P3-PHYS-004 — Species Transport ✅

* [x] Species-field infrastructure
* [x] Advection-diffusion equation
* [x] Species boundary conditions
* [x] Diffusivity models
* [x] Conservation checks
* [x] Analytical validation

## P3-PHYS-005 — Multiphase Foundation ✅

* [x] Two-phase representation
* [x] Volume-fraction field
* [x] Mixture properties
* [x] Conservative interface transport
* [x] Conservation tests
* [x] Boundedness monitoring
* [x] Single-phase equivalence
* [x] Minimal validation case

## P3-PHYS-006 — Compressible Foundation ✅

* [x] Compressible fluid properties
* [x] Ideal-gas equation of state
* [x] Density coupling
* [x] Compressible continuity
* [x] Compressible momentum foundation
* [x] Pressure-density coupling
* [x] Energy coupling
* [x] Mach-number diagnostics
* [x] Low-Mach regression
* [x] Compressible validation case

---

# P4 — Performance ✅

## P4-A — Profiling ✅

* [x] Reproducible profiling baseline
* [x] Runtime breakdown
* [x] Hardware/build metadata
* [x] Representative benchmark cases

## P4-B — CPU Optimization ✅

* [x] Matrix-assembly profiling
* [x] Matrix-assembly optimization
* [x] Linear-solver investigation
* [x] Solver-workspace investigation
* [x] Performance regression checks
* [x] Numerical-equivalence checks

## P4-C — Parallel Performance ✅

* [x] OpenMP baseline
* [x] Thread scaling
* [x] Scaling efficiency
* [x] Determinism/equivalence checks

## P4-D — Memory/Layout ✅

* [x] Memory/layout investigation
* [x] Allocation analysis
* [x] Data-layout review
* [x] Geometry/connectivity reuse where appropriate

## P4-E — CUDA ✅

* [x] Optional CUDA backend
* [x] CUDA context/backend foundation
* [x] GPU sparse operations
* [x] GPU linear algebra
* [x] CPU fallback
* [x] CPU/GPU equivalence

## P4-F — Large-Grid Benchmarks ✅

* [x] Multi-grid benchmark suite
* [x] Runtime scaling
* [x] Iteration scaling
* [x] Memory scaling
* [x] CPU/OpenMP/GPU comparison where supported

---

# P5 — Application 🚧

## P5-A — Production Case Manager ✅

* [x] New case
* [x] Open case
* [x] Save
* [x] Save As
* [x] Reload
* [x] Validate
* [x] Run
* [x] Cancel
* [x] Explicit case lifecycle
* [x] CLI/GUI case-format compatibility

## P5-B — GUI Solver Workflow ✅

* [x] Qt/QML application
* [x] Shared production solver backend
* [x] Worker-thread execution
* [x] Responsive GUI
* [x] Run/Stop workflow
* [x] Progress reporting
* [x] Failure handling
* [x] Controller-level tests
* [x] Headless QML smoke test

## P5-C — Field Visualization ✅

* [x] Scalar-field map
* [x] Field selector
* [x] Legend
* [x] Mesh/domain rendering
* [x] Completed-run result loading

## P5-D — Contours ✅

* [x] Marching-squares extraction
* [x] Automatic contour levels
* [x] GUI contour overlay
* [x] Synthetic analytical tests

## P5-E — Vector Plots ✅

* [x] Velocity-vector sampling
* [x] GUI vector glyphs
* [x] Sampling controls
* [x] Display scaling

## P5-F — Residual Monitoring ✅

* [x] Live residual monitoring
* [x] Full multi-series history
* [x] Residual-history plotting
* [x] Reload from `residuals.csv`

## P5-G — Post-Processing ✅

* [x] Field statistics
* [x] Probe tool
* [x] Line sampling
* [x] CSV export
* [x] Derived-field support

## P5-H — ParaView ✅

* [x] VTK output
* [x] Velocity vector export
* [x] Scalar field export
* [x] Automated VTK smoke test
* [x] ParaView user workflow documentation

## P5-I — Documentation ✅

* [x] Getting started
* [x] Installation
* [x] CLI guide
* [x] GUI guide
* [x] Visualization guide
* [x] Case-format documentation
* [x] ParaView guide
* [x] Troubleshooting

## P5-J — Packaging 🚧

* [x] CPack configuration written
* [x] Qt deployment configuration written
* [ ] Build Windows production package
* [ ] Run `windeployqt`
* [ ] Generate portable ZIP
* [ ] Generate Windows installer
* [ ] Smoke-test packaged CLI
* [ ] Smoke-test packaged GUI
* [ ] Verify clean-machine execution
* [ ] Verify CPU-only fallback
* [ ] Generate release checksums

## P5-K — Release Automation 🚧

* [x] GitHub release workflow written
* [ ] Execute workflow on Windows
* [ ] Run full Windows Release test suite
* [ ] Package inside CI
* [ ] Smoke-test packaged binaries
* [ ] Generate checksums
* [ ] Upload release assets
* [ ] Download and retest published artifact

---

# P6 — Production Physics Integration

Begin only after P5 release is complete.

The advanced-physics modules exist, but some are not yet fully available through the production case/configuration/dispatch path.

## P6-APP-001 — Species Production Integration

* [ ] Add/complete `physics.json` species configuration parsing
* [ ] Wire species configuration into `CaseBuilder`
* [ ] Dispatch species transport through `ProjectRunner`
* [ ] Add production example case
* [ ] Verify CLI execution
* [ ] Verify GUI execution
* [ ] Export species fields
* [ ] Add end-to-end regression

## P6-APP-002 — Multiphase Production Integration

* [ ] Add/complete multiphase configuration parsing
* [ ] Build phase definitions from case configuration
* [ ] Wire volume-fraction transport into production runner
* [ ] Expose mixture fields
* [ ] Add production example
* [ ] Verify CLI execution
* [ ] Verify GUI execution
* [ ] Verify restart/export
* [ ] Add end-to-end regression

## P6-APP-003 — Compressible Production Integration

* [ ] Add/complete compressible configuration parsing
* [ ] Add EOS configuration
* [ ] Define pressure/reference-pressure configuration
* [ ] Wire compressible solver dispatch into `ProjectRunner`
* [ ] Wire compressible BCs
* [ ] Wire compressible energy coupling
* [ ] Export density/Mach/absolute pressure
* [ ] Add production low-Mach example
* [ ] Verify CLI execution
* [ ] Verify GUI execution
* [ ] Add end-to-end regression

## P6-APP-004 — Production Physics Matrix

* [ ] Define supported physics combinations
* [ ] Reject unsupported combinations cleanly
* [ ] Document compatibility matrix
* [ ] Add production-dispatch tests
* [ ] Add representative integrated validation cases

---

# P7 — GUI Case Authoring

Current GUI can open, save, validate, run and post-process cases, but ordinary configuration still depends heavily on existing JSON case files.

## P7-GUI-001 — Mesh Editor

* [ ] Mesh size controls
* [ ] Domain dimensions
* [ ] Mesh preview
* [ ] Validation

## P7-GUI-002 — Physics Editor

* [ ] Flow-regime selection
* [ ] Material properties
* [ ] Thermal controls
* [ ] Turbulence selection
* [ ] Species controls
* [ ] Multiphase controls
* [ ] Compressible controls

## P7-GUI-003 — Boundary-Condition Editor

* [ ] Select boundary
* [ ] Select BC type
* [ ] Edit velocity
* [ ] Edit pressure
* [ ] Edit temperature
* [ ] Edit species
* [ ] Edit volume fraction
* [ ] Validation/error reporting

## P7-GUI-004 — Solver Settings Editor

* [ ] SIMPLE/PISO configuration
* [ ] Linear-solver selection
* [ ] Tolerances
* [ ] Relaxation factors
* [ ] Time-step controls
* [ ] CFL controls
* [ ] Backend selection
* [ ] CPU/OpenMP/GPU controls

## P7-GUI-005 — Case Creation Wizard

* [ ] Create new case without manual JSON editing
* [ ] Template selection
* [ ] Guided setup
* [ ] Validate before save
* [ ] CLI-compatible output

---

# P8 — Solver & Physics Expansion

Only after production integration is stable.

Potential future work:

* [ ] Higher-order convection schemes
* [ ] Additional preconditioners
* [ ] Multigrid
* [ ] Fully coupled pressure-based solver
* [ ] Advanced compressible-energy formulation
* [ ] Higher-Mach compressible capability
* [ ] Advanced turbulence validation
* [ ] Additional species models
* [ ] Reaction/source-term framework
* [ ] Advanced multiphase interface methods
* [ ] Surface tension
* [ ] Interface reconstruction
* [ ] Moving/deforming meshes
* [ ] 3D foundation

These are future capabilities, not current commitments.

---

# P9 — Production Maturity

Long-term application hardening.

* [ ] Crash reporting
* [ ] Structured diagnostic logs
* [ ] Result comparison tools
* [ ] Automated benchmark dashboard
* [ ] Backward-compatible case-schema migration
* [ ] Plugin/model extension architecture
* [ ] Cross-platform packaging
* [ ] Linux release
* [ ] Automated installer testing
* [ ] Long-duration stability tests
* [ ] Large-case stress tests
* [ ] Release-candidate qualification process

---

# Current Priority

## NOW — Finish P5 Release Gate

Complete:

```text id="p2mgxx"
P5-J — Packaging
P5-K — Release Automation
```

Immediate requirement:

```text id="jnktmg"
Windows + Qt 6
      ↓
Release build
      ↓
Full CTest
      ↓
windeployqt
      ↓
CPack
      ↓
Packaged CLI smoke test
      ↓
Packaged GUI smoke test
      ↓
Checksums
      ↓
GitHub release workflow
      ↓
Download + retest published artifact
```

Only then mark:

```text id="xehd74"
P5 — Application ✅
```

---

# Next After Release

Begin:

```text id="980bxa"
P6 — Production Physics Integration
```

Priority:

```text id="sd3qn5"
1. Species production dispatch
2. Multiphase production dispatch
3. Compressible production dispatch
4. Supported-physics compatibility matrix
```

After P6:

```text id="21d4yu"
P7 — GUI Case Authoring
```

This will remove the remaining need for ordinary users to edit case JSON manually.

---

# Development Rule

Every new roadmap item must follow:

```text id="imrd7b"
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
Documentation
    ↓
Only then mark complete
```

Never mark a feature complete because code merely exists.

A feature is complete only when it is:

```text id="kqnqu5"
implemented
tested
validated
integrated
documented
and usable through the production application
```
