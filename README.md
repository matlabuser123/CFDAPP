# CFDApp

A 2D finite-volume computational fluid dynamics solver in C++20 — steady
incompressible SIMPLE and transient PISO, thermal/turbulence/buoyancy
coupling, species/multiphase/compressible physics, CPU/OpenMP/CUDA
execution, and an application layer (CLI + Qt6/QML GUI) built around one
shared production solver backend.

**Status: released, v0.2.0.** Every capability below ships with unit
tests, analytical/literature-benchmark validation, and determinism checks
— see [TODO.md](TODO.md) for exact live status and [ROADMAP.md](ROADMAP.md)
for phase history, and [Known limitations](#known-limitations) below
before relying on this for anything beyond research/learning use.

## Features

- **Incompressible flow**: structured 2D mesh, finite-volume
  discretization, SIMPLE (steady) and PISO (transient), sparse linear
  algebra (CG/BiCGSTAB) with restart support.
- **Thermal**: energy equation, thermal boundary conditions, conjugate
  heat-transfer foundation, Boussinesq buoyancy validated against De Vahl
  Davis (1983).
- **Turbulence**: laminar, k-epsilon, k-omega, and SST RANS models,
  validated against channel-flow log-law/Re_tau benchmarks.
- **Species transport**: advection-diffusion of one or more passive
  species, production-integrated (`physics.json` → CLI/GUI → export).
- **Multiphase**: two-phase volume-fraction transport with mixture
  density/viscosity, production-integrated. Mixture *viscosity* feeds
  momentum; mixture *density* is deliberately not coupled into continuity
  (documented scope limit, not a bug).
- **Compressible flow — two modes** (see
  [Compressible flow status](#compressible-flow-status)):
  - *Default*: a post-hoc, one-way ideal-gas reinterpretation of an
    already-converged incompressible SIMPLE result (density/Mach/mass-flux
    from EOS, never fed back into the flow solve).
  - *Opt-in* (`compressible.coupled: true`, in progress — see
    [ROADMAP.md](ROADMAP.md)): a dedicated `CompressibleSIMPLE` solver
    with density as genuinely iterated state.
- **Production physics compatibility matrix**: every cross-physics
  combination (thermal/turbulence/buoyancy/species/multiphase/compressible)
  is validated at parse time by one authoritative function, reachable
  identically from the CLI, the GUI, and raw JSON — see
  [docs/user_guide/case_format.md](docs/user_guide/case_format.md).
- **GPU performance**: a persistent GPU-resident pipeline (fields/matrices
  stay on-device across SIMPLE iterations), production GPU CG/BiCGSTAB with
  a deterministic CPU fallback, and GPU-resident Jacobi preconditioning —
  verified CPU/GPU numerical equivalence and a measured end-to-end
  speedup, not a kernel-only microbenchmark. See
  [GPU / CUDA status](#gpu--cuda-status).
- **CPU performance**: OpenMP-parallelized sparse matrix-vector multiply,
  with measured thread-count scaling (see [Performance](#performance)).
- **Application layer**: a production case manager
  (`cfd::app::ProjectRunner`) that the CLI (`cfdapp`) and the Qt6/QML GUI
  (`cfdapp_gui`) both call unchanged — no second, GUI-only solve path —
  plus a GUI case-authoring workflow (mesh/physics/boundary/solver
  editors, new-case-from-scratch), field visualization, probes/line
  sampling, residual monitoring, and ParaView-ready VTK export.

## Quick Start

### Requirements

- CMake 3.20+
- A C++20 compiler: GCC, Clang, or MSVC (developed against GCC 11/Clang 14
  on Linux and MSVC 19.44/Visual Studio 2022 on Windows — both a WSL2/Linux
  build and a native Windows MSVC+Qt6 build are verified).
- Nothing else for the CLI and numerical core — GoogleTest and
  nlohmann/json are fetched automatically by CMake.

Optional:

- **GUI** (`cfdapp_gui`): Qt 6.2+ (`Core`, `Gui`, `Qml`, `Quick`,
  `QuickControls2`; developed against 6.5/6.9 on Windows).
- **OpenMP**: `-DCFDAPP_ENABLE_OPENMP=ON` — see
  [Performance](#performance) for measured scaling; the best-performing
  thread count on this project's own environment is moderate, not maximum.
- **CUDA**: `-DCFDAPP_ENABLE_CUDA=ON`, requires the CUDA toolkit — see
  [GPU / CUDA status](#gpu--cuda-status).
- **Windows packaging**: NSIS (installer) — see
  [docs/developer_guide/packaging.md](docs/developer_guide/packaging.md).

### Build

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

With the GUI:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DCFDAPP_BUILD_GUI=ON \
    -DCMAKE_PREFIX_PATH=<path to your Qt6 install>
cmake --build build -j
```

CMake presets are also provided (`debug`, `release`, `asan`):

```bash
cmake --preset release
cmake --build --preset release -j
```

### Run CLI

```bash
cfdapp --case cases/lid_driven_cavity
```

Reads, validates, solves, and writes `results/{metadata.json,residuals.csv,
fields.csv,solution.vtk}` under the case directory. See
[docs/user_guide/cli.md](docs/user_guide/cli.md) for exit codes.

### Run GUI

Launch `cfdapp_gui` (built with `-DCFDAPP_BUILD_GUI=ON`) to open, create,
edit, run, and visualize cases without touching JSON directly — see
[docs/user_guide/gui.md](docs/user_guide/gui.md).

### Tests

```bash
cd build && ctest --output-on-failure
```

See [Testing](#testing) below for current pass counts and verified
environments. Python validation/plotting tooling has its own suite:

```bash
python -m pip install -e "./python[dev]"
python -m pytest python/tests
```

## Case Structure

A case is a directory of small JSON files:

```text
case.json          Case metadata + paths to the files below
geometry.json       Domain shape/dimensions
mesh.json           Mesh type and resolution
physics.json        Fluid model + optional thermal/turbulence/buoyancy/
                    species/multiphase/compressible blocks
boundaries.json     Per-patch boundary conditions
solver.json         Solver type, tolerances, relaxation, linear-solver settings
```

`CaseReader`/`CaseWriter` round-trip a case losslessly; `CaseBuilder`
turns it into typed runtime objects (`Mesh`, `FluidProperties`, boundary
condition sets, ...) that `ProjectRunner` solves — the exact path both the
CLI and the GUI use. Full field/constraint documentation, per-physics
examples, and the physics compatibility matrix are in
[docs/user_guide/case_format.md](docs/user_guide/case_format.md).

## Example Cases

Ready to run as-is with either the CLI or the GUI, under `cases/`:

| Case | Demonstrates |
| --- | --- |
| `lid_driven_cavity` (+ `_40x40`, `_80x80`) | Incompressible SIMPLE, Ghia et al. validation |
| `poiseuille_flow` | Analytical parabolic-profile validation |
| `channel_flow` | Turbulence (k-ε/k-ω/SST) log-law validation |
| `heated_cavity` | Thermal + Boussinesq natural convection |
| `species_diffusion` | Passive species transport |
| `heated_species_diffusion` | Combined thermal + species |
| `multiphase_validation` | Two-phase volume-fraction transport |
| `compressible_validation` | Post-hoc compressible (ideal-gas, low-Mach) |
| `backward_facing_step` | Separated incompressible flow |

## Validation

Every module above has at least one integration test comparing against an
analytical solution or a published benchmark (Poiseuille flow, Ghia et al.
lid-driven cavity, De Vahl Davis natural convection, channel-flow
log-law/Re_tau), plus grid-refinement and determinism checks. See
[TODO.md](TODO.md) for the full per-item breakdown and
[QUALITY_GATE.md](QUALITY_GATE.md) for the quality-gate methodology (with a
worked example: two real order-of-accuracy bugs found and fixed).

## Compressible Flow Status

Two distinct modes, controlled by `physics.json`'s `compressible.coupled`
(default `false`) — see `docs/user_guide/case_format.md` for the full
schema:

- **Post-hoc (default, production-ready)**: incompressible SIMPLE solves
  the flow exactly as it always has; only afterward is an ideal-gas EOS
  used to compute density/Mach number/a compressible mass flux from the
  already-converged result. Never a second, coupled flow solve — density
  never feeds back into momentum/continuity.
- **Coupled (opt-in, in progress)**: a dedicated `CompressibleSIMPLE`
  solver where density is genuinely iterated state, updated from the
  corrected pressure every outer iteration, solving a compressible
  pressure-correction equation. See [ROADMAP.md](ROADMAP.md)'s P12-COMP
  section and [TODO.md](TODO.md) for current status — do not assume this
  mode is complete or validated until those mark it so.

## Performance

Three real, end-to-end (never kernel-only) benchmarks, full
machine-readable data under `results/performance/`:

**GPU vs CPU** (`cuda_end_to_end/`, lid-driven cavity, NVIDIA RTX 5000
Ada): GPU break-even at **80×80** (6,400 cells); up to **2.0x** measured
speedup at 320×320. GPU is slower than CPU below break-even
(launch/sync overhead dominates small problems) — expected, not a defect.

**OpenMP scaling** (`openmp_scaling/`, same case, Intel i9-14900HX, 32
logical CPUs): best measured thread count is **4** (1.33x speedup);
regresses severely beyond 8 threads on this project's WSL2 dev environment
(32 threads measured 16x *slower* than 1 — thread-spawn overhead). Exactly
one loop is OpenMP-parallelized today (`SparseMatrix::multiply`).

**Large-grid stress** (`large_grid_stress/`): largest stable CPU grid
**480×480**; GPU **640×640**. Memory was never the limiting factor
(host peak under 1GB even near 1M cells); the limit at both backends is
unpreconditioned linear-solver iteration budget, not memory or
instability.

See each results directory's own `summary.md` for full methodology and
known limitations of that specific measurement.

## GPU / CUDA status

**Verified**: with `-DCFDAPP_ENABLE_CUDA=ON`, fields and sparse matrices
stay resident on the GPU across outer SIMPLE iterations; production GPU
CG/BiCGSTAB (`LinearSolverSettings::backend = GPU`) with a deterministic,
logged CPU fallback when no usable device is present; a GPU-resident
Jacobi preconditioner with zero per-iteration host round-trips. CPU/GPU
numerical equivalence is verified (bit-identical CSR SpMV; tight tolerance
for the iterative solvers). Exercised end-to-end on a real NVIDIA RTX 5000
Ada GPU under **WSL2/Linux**.

**Not established**: native Windows + CUDA (only WSL2/Linux+CUDA is
verified — do not generalize that evidence to native Windows). GPU covers
only the linear solve; momentum/pressure assembly always runs on the CPU
host. GPU is slower than CPU below the 80×80 break-even grid size — an
inherent, expected property of small-problem launch overhead. The default
build has CUDA off and needs no GPU.

## Testing

```bash
cd build && ctest --output-on-failure
```

See [TODO.md](TODO.md) for the exact current pass count. Confirmed on:

- Linux (WSL2/Ubuntu), CPU-only and with `-DCFDAPP_ENABLE_CUDA=ON` on a
  real NVIDIA GPU, serial and parallel (`ctest -j8`) — no race conditions;
- AddressSanitizer + UndefinedBehaviorSanitizer (`--preset asan`);
- a genuine native Windows build (MSVC 19.44/Visual Studio 2022, Qt 6.9.3,
  `-DCFDAPP_BUILD_GUI=ON`).

**Note on `OMP_NUM_THREADS`**: under a virtualized/shared-CPU environment,
set it explicitly — the unconstrained default (one thread per logical
processor) was measured to make some tests take 20-30x longer on this
project's own WSL2 environment due to thread-spawn overhead, not slow
tests.

## Known Limitations

- **Compressible coupling is still post-hoc by default.** See
  [Compressible flow status](#compressible-flow-status) — the genuinely
  coupled solver is opt-in and in progress, not yet the default or fully
  validated.
- **Multiphase density is not coupled into continuity** — only mixture
  viscosity feeds momentum (documented scope limit, `MultiphaseProperties.hpp`).
- **P11-GUI-005 gap**: the case-creation-from-scratch GUI workflow has
  verified backend logic but no recorded human-executed GUI acceptance
  pass yet — see [TODO.md](TODO.md).
- **GPU covers linear-solve only, not assembly.** See
  [GPU / CUDA status](#gpu--cuda-status).
- **Native Windows + CUDA is untested** — only WSL2/Linux+CUDA is verified.
- **OpenMP covers one kernel** (`SparseMatrix::multiply`) — most of a
  production solve is single-threaded regardless of thread count.
- **2D only.** No 3D mesh support.
- Mesh geometry is structured/Cartesian only — no unstructured or
  boundary-fitted meshing.

## Development

- [TODO.md](TODO.md) — current work, blockers, next steps.
- [ROADMAP.md](ROADMAP.md) — phase-level plan and history.
- [CLAUDE.md](CLAUDE.md) — agent working rules (evidence discipline,
  workflow, stop conditions).
- [CONTRIBUTING.md](CONTRIBUTING.md) — how to contribute.
- `results/` — detailed, per-task verification evidence.

## Repository Structure

```text
include/cfd/            Public headers, one subdirectory per physics/infrastructure module
    core/                Types, exceptions, logging, versioning, profiling
    mesh/                Structured 2D mesh, cells, faces, boundary patches
    fields/              Scalar/vector/surface field containers
    algebra/             Sparse matrix, CG, BiCGSTAB, preconditioners
    discretization/      FVM operators (convection, diffusion, gradients)
    boundary/            Boundary-condition classes
    physics/             Momentum/continuity assembly, mass flux, buoyancy
    pressure_velocity/   SIMPLE, PISO, pressure correction, transient momentum
    thermal/             Energy equation, thermal properties/BCs
    turbulence/          RANS models (laminar, k-epsilon, k-omega, SST)
    species/             Species transport
    multiphase/          Volume-fraction transport
    compressible/        Ideal-gas EOS, compressible continuity/momentum/mass-flux
    gpu/                 Persistent-residency CUDA backend (CSR matrix/vector, solvers, preconditioner)
    viz/                 Contour/vector/probe/derived-field algorithms (no Qt)
    app/                 ProjectRunner, CaseSession, VisualizationSnapshot
    io/                  Case reader/writer/builder, CSV/JSON/VTK export
src/                     Implementation, mirroring include/cfd/
cuda/                    CUDA kernels (SpMV, vector ops, GPU solvers, GPU preconditioner)
apps/cli/                The `cfdapp` command-line executable
apps/gui/                The `cfdapp_gui` Qt6/QML executable (-DCFDAPP_BUILD_GUI=ON)
tests/                   unit/, solver/, integration/ — gtest, one CTest label per tier
benchmarks/              Standalone CPU/GPU performance-benchmark executables
cases/                   Example cases, runnable as-is by the CLI and GUI
cmake/                   CMake modules (sanitizers, OpenMP, CUDA, packaging, Qt deploy)
scripts/windows-release/ The verified, reproducible Windows build/package/test recipe
docs/user_guide/         Getting started, installation, CLI, GUI, visualization, ParaView, troubleshooting
docs/developer_guide/    Packaging status and evidence
results/                 Real verification/benchmark/release evidence
python/cfdapp/           Validation/plotting tooling (pytest-tested)
```

## Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md).

## License

[MIT](LICENSE).
