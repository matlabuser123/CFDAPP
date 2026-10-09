<!-- P18.8 evidence. Environment for every number in this file: WSL2 Ubuntu 22.04,
     nvcc 12.9.86 (/usr/local/cuda-12.9/bin/nvcc -- NOT the 11.5 first on PATH),
     g++ 11.4.0, glibc 2.35, RTX 5000 Ada cc 8.9 driver 580.97. --use_fast_math: 0.
     WSL2 evidence does not establish native Windows CUDA. -->

# P18.8 — Concentrations and reaction sources on device (E4, E5, amended by A2)

## Concentrations — BITWISE

`C_i = (ρ·Y_i)/M_i`. No transcendental, no accumulation. **0 differing** over 1 024 cells × 3
species, and **0 of 12 288** under the `--fmad=true` control, which is the expected result: a
multiply and a divide contain no `a*b+c` site for a fusion to reach.

Parenthesisation: `(ρ·Y_i)/M_i`, a multiply **then** a divide. `ρ·(Y_i/M_i)` is a different number.

## Progress rates — bounded

`|Δq| ≤ 8 · ε · (|k_f·ΠC′| + |k_r·ΠC″|)`.

| Run | Worst | Bound | Exercised |
| --- | --- | --- | --- |
| gate | **2.7411** | 8 | 2 048 |
| dry-run | **3.13865** | 8 | 8 192 |

The scale is the **sum of the term magnitudes**, not `|q|`. Bounding by `ε·|q|` would tighten
without limit as a reversible reaction approaches equilibrium and its two terms cancel — a property
of the cancellation, not of the two implementations. Cells with a zero term scale are required to
agree **exactly** instead.

## Species sources — BITWISE, from the device's own `q`

A2's structural change: rather than loosen `ω_i`, change *what* is compared. The libm difference
originates in `k` and reaches `ω` only through `q`, so feeding the device's **own** `q` back through
the host accumulation removes it as a confound and leaves the accumulation — its order, its
parenthesisation, its coefficients — under **zero tolerance**.

| Comparison | Result |
| --- | --- |
| `ω_i` from the device's own `q` | **0 / 12 288 differing** (gate), **0 / 12 288** (dry-run) |
| `ω_i` end to end, propagated bound `8·ε·Σ_r\|M_i ν_i q_r\|` | worst **2.78851** against 8, 3 072 exercised |

Accumulation order is the CPU's: ascending reaction index, then ascending species index.
Parenthesisation is `(M_i · ν_i) · q`, **not** `M_i · (ν_i · q)`.

## Recorded: FMA cannot reach this accumulation

The `--fmad=true` control changes `ω_i` in **0 of 12 288** values. Tested rather than assumed: the
host's own `std::fma` agrees with multiply-then-add on **12 288 of 12 288**, so the products
`M_i·ν_i·q` are exactly representable for this mechanism and no fusion has any rounding to remove.
The device control found the same, and the dry-run asserts the two agree — a disagreement would mean
one of them is not measuring contraction.

So this clause's controls are **N5 and N8**, not FMA:

| Control | Effect |
| --- | --- |
| N5 `M_i` omitted in `ω_i` | 3 072 values differ |
| N8 one reaction omitted | 4 096 values differ |
| N4 exponent sign flipped | 3.52 × 10²² × the `q` bound |

N8 was first classified as a control for the bounded `q` criterion. It measured **0** there —
omitting a reaction leaves the progress rates untouched and changes the accumulation. Reclassified;
see `coverage_map.md` §4.
