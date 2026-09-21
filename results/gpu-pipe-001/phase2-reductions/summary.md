# GPU-PIPE-001 Phase 2 — reductions

**Status: PASS**, with one gate item deliberately **not shipped** on its own measurement (§4).

Baseline `548401aef0d3cbab9552b1964ceac7efd7ad6154`. Environment: WSL2 Ubuntu 22.04, RTX 5000 Ada
(CC 8.9), driver 580.97, CUDA 12.9.86, Release, native `sm_80`+`sm_89`.

## 1. What Phase 1 said to attack

7 device→host reductions per Krylov iteration, 99.7–100 % of all D2H calls, **79–101 µs each
regardless of payload**. The cost is paid per *call*, not per byte.

## 2. What was implemented

**Phase 2B — fused reductions (shipped).** A `partialDot2Kernel` computes two independent dot
products over the same length in one launch, each keeping its own block partition, its own
shared-memory halving tree and its own partial slice. Applied at the two places in `GpuBiCGSTAB`
where independent reductions sit together:

```text
dot(rHat,v) + l2Norm(v)   ->  dot2(rHat,v, v,v)
dot(t,t)    + dot(t,s)    ->  dot2(t,t, t,s)
```

**7 → 5 reductions per Krylov iteration**, measured exactly at every grid.

**Phase 2C — reduction counters (shipped).** `reductionGroups` (round trips) and
`reductionQuantities` (scalars produced); their ratio is the realised fusion factor. Previously the
only way to know the reduction cost was to infer it from `deviceToHostCalls` minus an assumed
solution-download count.

## 3. Correctness: bitwise, not "within tolerance"

Fusion changes *when* arithmetic happens, never *what* it is — same terms, same block partition,
same per-product tree, same final summation order. So the claim is bit-for-bit equality, and it is
tested as such.

**Gate 1** (`reduction_identity.cpp`): **120/120 bitwise checks, 0 failures**, over sizes
straddling the 256-thread block boundary (1, 255, 256, 257, 511, 512, 1000, 25 600, 102 400,
409 600), random and cancelling magnitudes (10⁻⁸…10⁸), and zero vectors. The probe is proven
**non-vacuous**: a deliberately reassociated (pairwise) reference sum differs from `dot()` on
**3 of 3** cancelling cases, so the test can distinguish summation orders.

**At solver level**, paired before/after over 3 repeats at each grid:

| grid | pressure residual (17 s.f.) | Krylov its | outer | solves | verdict |
| --- | --- | ---: | ---: | ---: | --- |
| 160² | `0.0010531773410759504` | 3 898 | 8 | 24 | **identical** |
| 320² | `0.00068343695924319469` | 3 681 | 6 | 18 | **identical** |
| 640² | `0.00046812560910251881` | 3 677 | 4 | 12 | **identical** |

Continuity residual, global mass imbalance and pressure linear-iteration counts are identical too.
No threshold, tolerance or breakdown criterion was touched; GPU-PCORR-001's scale-relative test is
untouched and its regression passes.

## 4. Phase 2A: implemented, measured, and removed on the evidence

Device-side finalisation *was* implemented — `finalizeSumsKernel`, one thread per quantity walking
its partials in ascending block order so the bits matched the host loop exactly. It reduced D2H
**bytes** by 81–89 %.

It was then removed, because paired measurement said it does not pay:

```text
gpu_solve median, same session       device finalise    host finalise
  160^2  (100 blocks)                    2.0315 s          2.0191 s
  640^2 (1600 blocks)                    5.0178 s          3.7741 s   <- 25% worse
```

The finaliser is O(blocks) *dependent* adds on one CUDA core, and blocks grows with the problem,
so its cost grows — while what it buys, an 8-byte download instead of a blocks-sized one, buys
nothing, because Phase 1 measured the round trip at 79–101 µs **regardless of payload**. It was
never faster at any measured size.

This is exactly the outcome the phase's own brief anticipated: *"Do not assume that merely replacing
partial sums → D2H → CPU sum with partial sums → GPU final sum → scalar D2H will solve the
synchronization problem. Measure it."* Measured, it did not. The acceptance-gate item
**"device-finalized reductions implemented" is therefore reported as implemented-and-reverted**, not
as shipped. Evidence: `40_isolate_finalizer.log`.

## 5. Measured result (paired, same session, medians of 3)

| grid | metric | before | after | change |
| --- | --- | ---: | ---: | ---: |
| 160² | D2H calls | 27 280 | 19 502 | **−28.5 %** |
| | synchronizations | 58 480 | 50 702 | **−13.3 %** |
| | kernel launches | 58 480 | 50 702 | −13.3 % |
| | D2H bytes | 26 720 000 | 26 720 000 | 0.0 % |
| | gpu_solve | 3.460 s | 2.700 s | **−22.0 %** |
| | end-to-end solve | 4.236 s | 3.496 s | −17.5 % |
| 320² | D2H calls | 25 752 | 18 407 | **−28.5 %** |
| | synchronizations | 55 201 | 47 856 | **−13.3 %** |
| | gpu_solve | 5.221 s | 4.160 s | **−20.3 %** |
| | end-to-end solve | 7.837 s | 6.730 s | −14.1 % |
| 640² | D2H calls | 25 739 | 18 393 | **−28.5 %** |
| | synchronizations | 55 165 | 47 819 | **−13.3 %** |
| | gpu_solve | 5.213 s | 3.975 s | **−23.7 %** |
| | end-to-end solve | 14.899 s | 13.464 s | −9.6 % |

Reductions per Krylov iteration: **7.00 → 5.00** at all three grids. H2D unchanged. D2H **bytes**
unchanged — fusion removes calls, not volume, which is the correct target.

**Noise honesty:** repeat spread on end-to-end `solve_s` is 0.13–1.14 s, comparable to the 320²/640²
end-to-end differences, so the end-to-end column at those grids is suggestive rather than decisive.
`gpu_solve_s` — the quantity the change actually touches — moves −20 % to −24 % consistently and
well outside spread.

## 6. Negative control

Injecting one redundant `dot(r_, r_)` per BiCGSTAB iteration must be detected by the new counters:

```text
                clean     mutant    delta
d2h_calls       11 696    14 034    +2 338
syncs           30 407    32 745    +2 338
krylov_it        2 338     2 338    unchanged (injection is inert)
p_res       0.0018474694761252055  identical
```

`+2338` is exactly `krylov_it` — the predicted magnitude, not merely a detected difference. The
injection was removed and the source restored (verified: 0 occurrences remain).

The first run of this control **failed correctly**: the chosen anchor
`const Real residualNorm = l2Norm(r_);` occurs in *both* `GpuCG` and `GpuBiCGSTAB`, so the injector
refused to patch ambiguously, nothing was injected, and the verdict caught that the counters had not
moved. Fixed by anchoring on a line unique to BiCGSTAB.

## 7. Gates

| Gate | Result |
| --- | --- |
| 1 — reduction identity | **PASS** — 120/120 bitwise, non-vacuity 3/3 |
| 2 — solvers | **PASS** — GPU 66/66, algebra 97/97, SIMPLE 123/123, GPU-PCORR 6/6 |
| 3 — compute-sanitizer | **PASS** — memcheck/initcheck/synccheck/racecheck, 0 errors each |
| 4 — production cases | **PASS** — 160², 320², 640² identical residuals and iteration counts |
| Negative control | **PASS** — exact predicted magnitude |

A parser defect in the Gate 3 script initially reported `FAIL` on a clean run: it read only
`ERROR SUMMARY`, while racecheck prints `RACECHECK SUMMARY`. Fixed, and an unparsable summary is now
an explicit failure rather than a silent default.

## 8. The new measured bottleneck

Synchronizations fell only 13.3 % while reduction round trips fell 28.5 %, because reductions are no
longer the majority of syncs:

```text
160^2 after:  50 702 synchronizations,  of which 19 502 are reduction round trips (38 %)
              the remaining ~31 200 are waxpby / axpy / spmv / copy / fill kernels,
              each of which does its own cudaDeviceSynchronize
```

Every elementwise vector op in `DeviceVectorOpsKernel.cu` synchronizes after its launch. Those are
now the dominant source of host-device serialization, and they do **not** need a host round trip at
all — nothing reads their result on the host. That, not further reduction fusion, is where the next
gain is.

## 9. Files changed

```text
cuda/kernels/DeviceVectorOpsKernel.cu    partialDot2Kernel, grouped reduceToHost, dot2
cuda/kernels/GpuLinearSolverCuda.cpp     two fused reduction sites in GpuBiCGSTAB
include/cfd/gpu/DeviceVectorOps.hpp      dot2 declaration
include/cfd/gpu/GPUExecutionStats.hpp    reductionGroups / reductionQuantities
```

No numerical threshold, tolerance, iteration limit or breakdown criterion changed. `GpuCG` is
untouched. No persistent fields, persistent matrices, GPU-resident pressure solve or GPU-resident
SIMPLE loop was started.
