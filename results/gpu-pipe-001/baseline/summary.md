# GPU-PIPE-001 — Phase 1 baseline

**Instrumentation and measurement only. No production code was changed.**

Tree: `548401aef0d3cbab9552b1964ceac7efd7ad6154` (working tree also holds the local, unpushed
evidence commit `d7d74f5`). Environment: WSL2 Ubuntu 22.04, RTX 5000 Ada Laptop (CC **8.9**,
15352 MiB, idle at 0 MiB / 52 °C before the run), driver 580.97, CUDA **12.9.86**, 32 threads,
31 GiB visible to WSL. Release build, no sanitizers, no competing load.

## 1. Build identity (`00_build_identity.log`)

```text
native cubins   sm_80, sm_89        (no PTX-only fallback)
libcfdcuda.a    2cf4609492e0a791…   — identical to the hash GPU-PCORR-001 recorded
libcfdcore.a    98a80ef7dd2b1642…
benchmark       b647a3b196585eec…
freshness       ninja -n: "no work to do" for all three targets
```

Freshness is taken from **ninja's own dependency graph**. An earlier hand-rolled check compared
each artifact's mtime against the newest file anywhere under `src/` + `include/` and reported a
false `STALE`: `include/cfd/gpu/DeviceVectorOps.hpp` is included only from `cuda/`, so `cfdcore`
does not depend on it. Reimplementing the build graph by hand is how a fail-closed check becomes a
check that cries wolf.

## 2. End-to-end, 20²–640² (`data/runs.csv`, medians of repeats)

| grid | cells | outer its | Krylov its | CPU s | GPU s | speed-up | CPU spread | GPU spread |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 20² | 400 | 200 | 10 516 | 0.292 | 12.401 | **0.024×** | 0.009 | 3.981 |
| 40² | 1 600 | 200 | 19 047 | 1.393 | 25.450 | **0.055×** | 0.059 | 1.957 |
| 80² | 6 400 | 200 | 22 818 | 6.844 | 37.382 | **0.183×** | 0.310 | 1.039 |
| 160² | 25 600 | 60 | 11 838 | 11.874 | 20.696 | **0.574×** | 0.252 | 2.736 |
| 320² | 102 400 | 20 | 7 639 | 28.131 | 21.715 | **1.295×** | 0.071 | 0.348 |
| 640² | 409 600 | 8 | 5 785 | 90.059 | 29.372 | **3.066×** | n/a (1 repeat) | n/a |

Crossover lies between 160² and 320², as GPU-PCORR-001 recorded. All runs `ran_cleanly`.

**Run-to-run variation, disclosed:** GPU-PCORR-001 measured 160² at 0.506× (GPU 22.894 s); this run
gives 0.574× (20.696 s) — a ~10 % swing on the same machine and binary. Speed-up figures at a given
grid should be treated as ±10 %, and before/after comparisons must come from runs of the same
length, not across sessions.

## 3. Transfers (GPU backend, medians)

| grid | H2D calls | H2D MB | D2H calls | D2H MB | total calls | total MB | B / D2H call |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 20² | 1 804 | 13.09 | 73 840 | 3.09 | 75 644 | 16.18 | 42 |
| 40² | 1 804 | 53.14 | 133 476 | 15.12 | 135 280 | 68.26 | 113 |
| 80² | 1 804 | 214.11 | 159 930 | 62.59 | 161 734 | 276.70 | 391 |
| 160² | 544 | 259.57 | 82 869 | 103.02 | 83 413 | 362.59 | 1 243 |
| 320² | 184 | 353.26 | 53 446 | 219.99 | 53 630 | 573.25 | 4 116 |
| 640² | 76 | 589.29 | 40 492 | 596.63 | 40 568 | 1 185.92 | 14 735 |

Normalised (`04_analysis.log`):

| grid | transfers / outer it | D2H / outer it | H2D / outer it | kB / outer it | **D2H / Krylov it** |
| --- | ---: | ---: | ---: | ---: | ---: |
| 20² | 378.2 | 369.2 | 9.02 | 80.9 | **7.02** |
| 40² | 676.4 | 667.4 | 9.02 | 341.3 | **7.01** |
| 80² | 808.7 | 799.6 | 9.02 | 1 383.5 | **7.01** |
| 160² | 1 390.2 | 1 381.2 | 9.07 | 6 043.1 | **7.00** |
| 320² | 2 681.5 | 2 672.3 | 9.20 | 28 662.3 | **7.00** |
| 640² | 5 071.0 | 5 061.5 | 9.50 | 148 240.6 | **7.00** |

## 4. Attribution by purpose

No per-purpose counter exists in this build, so nothing below is read from an instrumented
breakdown. Instead each claim is tied to a ratio that holds across a **1024× range in problem
size**, which makes it falsifiable rather than a guess.

**By call count — the latency view.** `GpuBiCGSTAB` performs exactly seven reductions per
iteration: `dot(rHat,r)`, `dot(rHat,v)`, `l2Norm(v)`, `l2Norm(s)`, `dot(t,t)`, `dot(t,s)`,
`l2Norm(r)`. Predicted `7 × Krylov` versus measured D2H calls:

```text
20²  99.69%    40²  99.89%    80²  99.87%
160² 100.00%   320² 100.05%   640² 100.01%
```

**Convergence/Krylov reductions are ~100 % of all device→host calls.** Uploads are ~3 per linear
solve at every grid (3.01–3.17) — matrix, RHS and initial guess.

**By bytes — the bandwidth view**, which the call view hides. Model taken from source constants
(`kThreadsPerBlock = 256`; `result.solution = x_.downloadToVector()` once per solve), not fitted:

```text
reduction bytes = krylov × 7 × ceil(n/256) × 8
solution bytes  = solves × n × 8
```

| grid | measured MB | reduction MB | solution MB | model MB | model error | reduction % | solution % |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 20² | 3.09 | 1.18 | 1.92 | 3.10 | +0.19 % | 38.0 % | 62.0 % |
| 40² | 15.12 | 7.47 | 7.68 | 15.15 | +0.17 % | 49.3 % | 50.7 % |
| 80² | 62.59 | 31.95 | 30.72 | 62.67 | +0.13 % | 51.0 % | 49.0 % |
| 160² | 103.02 | 66.29 | 36.86 | 103.16 | +0.14 % | 64.3 % | 35.7 % |
| 320² | 219.99 | 171.11 | 49.15 | 220.27 | +0.13 % | 77.7 % | 22.3 % |
| 640² | 596.63 | 518.34 | 78.64 | 596.98 | +0.06 % | 86.8 % | 13.2 % |

The model reproduces measured bytes to within **0.2 % everywhere**, so the split is established,
not asserted.

Taxonomy requested by the phase: **convergence/Krylov** ~100 % of D2H calls and 38–87 % of D2H
bytes; **solution** 13–62 % of D2H bytes but only 0.01–0.8 % of calls; **matrix + RHS** ~100 % of
H2D. `field`, `boundary`, `diagnostics` and a separately-attributed `residual` are **unmeasured** —
there is no per-purpose counter, and none is invented here.

## 5. The synchronisation signature

```text
grid    download s   D2H calls   µs / call   effective MB/s
20²          5.821      73 840        78.8              0.5
40²         11.807     133 476        88.5              1.3
80²         16.091     159 930       100.6              3.9
160²         6.745      82 869        81.4             15.3
320²         5.389      53 446       100.8             40.8
640²         3.380      40 492        83.5            176.5
```

**Cost per call is 79–101 µs while the payload varies 350×** (42 B → 14 735 B). Effective bandwidth
peaks at 176 MB/s against a link capable of thousands. That is the definition of a latency-bound
path: the price is paid per *call*, not per byte. Each reduction launches a kernel, synchronises,
and copies a small buffer back so the host can finish the sum.

## 6. Timing breakdown (medians)

| grid | solve s | gpu_solve s | kernel s | upload s | download s | download % | transfer % |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 20² | 12.401 | 12.199 | 6.105 | 0.049 | 5.821 | 46.9 % | 47.3 % |
| 40² | 25.450 | 24.660 | 12.330 | 0.091 | 11.807 | 46.4 % | 46.8 % |
| 80² | 37.382 | 33.849 | 16.655 | 0.489 | 16.091 | 43.0 % | 44.4 % |
| 160² | 20.696 | 15.413 | 8.166 | 0.213 | 6.745 | 32.6 % | 33.6 % |
| 320² | 21.715 | 13.872 | 8.000 | 0.222 | 5.389 | 24.8 % | 25.8 % |
| 640² | 29.372 | 10.823 | 6.981 | 0.193 | 3.380 | 11.5 % | 12.2 % |

Scopes are kept separate and must not be mixed:

* **CPU end-to-end** — `simple_solve_seconds`, CPU backend.
* **GPU end-to-end** — `simple_solve_seconds`, GPU backend (the only valid basis for a speed-up).
* **GPU linear-solver** — `gpu_solve_seconds`, inside `GpuCG`/`GpuBiCGSTAB::solve()`.
* **kernel-only** — `kernel_seconds`; includes launch and the following synchronise.

The **51.3× SpMV figure is kernel-only at 800² / 640 000 unknowns** (`results/cuda-qual-001/
performance.md`) and appears nowhere in this document as a 160² or end-to-end result.

## 7. Metrics this harness does not record

Reported unavailable rather than estimated:

```text
SpMV time                    no spmv_seconds column (kernel_seconds is all kernels combined)
pressure-correction time     not separated from momentum
SIMPLE-loop time             not separated from simple_solve_seconds
pressure-only iterations     gpu_linear_solver_iterations covers all 3 solves/outer (u, v, p)
allocations / frees          counters exist in GPUExecutionStats, not emitted to CSV
synchronizations             counter exists in GPUExecutionStats, not emitted to CSV
transfer purpose breakdown   no per-purpose counters exist
```

`gpu_linear_solves ÷ outer_iterations = 3.00` exactly at every grid, confirming three solves per
outer iteration (u-momentum, v-momentum, pressure correction) — which is why Krylov iterations here
are **not** "pressure iterations".

## 8. Repeated patterns that look unnecessary — recorded, not removed

1. **Seven separate reduce→sync→download round trips per Krylov iteration.** Several are
   independent and could share one kernel and one transfer: `dot(t,t)` with `dot(t,s)`; `dot(rHat,v)`
   with `l2Norm(v)`. `l2Norm(r)` at the end of an iteration and `dot(rHat,r)` at the start of the
   next read the *same* `r_`.
2. **`l2Norm(v)` is evaluated unconditionally** as an argument to the cancellation test, even though
   that test usually exits on its cheap first comparison.
3. **The solution vector is downloaded once per linear solve** — 3 per outer iteration, 13–62 % of
   D2H bytes — although at 160² and above the next thing done with it is immediately re-used on the
   device.

None of these has been changed. Phase 1 is measurement only.

## 9. Benchmark integrity

The harness writes into `results/performance/cuda_end_to_end/`, which is committed P6/P7 evidence.
All five files were hashed before the run (`01_pre_run_committed_evidence.sha256`), the three the
benchmark rewrote were restored with `git checkout`, and `sha256sum -c` then verified **5/5 OK**.
`git status` for `results/performance/` is clean. New data lives only under
`results/gpu-pipe-001/baseline/data/`.
