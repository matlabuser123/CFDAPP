---
name: cfdapp-understand
description: Understand CFDApp code before editing it - entry points, call graph, data flow, ownership, the production path, and the extra questions numerical and GPU code demand. Includes a verified map of the repository. Use before any non-trivial change.
---

# CFDApp: understand before editing

**Default: NO CODE CHANGES** until the questions below are answered. Reading costs minutes; a fix
aimed at the wrong layer costs a phase.

## Always identify

```text
entry points · call graph · data flow · ownership · state lifetime
configuration path · case/parser path · solver dispatch
tests covering it · existing evidence in results/ · known debt in TODO.md
```

"Existing evidence" is not optional: a `results/<phase>/summary.md` often already answers the
question, and a prior phase may have recorded the exact behaviour as deliberate.

## Repository map

`src/` and `include/cfd/` mirror each other with the same 20 subdirectories: `algebra`, `app`,
`boundary`, `compressible`, `core`, `discretization`, `fields`, `gpu`, `io`, `mesh`, `multiphase`,
`parallel` (empty placeholder), `physics`, `pressure_velocity`, `solver`, `species`, `thermal`,
`turbulence`, `validation`, `viz`.

**The production path** (`CLAUDE.md` §9) — a capability is production capability only if it runs
end to end here:

```text
cases/<name>/{case,mesh,geometry,physics,boundaries,solver}.json
  → src/io/CaseReader.cpp            CaseReader::read()  -> CaseDefinition
      (per-file parsers in src/io/case/*.cpp; structs in include/cfd/io/case/)
  → src/io/CaseBuilder.cpp           CaseBuilder::build() -> SimulationSetup
      (mesh dispatch: buildMultiBlockMesh / buildGradedMesh / Cartesian
       -> src/mesh/MeshGeometry.cpp)
  → src/app/ProjectRunner.cpp        ProjectRunner::run()   ← THE one solver backend
      constructs SIMPLE, then optionally ThermalSolver, SpeciesSolver,
      VolumeFractionSolver, and CompressibleSIMPLE when physics.json sets
      compressible.coupled: true (its status then becomes authoritative)
  → src/pressure_velocity/SIMPLE.cpp the outer iteration loop
  → src/io/ResultExporter.cpp        -> VTKWriter / CSVWriter / JSONWriter
  → apps/cli/main.cpp  (target cfdapp)   and
    apps/gui/ -> SimulationController -> src/app/CaseSession.cpp -> the same ProjectRunner
```

CLI and GUI share one backend. A change to `ProjectRunner` reaches both.

**Solver core** — SIMPLE `src/pressure_velocity/SIMPLE.cpp`; PISO `PISO.cpp` (+ `PisoStep.hpp`),
moving-mesh `AlePISO.cpp`, driven by `src/solver/TransientSolver.cpp`; compressible
`src/compressible/CompressibleSIMPLE.cpp`. Linear solvers `src/algebra/{CG,BiCGSTAB,GMRES}.cpp`;
`makeLinearSolver` in `src/algebra/LinearSolverFactory.cpp`; `makeLinearSolverWithFallback` in
`src/algebra/LinearSolverFallback.cpp`. Settings: `LinearSolverSettings` in
`include/cfd/algebra/LinearSolver.hpp`, `SIMPLESettings` in
`include/cfd/pressure_velocity/SIMPLESettings.hpp`.

**Numerics** — gradients `src/discretization/Gradient.cpp` (`GradientScheme::{GreenGauss,
LeastSquares}`) and `VectorGradient.cpp`; convection `Convection.cpp` (`Upwind`, `Central`,
`LinearUpwind`, `QUICK`); diffusion `Diffusion.cpp` + `NonOrthogonalDiffusion.cpp`; face
interpolation `Interpolation.cpp`; Rhie–Chow `src/pressure_velocity/RhieChow.cpp`; boundary
conditions `src/boundary/`; mesh geometry and quality `src/mesh/{MeshGeometry,MeshQuality,
MeshGrading,MeshMotion}.cpp`.

**GPU** — see `cfdapp-cuda`. Headers `include/cfd/gpu/`, CPU stubs `src/gpu/`, real
implementations `cuda/kernels/` (library `cfdcuda`). Backend selection is
`src/algebra/LinearSolverFactory.cpp` — `settings.backend == LinearSolverBackend::GPU` builds
`makeGpuCG`/`makeGpuBiCGSTAB`; when the GPU is unavailable it increments
`gpuExecutionStats().gpuBackendFallbacks` and falls through to the CPU solver. Counters live in
`include/cfd/gpu/GPUExecutionStats.hpp`.

**Tests** — `tests/unit/` (157 files, 18 areas), `tests/integration/` (52), `tests/solver/` (37),
shared helpers `tests/support/`, fixtures `tests/data/cases/`. One gtest executable per area named
`CFD<Area>Tests` (43). Each is registered by **hand-written `add_executable(...)` +
`target_link_libraries(...)` + `gtest_discover_tests(...)` in that directory's own
`tests/**/CMakeLists.txt`** — copy a neighbouring `CMakeLists.txt` when adding a test. The helper
`cfdapp_add_test()` in `cmake/Testing.cmake` is **defined but has zero callers**, and does not link
`GTest::gtest_main`; do not use it.

`gtest_discover_tests` tags exactly one CTest label per target:
`unit | numerical | solver | integration | validation`. The label is the **tier**, not the subject —
`CFDGpuTests` is labelled `numerical`, so `ctest -L gpu` matches nothing; select it with
`-R CFDGpuTests`.

Binaries land at `build/<preset>/tests/<tier>/<area>/<Target>`, e.g.
`build/release/tests/unit/gpu/CFDGpuTests`. Heavy validation cases are `DISABLED_` and need
`--gtest_also_run_disabled_tests`.

**Benchmarks and numerical instruments** — `benchmarks/cpu/` and `benchmarks/gpu/` build targets
`cfd_benchmark_*`; `benchmarks/gpu/benchmark_cuda_end_to_end.cpp` produced **every GPU performance
number in `results/`**, and `benchmark_large_grid_stress.cpp` the stress ladder. Measured output
lands in `results/performance/`. For order studies and MMS, see `cfdapp-numerics` §2a — the
`include/cfd/validation/` shelf and `tests/unit/discretization/{DistortedMesh,ManufacturedFields}.hpp`
already exist; do not rebuild them.

**Build** — presets `debug`, `release`, `asan` → `build/<preset>`. Targets: `cfdcore`
(`cfdapp::core`), `cfdcuda` (`cfdapp::cuda`), `cfdapp` (CLI), `cfdapp_gui`. Options default **OFF**:
`CFDAPP_BUILD_GUI`, `CFDAPP_ENABLE_OPENMP`, `CFDAPP_ENABLE_MPI`, `CFDAPP_ENABLE_CUDA`,
`CFDAPP_ENABLE_SANITIZERS` (`CFDAPP_BUILD_BENCHMARKS` is ON).

**No preset enables CUDA or the GUI.** GPU code exists only in a separately configured tree
(`build/cuda`, Release + `-DCFDAPP_ENABLE_CUDA=ON`) — see `cfdapp-cuda`. Check
`CMakeCache.txt` before concluding anything from a GPU or GUI test result.

**Docs** — `docs/user_guide/` and `docs/validation/` have content; `docs/api/`,
`docs/architecture/`, `docs/numerical_methods/` are **empty**. There is **no `ARCHITECTURE.md`** and
no `PROJECT_STRUCTURE.md` despite comments referencing one. `schemas/` holds only a README —
case validation is native C++ in `src/io/case/`. `tools/` subdirectories are empty placeholders.

## For numerical code, additionally

```text
governing equation · discrete equation · sign convention · units
boundary treatment (and its order) · conservation requirements · expected order of accuracy
```

If these cannot be stated from the code, that is the finding. Hand off to `cfdapp-numerics`.

## For GPU code, additionally

```text
CPU reference path · GPU path · H2D transfers · D2H transfers · synchronizations
kernel launches · fallbacks · device ownership · residency lifetime
```

Establish **which path actually executed** before drawing any conclusion. Hand off to `cfdapp-cuda`.

## Trust source over checkboxes

This repository has had documentation drift. `TODO.md` and `ROADMAP.md` are summaries, not truth —
audit the source and tests before concluding work is missing or done (`CLAUDE.md` §2).
