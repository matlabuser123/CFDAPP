# Baseline performance — `logs/07`, `data/cuda_end_to_end*`

A baseline, not an optimization campaign. Nothing here was acted on.

## Conditions

Release, CUDA 12.9.86, architectures 80;89, OpenMP off, sanitizers off, machine otherwise idle
(1-minute load 0.59 before, 1.00 after). GPU 62 °C before, 56 °C after, 0 MiB in use at both ends.
CPU i9-14900HX. The benchmarks were run from a scratch directory so the committed
`results/performance/**` evidence could not be overwritten; their outputs are copied into `data/`.
Cold GPU first use (context init, first kernel load): 0.575 s, excluded from the per-grid numbers.

## End-to-end production SIMPLE solve, CPU vs GPU backend

Same case, settings, tolerances and per-grid outer-iteration budget; only
`LinearSolverSettings::backend` differs. Median of 3 repeats, 1 repeat at 640².

| grid | cells | CPU median | CPU spread | GPU median | GPU spread | speed-up | transfer % | GPU status |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 20² | 400 | 0.301 s | 0.299–0.301 | 13.63 s | 10.02–13.63 | 0.022× | 44.9 | ok |
| 40² | 1600 | 1.437 s | 1.417–1.437 | 24.47 s | 23.25–24.47 | 0.059× | 43.9 | ok |
| 80² | 6400 | 6.401 s | 6.373–6.401 | 32.75 s | 32.56–32.75 | 0.195× | 40.8 | ok |
| 160² | 25600 | 11.41 s | 11.30–11.43 | 19.83 s | 18.45–21.59 | 0.576× | 33.1 | ok |
| 320² | 102400 | 27.85 s | 27.38–28.11 | — | — | **invalid** | 27.2 | **PressureCorrectionFailure, 0 iterations** |
| 640² | 409600 | 89.85 s | single run | — | — | **invalid** | 15.8 | **PressureCorrectionFailure, 0 iterations** |

GPU-side detail at 160², per repeat: 544 H2D calls / 259 572 800 B, 71 031 D2H calls /
93 544 800 B, 0.216 s upload, 6.33 s download, 7.25 s in kernels, 13.2 s GPU solve, 180 linear
solves, 11 838 linear-solver iterations.

**Findings, recorded not acted on:**

1. **The GPU backend is slower than the CPU at every grid where it completes**, by 1.7× (160²) to
   45× (20²).
2. **Transfer dominates**: 33–45 % of GPU time, and the download count (71 031 calls at 160²) is far
   above the upload count (544) — a per-iteration device→host round trip.
3. **No crossover is demonstrated.** The benchmark prints `gpu_break_even_grid: 320x320`, but that is
   exactly the grid where the GPU run fails, and its "18.8×" and "22.9×" speed-ups compare a
   completed CPU solve against a GPU run that performed **0 outer iterations**. Both are artifacts
   and are dismissed.

## SpMV microbenchmark, 30 repeats — where the cost actually is

| grid | n | CPU | GPU stateless | GPU persistent | stateless speed-up | persistent speed-up |
| --- | --- | --- | --- | --- | --- | --- |
| 20² | 400 | 6.97e-05 s | 9.12e-02 s | 1.12e-03 s | 0.0008× | 0.062× |
| 50² | 2500 | 4.10e-04 s | 1.93e-02 s | 7.60e-04 s | 0.021× | 0.539× |
| 100² | 10000 | 1.80e-03 s | 2.85e-02 s | 8.70e-04 s | 0.063× | 2.07× |
| 200² | 40000 | 7.88e-03 s | 5.42e-02 s | 7.59e-04 s | 0.146× | 10.4× |
| 400² | 160000 | 3.55e-02 s | 1.18e-01 s | 1.66e-03 s | 0.302× | 21.4× |
| 800² | 640000 | 1.57e-01 s | 3.14e-01 s | 3.06e-03 s | 0.500× | **51.3×** |

"Stateless" allocates, uploads, launches and downloads on every call; "persistent" uploads once and
only launches inside the timed loop. The kernel itself is strong — 51× at 640 000 unknowns once the
data is resident — while the stateless path never beats the CPU. Persistent reuse is confirmed by
the project's own stats: the second call reports 0 allocations, 0 transfers, 0 bytes, and kernel
time 2.61e-05 s against 8.63e-05 s for the first.

Memory: the benchmarks reported 0 MiB GPU memory in use before and after, and per-run peak VRAM was
not sampled during execution, so no peak figure is claimed.
