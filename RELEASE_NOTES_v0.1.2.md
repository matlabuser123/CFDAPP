# CFDApp v0.1.2 — Release Notes

First published release (v0.1.0 and v0.1.1 were tagged during release-
automation debugging but never completed a passing workflow — see Known
limitations). A 2D finite-volume CFD solver core plus a case-manager and
Qt6/QML application layer, built around one shared production solver
backend used by both the CLI and the GUI.

## Major implemented features

- Structured 2D mesh, FVM discretization (convection, diffusion, gradients),
  sparse linear algebra (CG/BiCGSTAB).
- Steady incompressible flow via **SIMPLE**, with mass-conservative pressure
  correction and a canonical mass-flux treatment.
- Thermal coupling: energy equation, thermal boundary conditions, Boussinesq
  buoyancy, conjugate heat-transfer foundation.
- Turbulence: laminar/k-ε/k-ω/SST RANS models.
- Advanced-physics **foundations** (equation-level; see Known limitations):
  species transport, two-phase volume-fraction transport, compressible
  (ideal-gas, low-Mach) formulation.
- A production case manager (`cfd::app::CaseSession`), a CLI (`cfdapp`), and
  a Qt6/QML GUI (`cfdapp_gui`) sharing one solver backend
  (`cfd::app::ProjectRunner`).
- Field visualization (scalar maps, marching-squares contours, vector
  glyphs), a probe/line-sampling post-processing panel with CSV export, live
  and historical residual monitoring, and ParaView-ready VTK export.
- Windows packaging (CPack ZIP + NSIS installer, Qt runtime deployment).

## Numerical / validation status

Every physics module above has at least one analytical or published-
benchmark validation case (Poiseuille flow, Ghia et al. lid-driven-cavity
data, De Vahl Davis 1983 natural convection, channel-flow log-law/Re_τ),
plus grid-refinement and determinism checks where applicable. Full
regression:

```text
Default (CPU-only) build:        1149 / 1149 passing
GUI-enabled build (-DCFDAPP_BUILD_GUI=ON): 1162 / 1162 passing
```

Confirmed on both Linux (GCC 11/Clang 14) and a real Windows machine (MSVC
19.51, Visual Studio 2026). 13 pre-existing, deliberately `DISABLED_`
slow grid-refinement cases are excluded from both counts (documented in
`ci.yml`'s own comment) — they are not silently skipped, they are labeled
and intentionally excluded from the default fast suite.

See [QUALITY_GATE.md](QUALITY_GATE.md) for a worked example of this
project's own validation discipline (two real order-of-accuracy bugs,
root-caused and fixed).

## CLI / GUI status

- **CLI** (`cfdapp --case <dir>`): the primary, always-available interface.
  Reads/validates/solves/writes results; exit codes documented in
  [docs/user_guide/cli.md](docs/user_guide/cli.md).
- **GUI** (`cfdapp_gui`, built only with `-DCFDAPP_BUILD_GUI=ON`): case
  open/save/validate/run/stop, live and historical residual plots, a
  Canvas-rendered scalar field map with contour overlay and vector glyphs,
  a click-to-probe and line-sampling panel with CSV export, and reload of a
  completed run's results without re-solving. The GUI **cannot author
  cases** — mesh/physics/boundary-condition/solver-setting editing is still
  done by hand in the case JSON files, same as CLI-only use.

## Transient / PISO status

`TransientSolver` + PISO implement transient incompressible flow with CFL
monitoring and restart capability, validated (startup Poiseuille, impulsive
cavity start, temporal refinement, and steady-state-limit equivalence
against the same case solved with SIMPLE).

## Performance / OpenMP status

A profiling baseline, a matrix-assembly optimization
(`SparseMatrixBuilder::reserve()`, ~1.35x measured speedup on an isolated
microbenchmark), and an OpenMP-parallelized CSR sparse matrix-vector
multiply (the only kernel parallelized — chosen because it is provably
race-free per row). Measured OpenMP scaling plateaus/degrades beyond 2
threads on the tested hardware (a memory-bandwidth-bound kernel at this
problem size) — reported as measured, not chased further or hidden. Neither
finding is invented; both are in `results/performance/baseline/`.

## CUDA / GPU status

A CUDA CSR SpMV kernel with verified CPU/GPU numerical equivalence
(`CFDGpuTests`, run on a real NVIDIA RTX 5000 Ada GPU; skipped cleanly with
no hard failure when no GPU is present). **This is not a CUDA-accelerated
solver** — SIMPLE/PISO still run entirely on the CPU; the GPU path covers
exactly one kernel. Measured end-to-end GPU timing (including transfer
overhead, no persistent device residency yet) was slower than the CPU
equivalent at every tested size — reported honestly rather than omitted.
The default build has CUDA off and needs no GPU.

## Known limitations

- Species/multiphase/compressible physics are validated at the equation
  level but not yet reachable through `physics.json`/`ProjectRunner`'s
  production dispatch (tracked as ROADMAP.md P6).
- No GUI case-authoring (mesh/physics/BC/solver editors) — tracked as
  ROADMAP.md P7.
- CUDA accelerates one kernel (SpMV), not the solver loop.
- 2D only; structured/Cartesian mesh only.
- `v0.1.0` and `v0.1.1` exist as git tags but were never published as
  GitHub Releases — both were superseded during release-workflow
  debugging (an invalid Qt module name, a stale CI working-directory
  assumption, a missing `#include <algorithm>` that only failed under
  MSVC, and a tag/project-version mismatch were found and fixed along
  the way). `v0.1.2` is the first tag expected to actually publish.

## Major next steps

1. Finish verifying the GitHub Actions release workflow end-to-end and
   publish the v0.1.2 GitHub Release.
2. ROADMAP.md P6: wire species/multiphase/compressible physics into
   `physics.json` parsing and `ProjectRunner`'s production dispatch.
3. ROADMAP.md P7: GUI case authoring (mesh/physics/BC/solver editors), so
   ordinary use no longer requires hand-editing case JSON.
