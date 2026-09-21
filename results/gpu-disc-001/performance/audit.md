# GPU-DISC-001Q — Phase A: performance-instrumentation audit

What already exists, what is trustworthy, and the one thing that had to be added.

## 1. Existing instrumentation

### `cfd::gpu::gpuExecutionStats()` — `include/cfd/gpu/GPUExecutionStats.hpp`

A single process-wide counter block, incremented by `DeviceBuffer`, every kernel launch site and
the GPU Krylov solvers. Compiled unconditionally into `cfdcore`, so it is readable in a CPU-only
build (where every counter stays zero). `resetGpuExecutionStats()` zeroes it, so a measurement
window gives exact deltas.

| quantity the brief asks for | counter | trustworthy? |
| --- | --- | --- |
| H2D transfers | `hostToDeviceCalls` | yes — incremented inside `DeviceBuffer::uploadFrom` |
| H2D bytes | `hostToDeviceBytes` | yes |
| D2H transfers | `deviceToHostCalls` | yes |
| D2H bytes | `deviceToHostBytes` | yes |
| allocations | `allocations` | yes — every `cudaMalloc` |
| **re**allocations | `reallocations` | yes — the subset that GREW an already-allocated buffer, which is exactly the "iteration 7 unexpectedly grew a buffer" signal |
| frees | `frees` | yes |
| kernel count | `kernelLaunches` | yes — 32 launch sites, all recorded |
| synchronizations | `synchronizations` | yes — explicit `cudaDeviceSynchronize` only |
| linear-solve wall time | `gpuSolveSeconds` | yes — wall time inside `GpuCG`/`GpuBiCGSTAB::solve()` |
| upload / download time | `uploadSeconds` / `downloadSeconds` | yes — host-side timing around the synchronous copies |
| linear solver calls / iterations | `gpuLinearSolves`, `gpuLinearSolverIterations` | yes |
| reduction round trips | `reductionGroups`, `reductionQuantities` | yes |
| backend fallbacks | `gpuBackendFallbacks` | yes |

**One counter carries a documented caveat and it matters here.** `kernelSeconds` used to mean
device execution time, because every kernel synchronized. GPU-PIPE-001 Phase 3 removed those
synchronizations for the elementwise ops, SpMV and the preconditioner apply, so for those kernels
it now measures **launch** time and the device work completes asynchronously afterwards. Only the
reduction path still synchronizes. The header says so itself. **`kernelSeconds` is therefore not
used in this gate as a measure of GPU work**; `gpuSolveSeconds` and end-to-end wall time are.

### `SIMPLEResult` — `include/cfd/pressure_velocity/SIMPLEResult.hpp`

Carries `iterations`, `momentumLinearIterations`, `pressureLinearIterations`, the four residual
histories, `globalMassImbalance`, `status`, and (from 001M) `gpuDiscretization` plus its fallback
reason. Enough for "same numerical problem, same work" checks and for per-iteration normalisation.

### `benchmarks/gpu/benchmark_cuda_end_to_end.cpp` — the established production benchmark

The tool that produced the GPU-PIPE-001 numbers this gate must compare against. It times a complete
`SIMPLE::solve()` on a lid-driven cavity and varies **only** `LinearSolverSettings::backend`
between its two arms. Its methodology is reused verbatim (§3 below) so old and new are comparable.

### What does NOT exist

**There is no per-stage timing anywhere in `SIMPLE::solve()`.** Nothing measures momentum assembly
separately from the momentum solve, or the pressure-correction assembly separately from the
pressure solve. The brief's stage-timing breakdown — the section whose whole purpose is to show
whether the 75.5% CPU discretization bottleneck was removed or merely moved — cannot be produced
from the existing counters.

That is the one gap, and the only instrumentation this gate adds.

## 2. What was added, and why it is minimal

`SIMPLEStageTimings` in `SIMPLEResult.hpp`, accumulated by `cfd::Timer` calls around the eleven
stages of the outer loop in `SIMPLE.cpp`.

```text
setup                       before the outer loop: mesh-derived fields, plans, initial flux
momentumAssembly            assembleRelaxedMomentumComponent x {U,V,W}  (or the device facade)
momentumSolve               momentumSolver->solve() x {U,V,W}
responseCoefficients        computeMomentumResponseCoefficient x {U,V,W}
predictedFaceFlux           rhieChowMassFlux / calculateMassFlux
pressureAssembly            assemblePressureCorrection (all non-orthogonal passes)
pressureSolve               pressureSolver->solve() (all passes)
velocityCorrection          correctVelocity
faceFluxCorrection          correctFaceMassFlux
bookkeeping                 residuals, continuity, convergence tests, history push
```

Three properties keep this honest:

1. **No synchronization was added.** Not one `cudaDeviceSynchronize`. The brief forbids
   instrumentation that materially changes production behaviour, and a sync inside the outer loop
   would change exactly the thing GPU-PIPE-001 Phase 3 worked to remove.
2. **The cost is negligible and bounded.** `cfd::Timer` is `std::chrono::steady_clock`; two reads
   per stage, ten stages, ~20-30 ns each is ~0.5 us per outer iteration. Against the *fastest*
   measured iteration in this gate (20², ~1.4 ms) that is under 0.04%.
3. **It changes no numerics.** The timers write only to `SIMPLEResult`; no branch, no field, no
   ordering depends on them. Proven, not asserted: all 15 GPU-DISC differential gates and the full
   1998-test regression are re-run after the change (`summary.md` §2).

### The consequence that must be stated, not buried

Because no synchronization was added, **on the GPU arm these stage timers measure host-side issue
time, not device execution time**, for every stage whose kernels run asynchronously — which is
every discretization stage. The kernels complete later, and their cost surfaces at the next
synchronizing point, which is the linear solve's reduction.

So the GPU stage breakdown is read as:

* `momentumSolve` + `pressureSolve` — **real wall time**, because `GpuBiCGSTAB` synchronizes on
  every reduction and ends in a blocking D2H;
* every discretization stage — **launch/issue time**, a lower bound on device cost;
* the *sum over an outer iteration* — correct, because the iteration ends at a synchronizing point.

Device-side attribution of the asynchronous stages therefore comes from a profiler, not from these
timers, and Nsight Systems is used for exactly that (`profiling/`). Reporting host-side issue time
as if it were device time would be the easiest wrong claim to make in this gate.

## 3. Benchmark methodology, inherited rather than invented

Taken from `benchmarks/gpu/benchmark_cuda_end_to_end.cpp` so this gate's numbers are directly
comparable with `results/gpu-pipe-001/benchmarks/data/runs.csv`:

```text
case                 2D lid-driven cavity, MovingWall lid + Wall walls, FixedGradient pressure
grids                20² 40² 80² 160² 320² 640²
outer budgets        200 200 200  60   20    8      (identical on every arm at a given grid)
velocity relaxation  0.7        pressure relaxation 0.3
momentum solver      BiCGSTAB, 1000 it, abs 1e-8, rel 1e-6, no preconditioner
pressure solver      BiCGSTAB, 5000 it, abs 1e-7, rel 1e-5, no preconditioner
outer tolerances     1e-10 on velocity/pressure/continuity -- deliberately unreachable, so every
                     run executes its FULL budget and the arms do identical work
cold start           one untimed GPU run before any measurement, reported separately
```

The unreachable outer tolerance is the load-bearing methodological choice, inherited from
`benchmark_runner.cpp`: it removes "the GPU converged in fewer iterations" as a confound. Both arms
do the same number of outer iterations, the same number of assemblies and the same number of
corrections. Linear-solver *iteration* counts can still differ, and are reported.

### The arms

| arm | `momentumSolver.backend` / `pressureSolver.backend` | `enableGpuDiscretization` | what it is |
| --- | --- | --- | --- |
| `cpu` | CPU | false | the reference |
| `gpu-pipe` | GPU | false | the **old** path: CPU assembly and corrections, GPU linear solves |
| `gpu-disc` | GPU | true | the **new** path: GPU discretization and GPU linear solves |
| `disc-only` | CPU | true | GPU discretization, CPU linear solves — isolates the discretization change |

`gpu-pipe` is re-measured **in the same session** as `gpu-disc` rather than read from the recorded
CSV. The old `compare.py` documents ~10% cross-session timing variance; running both arms back to
back removes it, and the recorded CSV is then corroboration rather than the authority.

`disc-only` exists because without it, "the new path is faster" cannot be attributed: a change in
end-to-end time between `gpu-pipe` and `gpu-disc` could come from the discretization moving to the
device, or from that change altering what the linear solver is handed.

## 4. CPU configuration — and the fairness question

**An earlier draft of this section was wrong, and measuring is what caught it.** It said the CPU
reference is "parallel where it matters most", reasoning from
`src/CMakeLists.txt:380` (`target_link_libraries(cfdcore PUBLIC OpenMP::OpenMP_CXX)`) and the one
`#pragma omp parallel for` at `SparseMatrix.cpp:91`. Then the thread sweep came back flat:

```text
OMP_NUM_THREADS   320x320 cavity, CPU arm, median of 3
  1               28.720 s
  8               28.614 s
  32              28.988 s
```

No effect whatsoever. The reason is not Amdahl's law — it is that **OpenMP is not compiled in**:

```text
grep -c fopenmp build/cuda/build.ninja   ->  0
CFDAPP_ENABLE_OPENMP                      ->  OFF (the default; cmake/OpenMP.cmake is
                                               included only when it is ON)
```

`_OPENMP` is therefore undefined, the `#ifdef _OPENMP` around that pragma excludes it, and the SpMV
runs its plain serial loop. **The CPU baseline in every number in this gate is single-threaded.**

### Is that an unfair baseline?

Stated plainly so a reader can judge rather than be reassured:

* It is the project's **default production configuration** — `CFDAPP_ENABLE_OPENMP` is OFF, and
  `cmake/OpenMP.cmake` still says "No CFDApp target links against OpenMP yet". Nothing was turned
  off for this gate.
* It is the **same configuration the GPU-PIPE-001 baseline used**, so the old-vs-new comparison —
  the one this gate is really about — is unaffected either way. `gpu-pipe` and `gpu-disc` are both
  GPU arms measured against the same CPU build.
* But a 10× speed-up quoted against a single-threaded CPU on a 32-thread machine is a number a
  reader would rightly discount, and the brief explicitly forbids an artificially weak CPU
  baseline. So an OpenMP-enabled CPU build is measured as well and reported alongside
  (`scaling/openmp/`), and the headline table names which CPU it is compared against.

The SpMV loop is row-parallel with each row's inner sum accumulated serially in ascending column
order, so it is **bit-identical at any thread count** (`test_sparse_matrix.cpp` asserts this).
Thread count therefore changes speed and not numbers, and an OpenMP build is a legitimate
additional data point rather than a different numerical problem.

## 5. Environment

`environment.txt`, captured alongside the runs.

```text
CPU      Intel Core i9-14900HX, 16 cores / 32 threads, 36 MiB L3
RAM      31 GiB visible to WSL2 (64 GiB host, no .wslconfig)
GPU      NVIDIA RTX 5000 Ada Generation Laptop, 15352 MiB, compute 8.9
         driver 580.97, persistence mode ENABLED, max SM clock 3105 MHz
CUDA     12.9.86
compiler g++ 11.4.0 (Ubuntu 22.04.5, WSL2, kernel 6.6.87.2)
build    Release, -O3 -DNDEBUG, CFDAPP_ENABLE_CUDA=ON
arch     compute_80/sm_80 and compute_89/sm_89 (native), both with PTX
CUDA nvcc flags  -fmad=false on the 12 discretization kernels (unchanged)
```

No `-march`/`-mtune`: the CPU build targets baseline x86-64, which is why CPU arithmetic does not
contract `a*b+c` and why `-fmad=false` is required on the device side for bitwise equality.

**Persistence mode is enabled**, so driver re-initialisation is not a per-run cost.

## 6. Known limits carried into this gate

* **GPU BiCGSTAB restart asymmetry** (TODO.md technical debt). Not fixed, not in scope. The
  benchmark's fixed-budget configuration does not reach the reproducer (2D cavity 40×40 at outer
  tolerance 1e-6 for 3000 iterations); every benchmark run is checked for `ran_cleanly` and status
  regardless.
* **`nvcc` is not bit-reproducible on this toolchain** (GPU-DISC-001P). Library hashes are not used
  as evidence of identity here; the 31-source hash set is.
* **WSL `date +%s` can jump.** Timings come from `cfd::Timer` (`std::chrono::steady_clock`) inside
  the process, never from shell wall-clock arithmetic.
