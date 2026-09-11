# P7-PERF-002 — OpenMP Scaling

Real OpenMP thread-count scaling for CFDApp's CPU solver path, measured at two levels:
`benchmarks/cpu/cfd_benchmark_spmv_scaling` (P4, pre-existing — the one parallelized kernel in
isolation) and the new `benchmarks/cpu/cfd_benchmark_openmp_scaling` (this task — a complete
production `SIMPLE::solve()`, never a kernel-only proxy).

## Environment / CPU topology

Same machine as P7-PERF-001 (`results/performance/cuda_end_to_end/environment_hardware.txt`):
Intel Core i9-14900HX, 32 GiB RAM, WSL2/Ubuntu 22.04, g++ 11.4.0, `build/perf`
(`CMAKE_BUILD_TYPE=Release`, `-O3 -DNDEBUG`, `CFDAPP_ENABLE_OPENMP=ON`, libgomp via
`OpenMP::OpenMP_CXX`).

`lscpu` (inside WSL2): 1 socket, **16 cores/socket, 2 threads/core → 32 logical CPUs**.
`omp_get_num_procs()` agrees: 32. **Caveat**: the i9-14900HX is a real hybrid
Performance/Efficient-core CPU (8 P-cores with hyperthreading + 16 E-cores, natively
8×2+16=32 logical processors) — WSL2's virtualized CPU presentation flattens this to a
uniform "16 cores × 2 threads" view with no visibility into which vCPUs map to P- vs E-cores
or how the Windows/WSL2 scheduler places them. Thread-count interpretation below (e.g. "16 =
physical maximum") reflects WSL2's own reported topology, not necessarily the host's true
silicon layout.

`OMP_DYNAMIC=false`, `OMP_PROC_BIND=true`, `OMP_PLACES=cores` set for both benchmark runs;
`omp_set_dynamic(0)` + `omp_set_num_threads(N)` called explicitly per trial so the requested
count is never silently overridden.

## 1. Audit finding — this is the most important result of the audit step

**Exactly one `#pragma omp` exists anywhere in this codebase**: `SparseMatrix::multiply()`
(`src/algebra/SparseMatrix.cpp`), the CSR SpMV every CG/BiCGSTAB iteration calls. Confirmed by
grepping `src/`, `include/`, `cuda/` for `"pragma omp"` — one match. Everything else in the
production solve path (momentum/pressure assembly, boundary handling, `dot`/`l2Norm`/vector
arithmetic inside CG/BiCGSTAB, residual computation, field updates) is single-threaded
regardless of `OMP_NUM_THREADS`. `cmake/OpenMP.cmake`'s own header comment ("no CFDApp target
links against OpenMP yet... Phase 5") is stale — `src/CMakeLists.txt` does link
`OpenMP::OpenMP_CXX` to `cfdcore` when enabled — but the *substance* of that comment (very
little of the codebase is actually parallelized) is accurate today. This single fact predicts
and explains every number below.

## Methodology

- **Case**: same lid-driven cavity as P7-PERF-001, CPU backend, BiCGSTAB, no preconditioner.
- **Grid**: 160×160 (25,600 cells), 60 outer iterations — identical to P7-PERF-001's own
  160×160 CPU configuration, directly comparable.
- **Repeats**: 3 per thread count; median is the primary metric (min/mean/stddev also
  recorded).
- **Thread counts**: 1, 2, 4, 8 (mandatory) + 16 (WSL2's reported physical-core maximum) + 32
  (logical-thread maximum) — matches `omp_get_num_procs()`, nothing fabricated.
- **Component breakdown**: one representative outer iteration's momentum assembly + solve and
  pressure assembly + solve, measured per thread count (`assembly_s`/`solve_s` columns).
- **Equivalence**: max absolute velocity difference against the 1-thread run, every thread
  count.

## Results

| Threads | Median Runtime | Speedup | Efficiency | Assembly (1 iter) | Solve (1 iter) | Max |Δv| vs 1-thread | Converged |
|---|---|---|---|---|---|---|---|
| 1 | 12.333 s | 1.00× | 100% | 0.0398 s | 0.0066 s | 0 (baseline) | yes |
| 2 | 10.345 s | 1.19× | 60% | 0.0365 s | 0.0043 s | **0** | yes |
| 4 | **9.281 s** | **1.33×** | 33% | 0.0364 s | 0.0162 s | **0** | yes |
| 8 | 9.353 s | 1.32× | 16% | 0.0398 s | 0.0057 s | **0** | yes |
| 16 | 10.056 s | 1.23× | 7.7% | 0.0443 s | 0.0266 s | **0** | yes |
| 32 | **197.379 s** | **0.062×** | 0.20% | 0.0451 s | 0.1761 s | **0** | yes |

**Best-performing thread count: 4** (median 9.28 s, 1.33× speedup). 8 threads is statistically
indistinguishable from 4 (9.35 s). Scaling saturates by 4 threads and **actively regresses**
from 8 threads onward.

**32 threads is a catastrophic regression — 16× slower than 1 thread**, not merely a
diminishing-returns plateau. Every repeat at 32 threads landed within 0.1 s of the others
(stddev 0.135 s on a ~197 s mean) — this is a consistent, reproducible effect, not noise.

## Numerical equivalence / determinism

`max_abs_velocity_diff_from_1_thread` is **exactly 0.0** at every tested thread count.
`SparseMatrix::multiply()`'s own header comment claims bit-identical results at any thread
count (each row's inner sum is still accumulated by a single thread in the same fixed
ascending-column order — no reduction-order dependence across threads); this run confirms
that claim empirically end-to-end, not just for the isolated kernel. Zero race-condition
evidence at any thread count, including 32.

## Component-level scaling and the serial bottleneck

The breakdown numbers explain the headline results directly: one outer iteration's
assembly (~0.036–0.045 s, flat across every thread count — not parallelized, cannot scale)
is comparable to or larger than that iteration's linear solve (~0.004–0.18 s, the *only*
value affected by thread count, and only through its SpMV calls). With assembly (and every
other per-iteration phase: mass flux, residuals, boundary handling, field updates) pinned at
1-thread speed regardless of `OMP_NUM_THREADS`, no thread count can meaningfully accelerate
the 60-iteration total beyond the modest ~1.33× ceiling actually observed at 4 threads —
this *is* Amdahl's law, directly visible in measured data rather than assumed. The
**serial bottleneck is everything except `SparseMatrix::multiply()`** — momentum assembly,
pressure-correction assembly, `dot`/`l2Norm`/`waxpby`-equivalent vector arithmetic inside
CG/BiCGSTAB (`src/algebra/{CG,BiCGSTAB}.cpp` use plain sequential loops, `cfd::algebra::Vector`
has no OpenMP anywhere), mass-flux calculation, and residual evaluation.

## Saturation and the 16/32-thread regression

Scaling saturates by **4 threads**; 8 threads adds no further benefit (median within noise of
4 threads); 16 threads is measurably *worse* than 4 or 8; 32 threads collapses entirely. The
most plausible explanation, consistent with `cfd_benchmark_spmv_scaling`'s own pure-kernel
measurement below, is per-call OpenMP thread-team spawn/teardown overhead under WSL2's
threading layer: a 60-outer-iteration SIMPLE solve calls `SparseMatrix::multiply()` many
thousands of times (every CG/BiCGSTAB iteration, for both momentum components and the
pressure correction, every outer iteration), and each call re-spawns an `N`-thread team. At
low `N` that overhead is negligible next to the useful work; at high `N` — particularly when
the actual per-call workload (25,600 rows) is too small to amortize it — spawn/teardown cost
dominates and compounds across thousands of calls into a multi-minute total. This is a
property of *this specific virtualized environment* (WSL2 on Windows) and *this specific
workload shape* (many small, frequent parallel regions), not a general indictment of OpenMP
or of this codebase's SpMV parallelization strategy.

### Corroborating evidence: pure-kernel microbenchmark

`cfd_benchmark_spmv_scaling` (pre-existing, P4) isolates exactly the one parallel kernel —
repeated `SparseMatrix::multiply()` calls on a 90,000-row matrix, 30 repeats per thread count,
no assembly/solve/SIMPLE overhead at all:

| Threads | Total (30 calls) | Speedup | Efficiency |
|---|---|---|---|
| 1 | 0.0244 s | 1.00× | 100% |
| 2 | 0.0139 s | 1.76× | 88% |
| 4 | 0.0094 s | 2.60× | 65% |
| 8 | **0.0079 s** | **3.09×** | 39% |
| 16 | 0.0203 s | 1.20× | 7.5% |
| 32 | 0.257 s | 0.095× | 0.3% |

Even in complete isolation — no assembly, no SIMPLE loop, just the kernel itself — the same
saturation-then-collapse shape appears (peak at 8 threads here, since this microbenchmark's
matrix is larger per call and the loop has less other overhead diluting the effect; still a
severe regression by 32 threads). This corroborates that the 16/32-thread collapse is a
genuine property of this environment's OpenMP thread-spawn cost, not an artifact specific to
the full-SIMPLE benchmark's own measurement methodology.

## Estimated effective serial fraction (Amdahl, measured)

Using the best observed low-thread-count speedup (4 threads, 1.329×) and solving
`1/S = f + (1-f)/N` for `f`: **f ≈ 0.78** (≈78% effectively serial at this grid/iteration
budget). This is an estimate from measured performance only, consistent with the observed
"assembly ≈ or > solve time per iteration" breakdown — not a claim about the codebase's
theoretical maximum parallelizability, since additional loops (assembly, vector ops) could in
principle also be parallelized in future work. The 8/16/32-thread data points are excluded
from this fit deliberately: Amdahl's law describes diminishing returns from a fixed serial
fraction, not the active *regression* those thread counts show, which is a distinct
(overhead-dominated) effect the model does not capture.

## Recommendation

**Use `OMP_NUM_THREADS=4` (or leave unset — this codebase does not currently set a default,
so the OpenMP runtime's own default applies, typically all logical processors, i.e. 32 on this
machine, the *worst* observed configuration)** for production runs of the current codebase on
a WSL2-class environment. Given a real ~2× ceiling was never reached and 8 threads matched 4
within noise, a simple, defensible production default is 4–8 threads, with 16+ actively
avoided until either (a) enough of the currently-serial per-iteration work is also
parallelized to justify more threads' overhead, or (b) this is re-measured on bare-metal Linux
(no WSL2 virtualization layer) where thread-spawn cost is typically far lower.

## Known limitations

- Single grid/iteration-budget tested (160×160, 60 outer iterations) — chosen to match
  P7-PERF-001's own CPU baseline for direct comparability and to keep this benchmark's own
  wall-clock time bounded (~12.5 minutes as run, dominated by the 32-thread configuration's
  own 197 s). A larger grid was not additionally tested in the interest of time; the
  single-kernel microbenchmark above already covers a different (larger, 90,000-row) matrix
  size and shows the same qualitative shape.
- WSL2 is a virtualized environment; its thread-spawn overhead is very likely higher than
  bare-metal Linux. The *qualitative* finding (saturation by 4–8 threads, given only one
  parallel region) is expected to hold on bare metal too since it follows directly from
  Amdahl's law and this codebase's current parallelization scope; the *specific* 16/32-thread
  collapse magnitude is plausibly WSL2-specific and should not be assumed to reproduce
  identically on bare-metal hardware.
- CPU topology (P-core/E-core split) is not visible through WSL2 — thread-count-to-core
  mapping could not be independently verified beyond what `lscpu`/`omp_get_num_procs()`
  report.
