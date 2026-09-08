# CFDApp — Development Roadmap

## Project Goal

Build a professional, modular CFD application in modern C++ with:

* Finite Volume Method discretization
* SIMPLE / PISO / PIMPLE pressure–velocity coupling
* Robust sparse linear solvers
* Validation against analytical and published benchmark solutions
* OpenMP and MPI CPU parallelism
* CUDA GPU acceleration
* Python validation, automation, plotting, optimisation, and ML tooling
* CLI and GUI front ends
* Deterministic, testable, production-quality numerical behavior

---

# Phase 0 — Repository and Build Foundation

Status: 🔵 CURRENT

## Goals

Create a clean buildable development foundation before implementing CFD physics.

## Tasks

* [ ] Finalize root `CMakeLists.txt`
* [ ] Finalize `CMakePresets.json`
* [ ] Configure C++20 or newer
* [ ] Configure Debug and Release builds
* [ ] Add compiler warnings
* [ ] Add sanitizers for development builds
* [ ] Configure CTest
* [ ] Add OpenMP detection
* [ ] Add optional MPI support
* [ ] Add optional CUDA support
* [ ] Configure CLI target
* [ ] Configure GUI target as optional
* [ ] Configure install/package rules
* [ ] Confirm clean WSL build

## Acceptance Criteria

```bash
cmake --preset debug
cmake --build --preset debug
ctest --preset debug --output-on-failure
```

All commands complete successfully.

---

# Phase 1 — Core Infrastructure

Status: ⏳

## Goals

Implement reusable low-level infrastructure required by every CFD component.

## Modules

### Core

* [ ] `Types`
* [ ] `Constants`
* [ ] `Exception`
* [ ] `Logger`
* [ ] `Timer`
* [ ] Basic parallel abstraction

### Required Quality

* [ ] No raw owning pointers
* [ ] RAII throughout
* [ ] Clear ownership rules
* [ ] Const-correct interfaces
* [ ] Unit tests
* [ ] No numerical logic inside UI code

## Completion Gate

Core unit tests pass with no warnings or sanitizer failures.

---

# Phase 2 — Mesh System

Status: ⏳

## Goals

Build the geometric foundation for finite-volume calculations.

## Implement

* [ ] `Cell`
* [ ] `Face`
* [ ] `BoundaryPatch`
* [ ] `Mesh`
* [ ] `MeshGeometry`
* [ ] `MeshQuality`

## Geometry Data

Each cell should support:

* centroid
* volume
* neighboring faces
* neighboring cells

Each face should support:

* owner cell
* neighbor cell
* face center
* face area
* face normal
* boundary patch

## Initial Scope

Start with structured Cartesian 2D meshes.

Do not implement full arbitrary unstructured meshes yet.

## Tests

* [ ] Correct cell count
* [ ] Correct face count
* [ ] Positive cell volumes
* [ ] Correct face normals
* [ ] Correct owner/neighbour topology
* [ ] Boundary patch classification
* [ ] Conservation of geometric face areas

---

# Phase 3 — Field System

Status: ⏳

## Goals

Create safe field containers for CFD variables.

## Implement

* [ ] Generic `Field`
* [ ] `ScalarField`
* [ ] `VectorField`
* [ ] `TensorField`
* [ ] `SurfaceField`

## Primary CFD Variables

Support:

* pressure
* velocity
* density
* viscosity
* temperature
* mass flux
* turbulence quantities

## Tests

* [ ] Correct sizing
* [ ] Access safety
* [ ] Copy/move behavior
* [ ] Arithmetic operations
* [ ] Cell and face field distinction

---

# Phase 4 — Sparse Linear Algebra

Status: ⏳

## Goals

Create the numerical backbone for discretized PDE systems.

## Implement

* [ ] Vector
* [ ] Sparse matrix
* [ ] Linear system
* [ ] Linear solver interface
* [ ] CG solver
* [ ] BiCGSTAB solver
* [ ] Basic preconditioner

## Later

* [ ] Jacobi
* [ ] ILU
* [ ] AMG integration

## Numerical Tests

Test against systems with known solutions.

Required checks:

* [ ] solution accuracy
* [ ] residual reduction
* [ ] maximum iteration behavior
* [ ] zero RHS behavior
* [ ] singular-system detection where possible
* [ ] deterministic solutions

---

# Phase 5 — Boundary Conditions

Status: ⏳

## Goals

Implement reusable finite-volume boundary conditions.

## Implement

* [ ] Base `BoundaryCondition`
* [ ] Fixed value
* [ ] Fixed gradient
* [ ] Wall
* [ ] Moving wall
* [ ] Inlet
* [ ] Outlet
* [ ] Symmetry

## Tests

Use reduced analytical problems wherever possible.

---

# Phase 6 — Finite Volume Operators

Status: ⏳

## Goals

Implement independently testable spatial discretization operators.

## Implement

* [ ] Interpolation
* [ ] Gradient
* [ ] Divergence
* [ ] Laplacian
* [ ] Diffusion
* [ ] Convection

## Initial Schemes

### Diffusion

* central differencing

### Convection

* first-order upwind

## Later Schemes

* second-order upwind
* central differencing
* QUICK
* TVD schemes
* limiter-based schemes

## Numerical Verification

Every operator must be tested against known analytical fields.

Examples:

```text
phi = x
phi = y
phi = x² + y²
phi = sin(x)
```

Check convergence under grid refinement.

---

# Phase 7 — Governing Equations

Status: ⏳

## Goals

Build finite-volume equation assembly independently of solver algorithms.

## Implement

* [ ] Fluid properties
* [ ] Momentum equation
* [ ] Continuity equation
* [ ] Transport properties

## Initial Physics

Support:

* incompressible
* Newtonian
* laminar
* constant density
* constant viscosity
* steady state
* 2D

Avoid adding turbulence or compressibility yet.

---

# Phase 8 — SIMPLE Solver

Status: ⏳

## Goals

Implement the first complete Navier–Stokes solver.

## SIMPLE Algorithm

Each iteration should perform:

1. Apply boundary conditions
2. Assemble X-momentum equation
3. Solve X velocity
4. Assemble Y-momentum equation
5. Solve Y velocity
6. Calculate face fluxes
7. Assemble pressure-correction equation
8. Solve pressure correction
9. Correct pressure
10. Correct velocity
11. Correct mass flux
12. Reapply required boundary conditions
13. Calculate residuals
14. Calculate global mass imbalance
15. Test convergence

## Implement

* [ ] `PressureVelocitySolver`
* [ ] `SIMPLE`
* [ ] Residual calculation
* [ ] Under-relaxation
* [ ] Convergence criteria
* [ ] Finite-value checks
* [ ] Mass conservation validation

---

# Phase 9 — First Production Validation Case

Status: ⏳

## Case

Lid-driven cavity.

## Required Grids

* [ ] 20 × 20
* [ ] 40 × 40
* [ ] 80 × 80

## Validation

Compare against published Ghia cavity data.

Check:

* [ ] centerline U velocity
* [ ] centerline V velocity
* [ ] vortex location
* [ ] residual convergence
* [ ] global mass conservation
* [ ] grid refinement
* [ ] deterministic repetition

## Acceptance

The solver must converge for realistic tolerances without NaN or Inf values.

---

# Phase 10 — Analytical Validation Cases

Status: ⏳

## Poiseuille Flow

Validate:

* velocity profile
* pressure gradient
* mass flow rate

## Additional Cases

* [ ] Couette flow
* [ ] diffusion problem
* [ ] manufactured solution
* [ ] channel flow

These cases should detect errors before more complex CFD features are added.

---

# Phase 11 — Case System and CLI

Status: ⏳

## Goals

Run CFD simulations entirely from case files.

## Implement

Schema-backed:

* `case.json`
* `geometry.json`
* `mesh.json`
* `physics.json`
* `boundaries.json`
* `solver.json`

## CLI

Target command:

```bash
./cfdapp --case cases/lid_driven_cavity
```

## Required Output

* convergence status
* iteration count
* residuals
* mass imbalance
* runtime
* output directory

---

# Phase 12 — Result Export

Status: ⏳

## Implement

* [ ] CSV
* [ ] JSON metadata
* [ ] VTK
* [ ] Restart files

## ParaView Compatibility

VTK output should contain:

* coordinates
* pressure
* velocity
* velocity magnitude
* cell metadata

---

# Phase 13 — Python Tooling

Status: ⏳

Python supports the C++ solver rather than replacing it.

## Validation

* [ ] cavity comparison
* [ ] Poiseuille comparison
* [ ] analytical solutions

## Plotting

* [ ] residual histories
* [ ] velocity profiles
* [ ] pressure fields
* [ ] contour plots

## Automation

* [ ] case runner
* [ ] parameter sweeps
* [ ] benchmark runner

---

# Phase 14 — Transient Solver

Status: ⏳

## Implement

* [ ] Time management
* [ ] transient momentum terms
* [ ] time-step controls
* [ ] CFL calculation
* [ ] restart support

## Time Integration

Start with:

* first-order implicit Euler

Later:

* second-order backward
* Crank–Nicolson

---

# Phase 15 — PISO

Status: ⏳

Implement PISO for transient incompressible CFD.

## Requirements

* [ ] predictor
* [ ] pressure correction
* [ ] multiple correction loops
* [ ] transient regression cases
* [ ] conservation checks

---

# Phase 16 — PIMPLE

Status: ⏳

Combine SIMPLE-style outer loops with PISO corrections.

Only begin once both SIMPLE and PISO have strong validation evidence.

---

# Phase 17 — Thermal Physics

Status: ⏳

## Implement

* [ ] Energy equation
* [ ] Thermal properties
* [ ] Heat-transfer boundary conditions
* [ ] Heated cavity validation
* [ ] Conjugate heat-transfer foundation

---

# Phase 18 — Turbulence

Status: ⏳

Do not begin until laminar CFD is fully validated.

## Models

### Foundation

* [ ] turbulence model interface
* [ ] laminar model

### RANS

* [ ] k-epsilon
* [ ] k-omega
* [ ] SST k-omega

## Validation Cases

* turbulent channel flow
* backward-facing step
* benchmark aerodynamic cases

---

# Phase 19 — OpenMP Optimisation

Status: ⏳

## Parallelize

Only measured hotspots.

Likely candidates:

* matrix assembly
* field operations
* flux calculations
* residual calculations

## Rule

No optimisation without profiling evidence.

## Required Checks

* [ ] serial result unchanged
* [ ] deterministic behavior where required
* [ ] benchmark speedup measured

---

# Phase 20 — MPI Distributed CFD

Status: ⏳

## Implement

* [ ] domain decomposition
* [ ] local/global cell indexing
* [ ] processor boundaries
* [ ] halo exchange
* [ ] distributed residual reductions
* [ ] distributed convergence logic

## Validation

Compare:

```text
1 MPI rank
2 MPI ranks
4 MPI ranks
```

Solutions must agree within defined numerical tolerances.

---

# Phase 21 — CUDA GPU Backend

Status: ⏳

Do not port the whole application directly to CUDA.

Start with measured hotspots.

## Initial GPU Work

* [ ] device arrays
* [ ] vector kernels
* [ ] sparse matrix operations
* [ ] residual calculation
* [ ] CG
* [ ] BiCGSTAB

## Later

* flux kernels
* matrix assembly
* pressure correction operations

## Required Evidence

Measure:

```text
CPU serial
CPU OpenMP
GPU CUDA
```

for identical numerical problems.

---

# Phase 22 — Performance Engineering

Status: ⏳

## Benchmark Grids

* 20 × 20
* 40 × 40
* 80 × 80
* 160 × 160
* larger cases when practical

## Measure

* mesh generation
* equation assembly
* linear solve
* flux calculation
* pressure correction
* residual calculation
* I/O
* total runtime

## Tools

Use:

* timers
* profiler
* CPU profiling
* CUDA profiling

Optimisation must never change validated physics silently.

---

# Phase 23 — GUI

Status: ⏳

The GUI comes after the solver API is stable.

## Features

* open case
* edit mesh
* edit fluid properties
* edit BCs
* configure solver
* run simulation
* stop simulation
* display convergence
* view results
* export results

## Architecture Rule

```text
GUI
 ↓
Application Controller
 ↓
CFD Solver API
```

The GUI must never contain solver mathematics.

---

# Phase 24 — Advanced Mesh Support

Status: ⏳

## Expand Beyond Cartesian Meshes

* [ ] nonuniform structured mesh
* [ ] general 2D unstructured mesh
* [ ] triangular cells
* [ ] quadrilateral cells
* [ ] 3D mesh foundation
* [ ] tetrahedral cells
* [ ] hexahedral cells

## Mesh Quality

Track:

* skewness
* orthogonality
* aspect ratio
* minimum volume
* face quality

---

# Phase 25 — Advanced Physics

Status: ⏳

Potential future modules:

* compressible flow
* species transport
* multiphase
* rotating reference frames
* porous media
* buoyancy
* radiation
* reacting flow
* conjugate heat transfer

Each should be introduced as an independent validated capability.

---

# Phase 26 — Optimisation and ML

Status: ⏳

Python-side tooling may support:

* design optimisation
* parameter sweeps
* surrogate modelling
* reduced-order modelling
* ML-assisted turbulence research

ML must remain optional and must not replace baseline validated numerical methods.

---

# Phase 27 — Production Quality

Status: ⏳

## Required

* [ ] full unit-test suite
* [ ] numerical verification tests
* [ ] regression tests
* [ ] deterministic tests
* [ ] conservation tests
* [ ] sanitizers
* [ ] static analysis
* [ ] formatting
* [ ] CI
* [ ] release builds
* [ ] documentation
* [ ] packaging

---

# Phase 28 — Release Milestones

## v0.1 — Numerical Foundation

Target:

* mesh
* fields
* algebra
* FVM operators
* boundary conditions
* SIMPLE
* cavity solver
* Poiseuille validation
* CLI
* CSV/JSON/VTK
* deterministic regression tests

## v0.2 — Transient CFD

Target:

* transient solver
* PISO
* restart
* transient validation

## v0.3 — Thermal

Target:

* energy equation
* heat transfer
* thermal validation

## v0.4 — Turbulence

Target:

* RANS framework
* k-epsilon
* k-omega
* SST
* turbulent validation

## v0.5 — Parallel CPU

Target:

* OpenMP optimisation
* MPI decomposition
* distributed solver validation

## v0.6 — GPU

Target:

* CUDA backend
* GPU linear algebra
* measured CPU/GPU speedup

## v0.7 — GUI

Target:

* Qt-based desktop interface
* case editing
* solver execution
* result visualization

## v1.0 — Professional CFDApp

Required before 1.0:

* validated steady and transient incompressible flow
* laminar and RANS turbulence
* thermal transport
* robust case format
* CLI and GUI
* OpenMP
* MPI
* optional CUDA
* VTK/ParaView export
* deterministic regression suite
* documented validation evidence
* reproducible release builds

---

# Current Priority

The current development path is:

```text
CMake/build system
        ↓
Core types
        ↓
Mesh
        ↓
Fields
        ↓
Sparse algebra
        ↓
Boundary conditions
        ↓
Finite-volume operators
        ↓
Momentum + continuity
        ↓
SIMPLE
        ↓
20×20 cavity
        ↓
Poiseuille validation
        ↓
40×40 / 80×80 validation
        ↓
CLI + export
```

Everything below that line is secondary until the laminar SIMPLE solver is numerically correct.

---

# Immediate Next Work Queue

* [ ] Implement root `CMakeLists.txt`
* [ ] Implement `CMakePresets.json`
* [ ] Make a minimal CLI executable compile
* [ ] Add first CTest test
* [ ] Implement core types
* [ ] Implement Cartesian mesh
* [ ] Implement scalar/vector fields
* [ ] Implement sparse matrix and vector
* [ ] Implement CG
* [ ] Implement BiCGSTAB
* [ ] Verify linear solvers against known systems
* [ ] Implement finite-volume diffusion
* [ ] Implement gradient
* [ ] Implement divergence
* [ ] Implement convection
* [ ] Implement boundary-condition framework
* [ ] Implement momentum equation
* [ ] Implement continuity equation
* [ ] Implement SIMPLE
* [ ] Run lid-driven cavity
* [ ] Validate Poiseuille flow

---

# Development Rule

Do not advance to a new major capability because the previous one merely compiles.

A phase is complete only when it is:

```text
Implemented
    +
Unit tested
    +
Numerically verified
    +
Integrated
    +
Regression protected
    +
Documented
```

The central principle of CFDApp development is:

> Numerical correctness first, architecture second, performance third, advanced features last.
