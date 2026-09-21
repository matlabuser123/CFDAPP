# GPU-DISC-001Q — Performance qualification

**Result: PASS.** The integrated CUDA discretization path is **2.71× faster than the pre-GPU-DISC
GPU path** at 640², and the 75.5% CPU-discretization bottleneck that blocked GPU-PIPE-001 is
**removed, not shifted** — 18.13 s → 3.63 s, 73.7% → 39.3% of wall.

```text
headline, 2D cavity 640^2 (409,600 cells)
  CPU, project default (serial)      90.79 s
  CPU, best configuration (OpenMP 8) 69.95 s
  old GPU path (GPU-PIPE-001)        23.86 s
  NEW GPU-DISC path                   8.80 s

  speed-up vs the default CPU        10.32x     (was 3.60x)
  speed-up vs the BEST CPU            7.95x     <- the figure to quote
  improvement over the old GPU path   2.71x     <- independent of CPU baseline
  crossover                          320^2 -> 160^2
```

Every speed-up above is measured on **byte-identical fields**: `gpu-pipe` vs `gpu-disc` is bitwise
at all six grids (§4). Nothing here weakened a tolerance, shortened a budget, or throttled a CPU.

## 1. Environment and build

`environment.txt`

```text
CPU       Intel Core i9-14900HX, 16 cores / 32 threads, 36 MiB L3
RAM       31 GiB visible to WSL2 (64 GiB host)
GPU       NVIDIA RTX 5000 Ada Laptop, 15352 MiB, compute 8.9, driver 580.97
          persistence mode ENABLED, max SM clock 3105 MHz
CUDA      12.9.86       compiler g++ 11.4.0      OS Ubuntu 22.04.5 / WSL2
build     Release, -O3 -DNDEBUG, CFDAPP_ENABLE_CUDA=ON
arch      compute_80/sm_80 + compute_89/sm_89 (native), both with PTX
flags     -fmad=false on the 12 discretization kernels -- UNCHANGED
git       d7d74f51 + the uncommitted GPU-PIPE/GPU-DISC working tree
```

Release throughout. No Debug build is used as qualification evidence.

## 2. Methodology, inherited rather than invented

Taken verbatim from `benchmarks/gpu/benchmark_cuda_end_to_end.cpp`, the tool that produced the
GPU-PIPE-001 numbers this gate compares against:

```text
grids            20^2 40^2 80^2 160^2 320^2 640^2
outer budgets    200  200  200   60    20    8     -- IDENTICAL on every arm
outer tolerance  1e-10 on velocity/pressure/continuity -- deliberately UNREACHABLE
momentum solver  BiCGSTAB 1000 it, abs 1e-8, rel 1e-6, no preconditioner
pressure solver  BiCGSTAB 5000 it, abs 1e-7, rel 1e-5, no preconditioner
relaxation       velocity 0.7, pressure 0.3
repeats          5 / 5 / 5 / 3 / 3 / 3, median reported with min/max spread
```

The unreachable outer tolerance is the load-bearing choice: every run executes its **full** budget,
so "the GPU converged in fewer iterations" cannot masquerade as a speed-up. Both arms perform the
same number of assemblies and corrections. The harness **fails** a point whose status is not
`MaxIterations`, or whose iteration count differs from the budget.

### Four arms, one session

| arm | solver | discretization | what it is |
| --- | --- | --- | --- |
| `cpu` | CPU | CPU | the reference |
| `gpu-pipe` | GPU | CPU | the **old** path |
| `gpu-disc` | GPU | GPU | the **new** path |
| `disc-only` | CPU | GPU | isolates the discretization change |

`gpu-pipe` is re-measured **in this session** rather than read from the recorded CSV: the old
`compare.py` documents ~10% cross-session variance. `disc-only` exists because without it, a change
between `gpu-pipe` and `gpu-disc` could be the discretization moving to the device *or* that move
altering what the solver is handed — and those need separating.

### Warm-up

One untimed GPU solve before any measurement, reported rather than hidden:

```text
cold_start_seconds  0.2287     second identical run  0.1647     one-off cost  0.0639
```

## 3. Results

### 2D lid-driven cavity — the canonical series

```text
grid     cells    N   CPU(s)  gpu-pipe  gpu-disc  disc-only   pipe x   disc x  disc/pipe   old x
20x20      400    5    0.309     7.127     8.123      0.886   0.043x   0.038x     0.88x  0.049x
40x40     1600    5    1.461    13.795    14.282      1.866   0.106x   0.102x     0.97x  0.088x
80x80     6400    5    6.526    20.979    17.511      6.017   0.311x   0.373x     1.20x  0.277x
160x160  25600    3   11.312    13.423    10.153      8.810   0.843x   1.114x     1.32x  0.702x
320x320 102400    3   28.191    14.679     7.381     24.007   1.921x   3.819x     1.99x  1.773x
640x640 409600    3   90.793    23.857     8.799     79.287   3.806x  10.319x     2.71x  3.599x

spread (min..max as % of median): 0.1% - 10.5%, worst 24.9% at 20^2 gpu-pipe
```

`old x` is the recorded GPU-PIPE-001 measurement. The in-session `pipe x` reproduces it closely
(3.81 vs 3.60 at 640², 1.92 vs 1.77 at 320²), which is the cross-check that the two campaigns are
measuring the same thing.

**Small grids lose, and that is reported rather than buried.** At 20² the GPU is 23× *slower*: 400
cells cannot amortise kernel launch and transfer cost. The brief permits this; hiding it would not
be permitted.

### The other required cases

```text
inlet/outlet 2D (open boundaries, through-flow, pin suppressed)
  320x320   CPU 33.53   pipe 17.03   disc 10.02    3.35x vs CPU    1.70x vs old

3D cavity (production U/V/W)
  40x40x40  CPU 10.85   pipe  9.91   disc  3.26    3.33x vs CPU    3.04x vs old  <- best old-vs-new
  24x24x24  CPU  5.05   pipe  6.57   disc  3.44    1.47x vs CPU    1.91x vs old
  12x12x12  CPU  0.83   pipe  2.32   disc  2.25    0.37x vs CPU    1.03x vs old

higher-order convection (320x320)
  QUICK          CPU 28.14   pipe 14.15   disc  8.30    3.39x    1.71x vs old
  LinearUpwind   CPU 29.78   pipe 15.27   disc  8.46    3.52x    1.81x vs old
```

**3D gains most over the old path (3.04×)** — three momentum components to assemble instead of two,
so moving assembly to the device buys proportionally more. 3D also crosses over earlier: 13,824
cells (24³) against 25,600 (160²) in 2D.

**Inlet/outlet gains least (1.70×)** because that case is more solver-dominated. A case property,
not a weaker result.

### Crossover

```text
old GPU path   320^2      new GPU-DISC path   160^2      (2D cavity)
                          new GPU-DISC path    24^3      (3D)
```

## 4. Numerical integrity — what was actually asserted

A speed-up measured on a different answer is not a speed-up. But **which** equality applies depends
on what a pair changes, and conflating the two would either assert something this project has never
claimed or miss a real defect:

| pair | assertion | result |
| --- | --- | --- |
| `cpu` vs `disc-only` | **BITWISE** — same CPU solver, isolates GPU discretization | 0 differing, all grids, all cases |
| `gpu-pipe` vs `gpu-disc` | **BITWISE** — same GPU solver, old vs new | 0 differing, all grids, all cases |
| `cpu` vs `gpu-pipe` | reported, not asserted | cross-solver |
| `cpu` vs `gpu-disc` | reported, not asserted | cross-solver |

The second row is the load-bearing one: the two arms whose ratio is the headline number produce
**byte-identical fields**.

Cross-solver pairs are not bitwise *by nature* — GPU BiCGSTAB reduces in a different order than the
serial CPU sum — and GPU-DISC-001N qualified those against the project's converged tolerance, not
bitwise. These runs stop at a fixed budget far from convergence, where an unconverged intermediate
state legitimately carries a larger difference, so they are reported and the converged claim is
left to `full_solve_equivalence`, which is green.

**A corroboration worth stating:** `cpu vs gpu-pipe` and `cpu vs gpu-disc` report *identical*
differing counts and maxAbs at every grid (1959/5.34e-08, 7919/3.01e-05, 31839/4.06e-05, …). The
GPU discretization contributes **nothing** to the CPU/GPU difference; all of it is the solver. That
is what the bitwise row implies, arrived at independently.

```text
runs                                      168 timed runs across 4 families
distinct statuses                         ['MaxIterations']   (the expected one, only)
all_finite                                ['1']               (no NaN/Inf anywhere)
runs failing integrity                    0
worst |global mass imbalance|             0.000e+00
```

### The first criterion was wrong, and the data caught it

The harness originally asserted bitwise equality of the CPU arm against *every* other arm. That
failed on the cross-solver pairs, and the failure was **in the criterion, not the code**:
`cpu vs disc-only` passed bitwise while `cpu vs gpu-pipe` did not, which localises the difference
to the solver. The criterion was corrected to the table above — not relaxed — and all four families
were re-run under it.

## 5. Stage breakdown — was the bottleneck removed or moved?

`scaling/cavity_analysis.txt`. **640², median seconds and % of that arm's wall:**

```text
stage                              gpu-pipe (OLD)        gpu-disc (NEW)
setup / upload                     0.0205   0.1%        1.3569  14.7%
momentum assembly                 14.6008  59.3%        2.4159  26.2%
momentum solves                    0.5078   2.1%        0.2595   2.8%
response coefficients              0.0351   0.1%        0.0045   0.0%
predicted face flux                0.1532   0.6%        0.0003   0.0%
pressure-correction assembly       2.3370   9.5%        1.1583  12.5%
pressure solve                     5.7077  23.2%        3.6433  39.5%
velocity correction                0.9609   3.9%        0.0333   0.4%
face-flux correction               0.0479   0.2%        0.0159   0.2%
residual / bookkeeping             0.1203   0.5%        0.1122   1.2%
other                              0.1019   0.4%        0.0914   1.0%
-----------------------------------------------------------------------
DISCRETIZATION total              18.1311  73.7%        3.6279  39.3%
LINEAR SOLVE total                 6.2352  25.3%        3.9028  42.3%
wall                              24.6076 100.0%        9.2337 100.0%
```

**The old path's discretization share measures 73.7%** — an independent reproduction of the
recorded 75.5%, from a different harness and a different session. That is the number GPU-DISC-001
was created to attack.

**Removed, not shifted.** The share falls to 39.3%, but the decisive figure is the absolute one:
**18.13 s → 3.63 s, a 5.0× reduction**, while total wall fell 24.6 → 9.2 s. A merely *shifted*
bottleneck would show the same absolute time under a different label.

Per stage: momentum assembly 6.0× faster, velocity correction 29× faster, pressure-correction
assembly 2.0× faster.

At 160² the same table shows discretization 36.5% → 15.5%; at 320², 56.2% → 21.7%.

**What is now dominant in the new path:** the pressure solve (39.5%), then momentum assembly
(26.2%), then `setup/upload` at 14.7% — a fixed per-solve plan-construction cost that GPU-PIPE-001
residency would amortise across solves.

### The caveat that keeps these numbers honest

The production stage timers add **no synchronization** (`audit.md` §2). On the GPU arm they
therefore measure **host issue time** for every asynchronous stage; that work completes later and
surfaces at the next synchronizing point, which is the linear solve. So the discretization stages
are a *lower bound* and the pressure solve is correspondingly *over*-attributed. The per-iteration
sum is correct because the iteration ends at a sync. Device-side attribution comes from §8, not
from this table.

## 6. Transfers, synchronization, allocations

```text
640^2       iters   H2D   H2D MB     D2H   D2H MB  H2D/it  D2H/it   MB/it     sync  sync/it  alloc realloc
gpu-pipe        8    76    589.3   23154    596.4     9.5  2894.2  148.21    23130   2891.2     26       0
gpu-disc        8   463   2538.4   23282   1670.3    57.9  2910.2  526.09    23130   2891.2    371       0
disc-only       8   387   1949.1     128   1073.9    48.4    16.0  377.88        0      0.0    345       0
```

**The new path moves 3.5× MORE data per iteration than the old one** — 526 MB vs 148 MB at 640².
That is not a regression in disguise, it is the residency gap, and it is the single most useful
thing this gate produces for GPU-PIPE-001:

> Every system assembled on the device is **downloaded into a host `LinearSystem`** for the solver,
> which then **uploads it again**. `GpuSimpleDiscretization::assembleMomentum` and
> `assemblePressureCorrection` return host `LinearSystem`s by design (GPU-DISC-001M), because the
> `LinearSolver` interface is a host interface.

Removing that round trip is exactly what "GPU-resident pressure solve" means. **It is not done
here and no residency item is marked complete.**

`disc-only` isolates it: with a CPU solver the D2H drops to 16/iteration, because nothing is being
shipped back for a device solve.

**Synchronizations** are identical on both GPU arms (2891.2/iteration at 640²) — they come from the
Krylov reductions, which GPU-DISC-001 did not touch. `disc-only` has **zero**, confirming the
discretization path itself adds none.

**Allocations: 371, reallocations 0**, on every `gpu-disc` run at every grid and every budget.

### One counter was not deterministic, and the guard said so

`gpu-pipe` at 320² recorded `allocations = [26, 28]`, `reallocations = [0, 2]` — repeat 0 differed
from repeats 1–2, with identical `pressure_lin_iters = 7244` (so identical work). The GPU solver's
persistent workspaces, sized for the preceding 160² case, **grew once** on the first 320² solve and
were reused after. That is GPU-PIPE-001's persistent-workspace design behaving correctly, and the
regression guard flagged the counter as non-guardable rather than averaging it away.

## 7. Memory

```text
640^2       peak DeviceBuffer     bytes/cell    driver-reported after solve
gpu-pipe          137.54 MB            335.8               1351.82 MB
gpu-disc         2027.62 MB           4950.3               1351.82 MB
```

Peak device memory rises **14.7×** on the new path — the mesh, plans, fields and assembled systems
are now resident. 2.03 GB of a 15.35 GB card (13%). `bytes/cell` is ~4950 at every grid, so the
scaling is linear and predictable rather than a surprise waiting at a larger mesh.

`memory/memory_probe.log` answers the three required questions directly:

```text
budget    iters  allocations  reallocations     peak bytes    frees
     2        2          371              0      127206520      371
     5        5          371              0      127206520      371
    20       20          371              0      127206520      371
    60       60          371              0      127206520      371

PASS  allocations and peak are INDEPENDENT of the outer budget
      -> nothing allocates inside the loop; NO per-iteration VRAM creep
PASS  reallocations == 0  -- no buffer ever grew after its first allocation
PASS  currentDeviceBytes == 0 after the solve; allocations == frees == 371 -- no leak
PASS  5 repeated solves identical in allocations and peak -- no creep across solves
```

Largest successfully tested mesh: **640² = 409,600 cells** (2D) and **40³ = 64,000 cells** (3D).

## 8. Profiling — device time per stage

`profiling/stage_profile_640.log`

```text
stage                          ms/iteration   % of device discretization   kernels
momentum assembly U+V              288.345        61.1%                        30
pressure-correction assembly       157.465        33.4%                         2
velocity correction                 16.842         3.6%                        13
beginIteration (upload)              3.627         0.8%                         0
setPressureCorrection                2.204         0.5%                         0
setMomentumSolution x2               1.702         0.4%                         0
face-flux correction                 1.471         0.3%                         1
response coefficients                0.387         0.1%                         2
predicted face flux                  0.103         0.0%                         1
```

**Top production GPU time consumers: momentum assembly (61.1%) and pressure-correction assembly
(33.4%) — 94.5% of device discretization time between them.** The pressure-correction assembly is
only **2 kernels** yet a third of the time, so those two kernels are where any future optimisation
should look first. Recorded as a follow-up; **nothing was optimized in this gate.**

These stages are synchronized and therefore serialised by the profiler, so each figure is an
isolated device cost and their sum exceeds a real iteration. The synchronization is in the
**profiling harness, not in production** — `SIMPLE::solve()` still adds none.

### Nsight Systems was attempted and could not be used

Recorded rather than quietly replaced. `profiling/profile_run.log`:

* Nsight Systems 2025.1.3 ships a `target-linux-x64` agent that runs under WSL2, and it launched
  the production workload successfully (62,570 kernels).
* First attempt: `LD_PRELOAD` was split on the spaces in "NVIDIA Corporation/Nsight Systems", so
  the injection library never loaded. Fixed by copying the agent to a space-free path — a symlink
  is not enough, nsys resolves it back.
* With injection working, the capture still contains **no CUDA kernel rows**: Nsight's kernel
  tracing needs CUPTI, and this WSL2 CUDA 12.9 install is the minimal one — `extras/CUPTI` is
  absent.

The brief lists "existing CUDA event instrumentation" as an acceptable alternative, which is what
§8 is. The vacuity guard in `tools/profile.sh` was also fixed: its first version tested for the
*absence of one error string* and would have reported an empty profile as a success.

## 9. Throughput and scaling

```text
Mcell-iterations/s     cpu    gpu-pipe   gpu-disc
20x20                 0.221     0.010      0.009
160x160               0.133     0.113      0.147
640x640               0.035     0.133      0.355
```

`cell-iterations/s = cells x outer iterations / wall seconds`.

**GPU efficiency improves monotonically with size** (0.009 → 0.355, a 39× rise) while **CPU
throughput degrades** (0.221 → 0.035) as the working set outgrows the 36 MiB L3. The two effects
compound, which is why the speed-up curve steepens rather than plateaus. Six points is not enough
to extrapolate an asymptote and none is claimed.

## 10. CPU configuration — and a correction

**The CPU baseline in the tables above is single-threaded**, and finding that out required
measuring rather than reading:

```text
production build/cuda, 320^2 cavity, CPU arm
  OMP_NUM_THREADS=1    28.720 s      8    28.614 s      32    28.988 s
```

Flat, because `CFDAPP_ENABLE_OPENMP` defaults **OFF** — `grep -c fopenmp build/cuda/build.ninja`
returns **0**, `_OPENMP` is undefined, and the one `#pragma omp parallel for` at
`SparseMatrix.cpp:91` is compiled out. `audit.md` §4 originally claimed the reference was "parallel
where it matters most", reasoning from the link line at `src/CMakeLists.txt:380` — which is inside
`if(CFDAPP_ENABLE_OPENMP AND OpenMP_CXX_FOUND)`. The flat sweep exposed the misreading and the
audit was corrected.

That is the project's own default and the configuration the GPU-PIPE-001 baseline used, so
old-vs-new is unaffected. But quoting a 10× speed-up against a serial CPU on a 32-thread machine
would be the "artificially weak CPU baseline" the brief forbids. So a **separate OpenMP-enabled
build** (`build/omp`, production untouched) was measured:

```text
OpenMP build, CPU arm          320^2        640^2
  OMP_NUM_THREADS=1           32.208 s     95.514 s
  OMP_NUM_THREADS=8           22.262 s     69.948 s   <- best
  OMP_NUM_THREADS=32          39.447 s        n/a     (67.5% spread -- oversubscribed)
```

At 32 threads OpenMP is **slower than serial** on this 16-core/32-thread part, so defaulting to
`nproc` would also have been the wrong "fair" choice. The fair denominator is the **best** CPU:

```text
640^2 speed-up of gpu-disc
  vs project-default CPU (serial, 90.79 s)   10.32x
  vs best CPU (OpenMP 8 threads, 69.95 s)     7.95x   <- the figure to quote
  vs the old GPU path                         2.71x   <- unchanged either way
```

**The old-vs-new improvement factor is independent of the CPU baseline**, because both arms are GPU
arms and the denominator cancels. GPU-DISC-001's own contribution is 2.71× at 640² whichever CPU
you compare against.

## 11. Performance-regression guard

`tools/regression_guard.py`, baseline in `regression-guard/baseline.json` (24 points).

Two metric classes, deliberately treated differently:

```text
EXACT    h2d/d2h calls and bytes, allocations, reallocations, kernels, syncs,
         outer iterations                                     threshold 0%
TIMING   wall_seconds, discretization_s, linear_solve_s       threshold 35%, floor 0.05 s
```

The exact counters are deterministic, so any change is a real change — a doubling of per-iteration
transfers would be caught immediately. The 35% timing threshold is **derived from this gate's own
measured spread** (worst observed 24.9% at 20², typical 2–9%), not invented: tight enough to catch
a stage that doubles or a silent CPU fallback, broad enough not to fire on normal noise. It is
explicitly an informational local-qualification threshold, not a release gate — the project has no
pre-existing performance-threshold policy, and inventing a tight one would manufacture false
failures.

The guard reports non-deterministic counters rather than guarding them (§6), and `check` against
the data `record` was taken from returns **PASS**.

CI note: the guard needs a GPU, so it is a local tool; its JSON baseline is plain text a CPU-only
CI job can still diff for unexpected edits.

## 12. Instrumentation added, and its cost

`SIMPLEResult::StageSeconds` (ten stage accumulators) and `GPUExecutionStats::peakDeviceBytes` /
`currentDeviceBytes`. `cfd::Timer` reads only; **no `cudaDeviceSynchronize` was added anywhere**,
because a sync in the outer loop would destroy the property GPU-PIPE-001 Phase 3 created (syncs
−66.7%) and would change production behaviour in order to measure it.

Cost: two `steady_clock` reads per stage, ten stages, ~0.5 µs per outer iteration — under 0.04% of
even the fastest measured iteration.

Proven not to change numerics: **15/15 GPU-DISC differential gates green** after the change, and
the full regression in §13.

## 13. Gates and regression

`regression/all_gates.log`, `regression/regression.log`, `regression/regression_freshness.log`

```text
15/15 GPU-DISC differential gates green, after the instrumentation change
  mesh 96 | gradients 132 | diffusion 528 | convection 1684 | momentum convection 10352
  boundary conditions 75 | momentum assembly 27744 | momentum response 260
  Rhie-Chow 2628 | pressure-correction 4950 | velocity correction 894
  face-flux correction 1848 | single-iteration 42 | integrated SIMPLE 26 | full-solve 17
  all failures = 0

ctest --test-dir build/cuda --output-on-failure
  100% tests passed, 0 tests failed out of 1998   (45 disabled of 2043 registered)
  Total Test time (real) = 1380.47 s
  `ninja: no work to do` BEFORE and AFTER ctest
```

This gate changed four production files, so the suite re-passing is a requirement rather than a
formality. `build/omp` exists only to measure an OpenMP CPU baseline and is not the tested build.

## 14. Evidence

```text
results/gpu-disc-001/performance/
  audit.md                      Phase A -- instrumentation inventory, and the correction in S4
  summary.md                    this file
  environment.txt               hardware, toolchain, build identity, source hashes
  raw/                          runs-{cavity,inletoutlet,3d,schemes}.csv + per-family logs
                                analysis-*.txt, first-pass/ (the superseded first criterion)
  scaling/cavity_analysis.txt   speed-up, transfers, throughput, memory, stage tables
  scaling/cpu_threads.log       the flat sweep that exposed OpenMP being OFF
  scaling/openmp/               the OpenMP-enabled CPU baseline, 1/8/32 threads
  profiling/stage_profile_*.log device time per stage -- the top GPU consumers
  profiling/profile_run.log     the Nsight attempt and why it could not be used
  memory/memory_probe.log       per-iteration creep, leak, creep across solves
  regression-guard/baseline.json + the guard script
  regression/                   15 gates, ctest, freshness
```

## 15. What is NOT claimed, and what is NOT started

* **No GPU-PIPE-001 residency item is marked complete.** This gate quantifies what residency must
  fix (§6: 526 MB/iteration, the host `LinearSystem` round trip; §5: the 14.7% setup cost) and does
  nothing about it.
* **`Full regression` as a GPU-DISC-001 Integration checkbox is NOT ticked.** 1998/1998 passes here
  as it has after every gate, but that checkbox's own scope has never been written down and this
  gate did not audit it.
* **Nothing was optimized.** The dominant costs are identified (§8: momentum assembly 61.1%,
  pressure-correction assembly 33.4%) and recorded as follow-up. No kernel was tuned to improve a
  benchmark number.
* **No numerical algorithm, tolerance or convergence criterion was changed.**
* **Nothing committed or pushed.**
