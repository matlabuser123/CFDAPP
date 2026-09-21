# GPU-PIPE-001 — persistent GPU pipeline

**Status: PARTIALLY COMPLETE.** Four of the checklist items are implemented and verified; three
(persistent fields, GPU-resident pressure solve, GPU-resident SIMPLE loop) are **not started**, and
the phase is therefore **not** marked `[x]`.

Baseline `548401aef0d3cbab9552b1964ceac7efd7ad6154`. WSL2 Ubuntu 22.04, RTX 5000 Ada (CC 8.9),
driver 580.97, CUDA 12.9.86, Release, native `sm_80`+`sm_89`.

## What was delivered

| Phase | Result | Evidence |
| --- | --- | --- |
| 1 — instrument and baseline | **PASS** | [`baseline/`](baseline/) |
| 2 — fuse reductions | **PASS** — 7.00 → 5.00 per Krylov iteration, bitwise identical | [`phase2-reductions/`](phase2-reductions/) |
| 3 — remove synchronizations | **PASS** — syncs −66.7 % | [`phase3-sync/`](phase3-sync/) |
| 4 — persistent matrices/workspaces | **PASS** — pre-existing, verified by measurement | [`phase4-persistence/`](phase4-persistence/) |
| 11 — 20²–640² benchmarks | **PASS** — GPU time −15…−53 % at every grid | [`benchmarks/`](benchmarks/) |
| 12 — regression | see [`regression/`](regression/) | |

## Cumulative measured effect

Paired, same session, against HEAD (medians of 3):

| grid | syncs | D2H calls | gpu_solve | end-to-end |
| --- | ---: | ---: | ---: | ---: |
| 160² | 58 480 → 19 478 (**−66.7 %**) | 27 280 → 19 502 (−28.5 %) | 3.208 → 2.098 s (**−34.6 %**) | −28.0 % |
| 320² | 55 201 → 18 389 (**−66.7 %**) | 25 752 → 18 407 (−28.5 %) | 4.768 → 2.739 s (**−42.5 %**) | −28.9 % |
| 640² | 55 165 → 18 381 (**−66.7 %**) | 25 739 → 18 393 (−28.5 %) | 4.806 → 3.352 s (**−30.2 %**) | −9.2 % |

Full ladder against the Phase-1 baseline, with CPU comparison:

| grid | CPU | GPU baseline | GPU now | speed-up before | **speed-up now** |
| --- | ---: | ---: | ---: | ---: | ---: |
| 20² | 0.284 s | 12.401 s | 5.780 s | 0.023× | 0.049× |
| 40² | 1.382 s | 25.450 s | 15.695 s | 0.054× | 0.088× |
| 80² | 6.334 s | 37.382 s | 22.908 s | 0.169× | 0.277× |
| 160² | 10.886 s | 20.696 s | 15.500 s | 0.526× | 0.702× |
| 320² | 26.953 s | 21.715 s | 15.199 s | 1.241× | **1.773×** |
| 640² | 89.304 s | 29.372 s | 24.812 s | 3.040× | **3.599×** |

**Crossover is unchanged: still between 160² and 320².** At 160² the GPU went from 0.526× to
0.702× and is still **slower than the CPU**. Stated plainly rather than presented as a win.

## Correctness

Every change is **bitwise identical**, not merely within tolerance:

* 120/120 bit-pattern checks on the fused reduction, with a proven non-vacuous probe (a
  reassociated reference differs on 3/3 cancelling cases);
* pressure residual identical to 17 significant figures and Krylov iteration counts identical at
  160²/320²/640², paired before/after;
* Krylov iteration counts identical to the Phase-1 baseline across the whole 20²–640² ladder;
* determinism: 5 repeated solves bitwise identical at 160² and 640²;
* compute-sanitizer memcheck/initcheck/synccheck/racecheck — **0 errors each**, after every phase;
* GPU-PCORR-001 regression 6/6 after every phase;
* negative control detects an injected round trip at exactly the predicted magnitude.

No numerical threshold, tolerance, iteration limit or breakdown criterion was changed. `GpuCG` is
untouched.

## Two results that contradicted the plan, kept

**Device-side reduction finalisation was implemented and then removed.** It cut D2H bytes by
81–89 % and was still 25 % *slower* at 640² (5.018 s vs 3.774 s), because the order-preserving
finaliser is O(blocks) serial work while the byte saving buys nothing against a round trip that
costs 79–101 µs regardless of payload. Reverted on its own evidence; the gate item is reported as
implemented-and-reverted, not shipped.

**Phase 4 needed no new code.** Matrix structure/value splitting and workspace persistence already
existed (P6-GPU-001/002). Measurement confirms 28 allocations and 0 reallocations regardless of
solve count or grid size. Reporting that is more useful than reimplementing it.

## Why phases 5–7 were not started

They target H2D traffic and byte volume. The measurements say neither is the bottleneck:

```text
H2D          40–76 calls for an entire run
D2H       18 000–19 500 calls, all of them reductions after Phase 3
per call  79–101 us, independent of payload  (Phase 1 §5)
```

Eliminating the per-solve solution download — Phase 6's main target — removes ~24 calls out of
~19 500. Persistent fields and a GPU-resident SIMPLE loop are large architectural changes to
`SIMPLE.cpp` and the solver interface, with correspondingly large regression risk, for a saving the
data does not support at the current bottleneck.

**The evidence points somewhere else.** After Phase 3, `synchronizations == reductionGroups`
exactly: every remaining host-device wait is one of the 5 reductions per Krylov iteration. The
next real gain is the third fusion Phase 2 identified and did not take — `l2Norm(r)` at the end of
an iteration and `dot(rHat,r)` at the start of the next read the *same* `r_`, so carrying `rho`
across the iteration boundary would give **5 → 4**, a further ~20 % cut in the only remaining
serialization. That is a contained change to `GpuBiCGSTAB`'s loop structure, not an architectural
one.

## Honest limits

* Runtime figures in `benchmarks/` are cross-session and carry Phase 1's measured ~10 % session
  variance; the paired same-session runs are the authority on speed-up.
* The dedicated equivalence campaign over the full case matrix (compressible, multiphase, 3D,
  turbulence) was **not** run. Equivalence is verified at 160²/320²/640² and across the benchmark
  ladder, plus the full SIMPLE/algebra/GPU unit suites.
* `kernelSeconds` changed meaning in Phase 3 (launch time for asynchronous kernels) and is
  documented as such in `GPUExecutionStats.hpp`.
* Error attribution for asynchronous kernels moved to the next synchronizing call. Launch errors
  are still caught immediately; execution errors are still detected, just reported later.

## Files changed

```text
cuda/kernels/DeviceVectorOpsKernel.cu     partialDot2Kernel, grouped reduceToHost, dot2,
                                          2 synchronizations removed
cuda/kernels/GpuLinearSolverCuda.cpp      two fused reduction sites in GpuBiCGSTAB
cuda/kernels/CsrSpmvKernel.cu             synchronization removed
cuda/kernels/GpuPreconditionerKernel.cu   synchronization removed
include/cfd/gpu/DeviceVectorOps.hpp       dot2 declaration
include/cfd/gpu/GPUExecutionStats.hpp     reduction counters; kernelSeconds/synchronizations re-documented
tests/unit/gpu/test_device_buffer.cpp     spmv synchronization contract updated to guard the new behaviour
```
