<!-- P18.8 evidence. Environment for every number in this file: WSL2 Ubuntu 22.04,
     nvcc 12.9.86 (/usr/local/cuda-12.9/bin/nvcc -- NOT the 11.5 first on PATH),
     g++ 11.4.0, glibc 2.35, RTX 5000 Ada cc 8.9 driver 580.97. --use_fast_math: 0.
     WSL2 evidence does not establish native Windows CUDA. -->

# P18.8 — Mixture properties on device (E1)

**BITWISE, and this criterion carries G9's condition (4).**

## What was compared

`M_mix`, `R_mix`, `cp_mix`, `cv_mix`, `γ` and the status, device against the same shared header
compiled for nvcc's host.

| Case | Cells | Result |
| --- | --- | --- |
| E1's frozen list: single-species, binary, ternary, near-pure (`Y → 1−ε`), × 5 temperatures | 64 each, 1 280 comparisons | **0 differing** |
| spatially varying ternary | 1 024 | **0 differing** |

Plus an independent check that is not a comparison of two implementations: a **single-species**
mixture must return exactly that species' own `M` and `cp`. It does.

The spatially-varying case asserts `M_mix` differs between the first and last cell, so a kernel that
computed one cell and broadcast it could not pass.

## Why this criterion ended up load-bearing

The `--fmad=true` control translation unit — same shared header, same kernel bodies, contraction
enabled — moves `R_mix` in **662 of 4 096** cells and `cp_mix` in **543 of 4 096**. Since these are
zero-tolerance criteria, that makes E1 the place where a lost `--fmad=false` is detected.

It could not be detected on E3/E4: contraction moves `q` by 0.843356 in the bound's units while the
libm difference the bound must admit is 3.13865, so no passing threshold there can reject it. The
reallocation is argued in `acceptance_gate_A2.md` §4 and `coverage_map.md` §3.

## The signature that validates the FMA control

`deviceMolecularWeight` accumulates `reciprocal += composition[i] / weight` — a divide then an add,
**not** an `a*b+c` pattern, so FMA cannot reach it. `R_mix` and `cp_mix` accumulate
`total += composition[i] * coefficient`, which it can.

| Quantity | Pattern | FMA effect |
| --- | --- | --- |
| `M_mix` | divide then add | **0 of 4 096** |
| `R_mix` | `a*b+c` | 662 of 4 096, worst 2 ULP |
| `cp_mix` | `a*b+c` | 543 of 4 096, worst 1 ULP |

A control that broke all three, or none, would be measuring something other than contraction.

## Parenthesisation

`M_mix` is `1.0 / Σ(Y_i / M_i)` — a divide per term, one reciprocal at the end. **Not**
`1.0 / Σ(Y_i · (1/M_i))`, which is a different number: 17.2 % of random doubles fail the `1/(1/x)`
round trip, so the other form would be a defect rather than a simplification.

## Controls

| Control | Effect |
| --- | --- |
| N1 species index permuted | 3 072 values differ |
| N2 one weight for all species | 1 024 values differ |
| `--fmad=true` | 662 and 543 of 4 096 cells |

## Limit

`DeviceSpecies` carries one **constant** `cp` per species; the CPU production path supports ranged
`cp`. So E1's "× 5 temperatures" clause is **inert for this mechanism family** — the figures above
satisfy the frozen list without measuring anything new in `T`. See `kernel_inventory.md` §4.
