# GPU-PIPE-001 rho-carry — 5 → 4 reduction groups

**Status: PASS.** Dependency proof: [`dependency_proof.md`](dependency_proof.md).

## The change

`GpuBiCGSTAB` computed `rho = dot(rHat_, r_)` at the top of each iteration and
`residualNorm = l2Norm(r_)` at the bottom — two round trips reading the **same** `r_`, because
nothing between them writes it. They are now one fused reduction, and iteration 1's `rho` is taken
from the setup reduction that already computes `dot(r_, r_)`.

Three lines of real change:

```cpp
// setup: one reduction yields both
const Real r0DotR0 = dot(r_, r_);
const Real b0      = std::sqrt(r0DotR0);
Real rhoNext       = r0DotR0;        // == dot(rHat_, r_), rHat_ is a bitwise copy of r_

// top of loop
const Real rho = rhoNext;            // carried, not recomputed

// bottom of loop
dot2(r_, r_, rHat_, r_, rDotR, rhoNext);
const Real residualNorm = std::sqrt(rDotR);
```

`GpuCG` untouched. `SIMPLE.cpp` untouched. No threshold, tolerance or breakdown criterion changed.

## Measured (paired, same session, medians of 3)

| grid | metric | before | after | change |
| --- | --- | ---: | ---: | ---: |
| 160² | reduction groups / Krylov | 5.00 | **4.00** | −20 % |
| | D2H calls | 19 502 | 15 604 | **−20.0 %** |
| | synchronizations | 19 478 | 15 580 | **−20.0 %** |
| | kernel launches | 50 702 | 46 804 | −7.7 % |
| | gpu_solve | 2.459 s | 1.948 s | **−20.8 %** |
| | end-to-end | 3.198 s | 2.661 s | −16.8 % |
| 320² | reduction groups / Krylov | 5.00 | **4.00** | −20 % |
| | D2H calls | 18 407 | 14 726 | **−20.0 %** |
| | synchronizations | 18 389 | 14 708 | **−20.0 %** |
| | gpu_solve | 2.929 s | 2.540 s | **−13.3 %** |
| | end-to-end | 5.355 s | 4.942 s | −7.7 % |
| 640² | reduction groups / Krylov | 5.00 | **4.00** | −20 % |
| | D2H calls | 18 393 | 14 716 | **−20.0 %** |
| | synchronizations | 18 381 | 14 704 | **−20.0 %** |
| | gpu_solve | 3.626 s | 2.997 s | **−17.3 %** |
| | end-to-end | 12.956 s | 12.233 s | −5.6 % |

Fusion factor (quantities ÷ groups) is now **1.75×** — 7 reduction quantities delivered in 4 round
trips. `reductionQuantities` per Krylov iteration is **7 before and 7 after**: the arithmetic is
unchanged, only its packaging.

D2H **bytes** fall by 0.0–0.1 %, which is the point: this removes round trips, not volume.

## Correctness — bitwise

| grid | pressure residual | Krylov its | verdict |
| --- | --- | ---: | --- |
| 160² | `0.0010531773410759504` | 3 898 | **identical** |
| 320² | `0.00068343695924319469` | 3 681 | **identical** |
| 640² | `0.00046812560910251881` | 3 677 | **identical** |

Continuity residual, mass imbalance, outer iterations and solve counts also identical.

## Gates

| Gate | Result |
| --- | --- |
| Dependency proof valid on every path | **yes** — including the absence of a restart path |
| BiCGSTAB / CG / SIMPLE tests | **PASS** — 66/66, 97/97, 123/123 |
| GPU-PCORR-001 regression | **PASS** — 6/6 |
| compute-sanitizer (4 tools) | **PASS** — 0 errors each |
| Bitwise identity | **PASS** |
| Negative control | **PASS** (below) |

## Negative control

Injecting back the removed reduction (recompute `rho` at the top of the loop):

```text
groups/iteration   clean 4.00     mutant 5.00
red_groups         clean 9 346    mutant 11 684   delta +2 338
d2h_calls          clean 9 358    mutant 11 696   delta +2 338
krylov_it          clean 2 338    mutant 2 338    unchanged
p_res              identical in both
expected extra groups = krylov_it = 2338, observed = 2338
```

The control now also **proves the restore**, which the previous one did not:

```text
source restored AND library rebuilt
source clean: no injection markers
build/cuda up to date with the restored source (ninja)
restored groups/iteration = 4.00 (equals the clean measurement)
```

This was added because the earlier negative control restored the source but left the mutant library
in `build/cuda`, and a later probe silently measured it (23 400 D2H calls instead of 19 502). The
stale-binary hazard is real, and a control that creates one is worse than none.
