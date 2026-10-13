<!-- P18.8 evidence. Environment for every number in this file: WSL2 Ubuntu 22.04,
     nvcc 12.9.86 (/usr/local/cuda-12.9/bin/nvcc -- NOT the 11.5 first on PATH),
     g++ 11.4.0, glibc 2.35, RTX 5000 Ada cc 8.9 driver 580.97. --use_fast_math: 0.
     WSL2 evidence does not establish native Windows CUDA. -->

# P18.8 — Arrhenius kinetics on device (E3, amended by A2)

**≤ 8 ULP on `k`, and the BRANCH must be identical.** This is one of only three criteria A2 relaxed.

## Measured

| Quantity | Measured | Bound |
| --- | --- | --- |
| `k` worst ULP distance (gate run, 2 048 comparisons) | **3** | 8 |
| `k` exactly bitwise | 1 564 of 2 048 | — |
| `k` worst ULP (dry-run, 8 192 comparisons) | **3** | 8 |
| branch disagreements | **0 / 8 192** | 0 |

## Why `k` is bounded and not bitwise

`k = (A·T^n)·exp(−Ea/(R_u·T))` routes through `pow` and `exp`. Those come from glibc under gcc and
from CUDA libdevice under nvcc — different implementations — and IEEE-754 requires correct rounding
only for `+ − × ÷` and `sqrt`. Full diagnosis in `libm_divergence.md`; the derivation of 8 is in
`acceptance_gate_A2.md` §2 (2 ULP per faithfully-rounded libm call, two calls, plus two multiplies,
rounded to the next power of two).

**Isolated, so the cause is named rather than inferred.** Choosing `n` and `Ea` to make each factor
exact — `pow(x,0)` and `exp(0)` are exactly 1 by IEEE-754 — gives:

| case | `n` | `Ea` | isolates | differing | worst ULP |
| --- | --- | --- | --- | --- | --- |
| A | 0 | 0 | nothing | **0 / 4 096** | 0 |
| B | 0 | 4e4 | `exp` only | 218 / 4 096 | 2 |
| C | 0.5 | 0 | `pow` only | 901 / 4 096 | 2 |
| D | 0.5 | 4e4 | both | 1 029 / 4 096 | 3 |

**Case A is exact over all 4 096 cells.** Operand order, parenthesisation, accumulation order, the
`--fmad=false` registration, the geometry upload and the POD upload would every one of them have
failed case A. The cause is `exp` and `pow`, and nothing else.

## The branch is NOT bounded

Amendment A1 selects the direct form when `A·T^n` is finite and a log-space form otherwise, and that
predicate reads `pow`'s **output**. If the two libms disagreed there, the backends would run
different *algorithms* and the difference would stop being a last-bit one.

Swept across **4 096 consecutive doubles** around the crossing (`A = 3`, `n = 100`, `Ea = 457 000`
J/mol, chosen so the true `k` stays finite while `A·T^n` straddles `DBL_MAX`):

```
host branch at the low end 1, at the high end 2   (1 = direct, 2 = log-space fallback)
BRANCH DISAGREEMENTS host vs device:  0 / 4096
worst ULP where the branches AGREED:  3
```

The sweep genuinely crossed the predicate — the test asserts the branch changes within it, or the
probe would prove nothing.

**Limit of this evidence:** one `(A, n, Ea)` triple and one 4 096-point window. It shows the
bifurcation did not occur here; it does not prove it cannot occur anywhere.

## Control

N4, the Arrhenius exponent sign flipped: **3.52 × 10²² times** the `q` bound.
