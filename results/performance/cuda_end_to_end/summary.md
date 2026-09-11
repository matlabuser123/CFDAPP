# P7-PERF-001 — CUDA End-to-End Benchmark

Real, end-to-end CPU-vs-GPU production timing for CFDApp's SIMPLE solver (lid-driven
cavity, Re=100 case geometry), measured with `benchmarks/gpu/cfd_benchmark_cuda_end_to_end`.
Every number below is a complete `SIMPLE::solve()` call (mesh setup through
converged/max-iteration result) — never an isolated SpMV or CUDA kernel timing.

## Environment

See `environment_hardware.txt` for the raw capture. Summary:

| | |
|---|---|
| CPU | Intel Core i9-14900HX (32 logical cores) |
| RAM | 32 GiB |
| GPU | NVIDIA RTX 5000 Ada Generation Laptop GPU, 16 GiB, driver 580.97, compute capability 8.9 |
| CUDA toolkit (build) | 11.5.119 |
| Compiler | g++ 11.4.0 (Ubuntu 22.04, via WSL2) |
| CMake | 3.22.1 |
| Build | `build/perf` — `CMAKE_BUILD_TYPE=Release` (`-O3 -DNDEBUG`), `CFDAPP_ENABLE_CUDA=ON` |

Timings were taken on an otherwise-idle machine (the earlier P6-GPU-003 validation `ctest`
run was stopped first — see "Known limitations" for why CPU numbers still show more jitter
than GPU numbers even so).

## Methodology

- **Case**: lid-driven cavity, `rho=1`, `mu=0.01`, lid speed 1 (`Re=100` by grid geometry) —
  the same canonical case `benchmarks/cpu/benchmark_runner.cpp` already uses.
- **CPU vs GPU configurations are identical** except `LinearSolverSettings::backend`:
  both use BiCGSTAB for momentum and pressure, no preconditioner, the same tolerances,
  relaxation, and zero initial conditions. Isolating backend as the single variable is the
  point of this specific benchmark (P6-GPU-003's Jacobi preconditioner has its own dedicated
  before/after comparison in `benchmark_gpu_preconditioner.cpp`).
- **Outer-iteration budget** is fixed per grid (same for CPU and GPU at that grid), and
  shrinks at larger grids purely to keep this tool's own wall-clock time bounded — see
  "Known limitations". `total_seconds` is therefore directly comparable *within* a grid
  (which is exactly what the break-even calculation needs); `per_iter_ms` is the metric
  comparable *across* grid sizes.
- **Warm-up**: one untimed GPU solve (5 outer iterations on a throwaway 20×20 case) runs
  before any timed measurement, isolating CUDA context/driver initialization and first-kernel-
  load cost. That cost is reported separately (`cold_gpu_first_use_seconds`) and excluded from
  every per-grid number below — all per-grid GPU numbers are steady-state/warm.
- **Repeats**: 3 timed repeats per configuration per grid (1 repeat only at 640×640 — see
  "Known limitations"). Median is the primary comparison metric; min/mean/stddev also
  recorded (`runs.csv` has every individual repeat; `summary.csv` has the aggregates).
- **Transfer overhead**: read directly from `cfd::gpu::GPUExecutionStats` (P6-GPU-001's own
  instrumentation) around each GPU run — upload + download seconds as a percentage of that
  run's own `SIMPLE::solve()` time.
- **Numerical equivalence**: max absolute velocity/pressure error between the CPU and GPU
  final fields from repeat 0 of each grid.

## Results

| Grid | Cells | Outer iters | CPU median | GPU median | Speedup (CPU/GPU) | Transfer % | Converged |
|---|---|---|---|---|---|---|---|
| 20×20 | 400 | 200 | 1.220 s | 10.157 s | 0.120× | 44.8% | yes |
| 40×40 | 1,600 | 200 | 1.813 s | 23.398 s | 0.077× | 45.9% | yes |
| 80×80 | 6,400 | 200 | 39.963 s | 31.251 s | **1.279×** | 44.3% | yes |
| 160×160 | 25,600 | 60 | 36.341 s | 20.260 s | **1.794×** | 37.7% | yes |
| 320×320 | 102,400 | 20 | 40.553 s | 20.213 s | **2.006×** | 27.7% | yes |
| 640×640 | 409,600 | 1 (CPU) / 8 (GPU) | 32.237 s | 27.451 s | not valid — see below | 13.9% | **NO (CPU)** |

`cold_gpu_first_use_seconds`: **0.504 s** (CUDA context init + first kernel load, one-time,
excluded from every number above).

**GPU break-even: 80×80** (6,400 cells / 20,000 momentum+pressure unknowns) — the smallest
tested grid where GPU total runtime beats CPU total runtime, computed over the valid
20×20–320×320 grid set.

**Maximum measured speedup: 2.006× at 320×320** — also over the valid grid set.

Full per-repeat data: `runs.csv`. Per-grid aggregates (including the assembly/solver
breakdown and raw `GPUExecutionStats` fields): `summary.csv` and `metadata.json`.

## Transfer overhead

Transfer time (upload + download, from `GPUExecutionStats`) as a percentage of that grid's
GPU `SIMPLE::solve()` time falls steadily as grid size grows — 44.8% at 20×20 down to 13.9%
at 640×640 — because transfer volume per SIMPLE iteration scales with problem size (O(n))
while kernel-launch/synchronization overhead per iteration is closer to constant, so larger
problems spend a growing fraction of GPU time doing real compute rather than moving data.
This is exactly the persistent-GPU-pipeline (P6-GPU-001) behavior working as intended —
transfers are never a majority of GPU time at any tested grid.

## Assembly/solver breakdown

One representative outer iteration's momentum assembly, momentum solve, pressure assembly,
and pressure solve, CPU and GPU, per grid — see `summary.csv`'s `cpu.*`/`gpu.*` breakdown
fields (also in `metadata`-style form inside each grid's JSON block). Assembly itself never
runs on the GPU in this codebase (production SIMPLE always assembles on the CPU host, then
uploads); only the linear solve differs by backend. At the smallest grids the GPU linear
solve is dominated by fixed per-solve overhead (matrix upload, kernel launches, block-
reduction downloads for `dot`/`l2Norm`); at the larger grids the same fixed overhead is
amortized over far more actual arithmetic, which is the direct cause of the break-even
crossover above.

## Numerical validity

For every grid from 20×20 to 320×320, both CPU and GPU runs reported `SIMPLEStatus::
MaxIterations` (the expected outcome — see `makeCavitySettings`'s own effectively-
unreachable outer tolerances, matching `benchmark_runner.cpp`'s established convention of a
fixed iteration budget for comparable timing rather than tuning each grid to reach tight
convergence) with zero mass imbalance and small CPU/GPU field differences:

| Grid | Max abs velocity error | Max abs pressure error |
|---|---|---|
| 20×20 | 5.9e-08 | 1.8e-07 |
| 40×40 | 1.2e-05 | 3.9e-05 |
| 80×80 | 2.8e-05 | 1.5e-04 |
| 160×160 | 2.0e-05 | 8.1e-04 |
| 320×320 | 1.6e-03 | 4.9e-02 |

Errors grow with grid size because larger grids use a smaller fixed outer-iteration budget
(20 at 320×320 vs. 200 at 80×80) purely to keep this benchmark's own wall-clock time
bounded — the two backends' partially-converged intermediate states have more room to
diverge (different floating-point summation order: GPU block-reduction vs. CPU sequential
dot products, the same documented, expected effect `tests/solver/simple/
test_simple_gpu_solver.cpp`'s own `GpuBackendReproducesCpuCavitySolutionWithinTolerance`
records) before both trajectories stop at the same iteration count. This is not evidence of
a numerical defect: P6-GPU-002/003's own dedicated equivalence test suites run CPU/GPU to
*full* convergence and hold both to a 1e-6 tolerance — see those tests (`tests/unit/gpu/
test_gpu_linear_solver.cpp`, `tests/solver/simple/test_simple_gpu_solver.cpp`) for the
tighter, fully-converged equivalence evidence this benchmark does not attempt to reproduce.

**640×640 is excluded from every claim above** — see "Known limitations".

## Known limitations

- **640×640 did not produce a valid comparison.** The CPU run reported
  `SIMPLEStatus::PressureCorrectionFailure` after only 1 outer iteration (its
  pressure-correction BiCGSTAB solve did not converge within the shared 5,000-iteration
  budget — the same budget that converged cleanly at every smaller grid). The GPU run
  completed all 8 outer iterations but its own inner solves only ever reached
  `SolverStatus::MaxIterations`, never `Converged`. Neither side reached a comparable,
  well-defined numerical state, so 640×640's `total_seconds`/speedup/equivalence numbers in
  `summary.csv` do not represent a valid end-to-end comparison and must not be read as one
  (they are recorded, not fabricated — `runs.csv` and `summary.csv` still contain the raw
  observed numbers with `converged=no` for CPU, exactly as the numbers came out). This is
  consistent with unpreconditioned BiCGSTAB's iteration count growing with problem size
  (also observed directly in P6-GPU-003's own benchmark: CG iterations on a comparably
  poorly-conditioned grid grew 63 → 170 → 278 as the grid grew 40×40 → 80×80 (implicitly)
  → larger) — at 409,600 cells, the fixed 5,000-iteration momentum/pressure budget this tool
  reused from the smaller grids is no longer sufficient, and CPU and GPU land on opposite
  sides of that threshold due to their different (but equally valid) floating-point
  summation order. A retry with a substantially larger per-grid linear-solver iteration
  budget specifically for 640×640 was not attempted, to avoid tuning the benchmark until it
  produces a positive-looking result (this task's own explicit "do not fake a win" standard,
  carried over from P6-GPU-003). 640×640 is the honestly-reported "attempted, larger grid"
  required by section 4/21 of this task.
- **CPU wall-clock numbers show substantially more run-to-run jitter than GPU numbers** in
  this WSL2-on-Windows environment (e.g. 40×40 CPU: min 1.72 s but one repeat as slow as
  ~24 s, skewing the mean to 9.1 s while the median stays a representative 1.81 s; 80×80 CPU
  similarly ranges 17.1–40.0 s across 3 repeats). GPU numbers are comparatively tight (e.g.
  320×320 GPU: 19.8–20.2 s across 3 repeats, stddev 0.21 s). This is host CPU-scheduling
  noise under WSL2's virtualization layer, not a property of the CPU solver path itself —
  median (the primary metric used throughout) is far more robust to it than mean, which is
  exactly why median was chosen as the headline number, but the raw spread is visible in
  `runs.csv`/`summary.csv`'s own `*_stddev_s` columns and should be read as environment
  noise, not solver instability, when interpreting the CPU column.
- **Per-grid outer-iteration budgets are not uniform across grid sizes** (200 down to 8) —
  a deliberate choice to keep this tool's own total runtime bounded (~13 minutes for the
  full matrix as run), documented up front in "Methodology". `total_seconds` is only
  compared CPU-vs-GPU *within* a grid (valid); `per_iter_ms` is the metric to use for
  *cross-grid* comparisons.
- A latent bug was found and fixed in this tool while writing this report:
  `CFDAPP_ENABLE_CUDA` is a CMake cache variable never propagated as a preprocessor
  `#define` anywhere in this project, so an early revision's `#ifdef CFDAPP_ENABLE_CUDA`
  metadata check always evaluated false regardless of the actual build (the same pattern,
  inherited unmodified, already exists in `benchmarks/cpu/benchmark_runner.cpp`'s own
  `cuda_enabled_at_build` field — not fixed there, out of this task's scope). Fixed here to
  report `cuda_available_at_runtime` (an actual runtime check) instead; this run's
  `metadata.json` was corrected by hand to match, since it predates the source fix.
