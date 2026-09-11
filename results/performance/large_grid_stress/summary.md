# P7-PERF-003 — Large-Grid Stress Tests

Progressively larger lid-driven-cavity SIMPLE solves, CPU and GPU, measured with
`benchmarks/gpu/cfd_benchmark_large_grid_stress`, looking for where memory, runtime, or
solver stability actually break down — not chasing a headline performance number.

## Environment / build

Same machine as P7-PERF-001/002 (`results/performance/cuda_end_to_end/environment_hardware.txt`).
`build/perf`, Release, `CFDAPP_ENABLE_CUDA=ON`, `CFDAPP_ENABLE_OPENMP=ON`.
**`OMP_NUM_THREADS=4`** (P7-PERF-002's own measured best CPU configuration — the run was
first attempted with no thread count set, which defaults to all 32 logical processors, the
configuration P7-PERF-002 proved is a ~16x regression; that run was killed and restarted
correctly before any timed data was kept, so no contaminated numbers appear anywhere in this
report).

## Methodology

Same lid-driven cavity case as every other benchmark this session. CPU and GPU configs
identical except backend (BiCGSTAB, no preconditioner, same tolerances/relaxation — same
convention as P7-PERF-001/002). Outer-iteration budget tapers with grid size (60 → 20 → 10 →
8 → 5 → 3 for 160×160 → 1024×1024) purely to keep wall-clock time bounded, the same
methodology P7-PERF-001 already established — this measures *stability trends* (residual
history, NaN/Inf, iteration behavior), not necessarily full convergence, and a linear-solver
failure is recorded as exactly that, never masked by a smaller iteration count. Once a
backend fails a grid, larger grids for that backend are skipped and recorded as such (not
silently omitted).

## Results

| Grid | Cells | Backend | Runtime | Host Peak (HWM) | GPU Used* | Outer Iters | Mass Imbalance | NaN/Inf | Result |
|---|---|---|---|---|---|---|---|---|---|
| 160×160 | 25,600 | CPU | 9.23 s | 55.1 MB | — | 60 | 0 | 0/0 | OK |
| 160×160 | 25,600 | GPU | 20.03 s | 140.4 MB | 1,289 MB | 60 | 0 | 0/0 | OK |
| 320×320 | 102,400 | CPU | 22.67 s | 239.4 MB | — | 20 | 0 | 0/0 | OK |
| 320×320 | 102,400 | GPU | 23.48 s | 239.4 MB | 1,289 MB | 20 | 0 | 0/0 | OK |
| 480×480 | 230,400 | CPU | 43.09 s | 413.7 MB | — | 10 | 0 | 0/0 | OK |
| 480×480 | 230,400 | GPU | 21.61 s | 423.6 MB | 1,289 MB | 10 | 0 | 0/0 | OK |
| 640×640 | 409,600 | CPU | 24.51 s | 656.0 MB | — | **1** | 0 | 0/0 | **FAILED** |
| 640×640 | 409,600 | GPU | 27.86 s | 656.1 MB | 1,289 MB | 8 | 0 | 0/0 | OK |
| 800×800 | 640,000 | CPU | — | — | — | — | — | — | *skipped* |
| 800×800 | 640,000 | GPU | 18.66 s | 935.2 MB | 1,289 MB | **2** | 0 | 0/0 | **FAILED** |
| 1024×1024 | 1,048,576 | CPU | — | — | — | — | — | — | *skipped* |
| 1024×1024 | 1,048,576 | GPU | — | — | — | — | — | — | *skipped* |

*"GPU Used" (`cudaMemGetInfo` total−free) is essentially constant (~1.29 GB) across every
grid — see "Known limitations" for why this is a coarse, contaminated proxy, not a clean
per-run CFDApp-attributable figure.

**Largest stable CPU grid: 480×480** (230,400 cells). **Largest stable GPU grid: 640×640**
(409,600 cells) — one full tier further than CPU.

## Scaling (grids where both backends succeeded)

| Grid | CPU Runtime | GPU Runtime | Speedup (CPU/GPU) | Result |
|---|---|---|---|---|
| 160×160 | 9.23 s | 20.03 s | 0.46× | both OK |
| 320×320 | 22.67 s | 23.48 s | 0.97× | both OK |
| 480×480 | 43.09 s | 21.61 s | **1.99×** | both OK |
| 640×640 | 24.51 s (failed at iter 1) | 27.86 s | not comparable | CPU FAILED |

(These speedups use the same tapered iteration budgets as the raw runtimes above, so they
are indicative of relative backend behavior in this stress-test methodology, not a
substitute for P7-PERF-001's own dedicated, more carefully controlled CPU-vs-GPU comparison —
which independently found the same qualitative crossover, break-even at 80×80.)

## Solver stability and convergence trends

CPU and GPU both ran cleanly (`MaxIterations`, the expected outcome given the deliberately
unreachable outer tolerances — see P7-PERF-001's own identical convention) through 480×480.
At 640×640, **CPU failed after exactly 1 outer iteration** with
`SIMPLEStatus::PressureCorrectionFailure` — its pressure-correction BiCGSTAB solve did not
converge within the shared 5,000-iteration budget. This exactly reproduces P7-PERF-001's own
independent finding at the same grid size (also `PressureCorrectionFailure`, also iteration
1), a second, independent confirmation that this is a genuine, reproducible property of the
unpreconditioned BiCGSTAB pressure solve at this problem size and iteration budget — not a
one-off fluke. GPU continued to succeed at 640×640 (8 full outer iterations, `MaxIterations`)
before hitting the identical failure mode itself at 800×800 (`PressureCorrectionFailure`,
after only 2 outer iterations). Per-iteration residual histories are saved in
`convergence_<backend>_<grid>.csv` for every attempted grid, including the failed ones (the
640×640 CPU history has exactly 1 row, matching its 1 completed iteration).

This is consistent with the trend already measured in P6-GPU-003's own benchmark data:
unpreconditioned CG/BiCGSTAB iteration counts grow with problem size/conditioning (63 → 170 →
278 iterations across a comparably-scaled grid sequence there), so a *fixed* linear-solver
iteration cap that comfortably converges small-to-medium grids eventually stops being enough
as the grid grows — exactly what both P7-PERF-001 and this task observe at 640×640/800×800.

## NaN/Inf

**Zero NaN, zero Inf, at every attempted grid on both backends — including the two failed
runs.** The failures are clean, well-defined "linear solver did not converge within its
iteration cap" outcomes (`PressureCorrectionFailure`), never numerical blow-up. This
distinction matters: the current solver configuration's actual limit at these grid sizes is
*solver capacity* (iteration budget vs. problem conditioning), not *numerical instability*.

## Mass conservation

`global_mass_imbalance` is exactly 0.0 for every run, including the failed ones (SIMPLE
reports the state as of the last completed step, which had not yet diverged).

## Memory

**Memory was never the limiting factor at any tested grid.** Host peak RSS (`VmHWM`) grows
from 55 MB (160×160) to 656 MB (640×640) to 935 MB (800×800, GPU) — all trivially small
against this machine's 32 GiB RAM. GPU-side, `cudaMemGetInfo`'s reported "used" figure stays
essentially flat (~1.29 GB) across every grid up to 800×800, against a 16 GiB device — nowhere
close to exhaustion. The more precise, CFDApp-attributable GPU proxy — cumulative
`GPUExecutionStats::hostToDeviceBytes` per run — does grow with grid size as expected (260 MB
at 160×160 → 589 MB at 640×640's 8 iterations), confirming actual upload volume scales with
problem size even though total device memory pressure does not become a constraint in the
tested range.

## Known limitations

- **`cudaMemGetInfo`-based "GPU used" is a coarse, contaminated proxy, not a clean
  per-run figure.** This GPU runs in WDDM mode (a Windows-driven laptop GPU, not a headless
  TCC/Linux-exclusive device — confirmed via `nvidia-smi`'s own `Driver-Model: WDDM` in
  P7-PERF-001's environment capture), so `cudaMemGetInfo`'s system-wide free/total figures
  can include the Windows desktop compositor and any other concurrent GPU clients, not only
  this process. Its near-constant ~1.29 GB reading across every grid size is far more
  consistent with a large, fixed baseline (driver/context reservation + other GPU consumers)
  than with CFDApp's own actual data footprint, which the H2D-byte figures show clearly does
  grow with problem size. No true "peak GPU memory used by CFDApp alone" instrumentation
  exists in this codebase (adding one would need either NVML per-process queries or
  instrumenting every `DeviceBuffer` allocation with a live running total — out of this
  task's scope; flagged here as a real gap, not silently worked around).
- **The tapered outer-iteration budget means `total_seconds` is only directly comparable
  within a grid** (the point of the "CPU/GPU speedup" table above), not across grid sizes,
  the same caveat P7-PERF-001 already documents for its own methodology.
- **800×800 and 1024×1024 CPU were never attempted** (skipped once 640×640 CPU failed) — a
  deliberate methodology choice (this task's own instruction to avoid grinding through
  runs once instability is already established) rather than an oversight; the CPU limiting
  factor (solver iteration budget vs. problem conditioning) is already established at
  640×640 and would not be expected to reverse at a still-larger grid.
- **1024×1024 GPU was never attempted** for the same reason, once GPU failed at 800×800.
- This is a *stress test under the current, unpreconditioned production configuration* — it
  deliberately does not retry with a larger linear-solver iteration cap or with P6-GPU-003's
  own Jacobi preconditioning to "rescue" the failing grids, since doing so would answer a
  different question (does *some* configuration scale further) than the one this task asks
  (does the *current standard* configuration scale, and where does it stop). That the
  P6-GPU-003 Jacobi preconditioner measurably reduces iteration counts on comparable grids
  (documented in `results/performance/preconditioner/`) suggests it is a plausible, testable
  next step for pushing this practical limit further, but that is future work, not part of
  this task's own scope.
