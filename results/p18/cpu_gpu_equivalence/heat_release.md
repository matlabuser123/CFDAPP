<!-- P18.8 evidence. Environment for every number in this file: WSL2 Ubuntu 22.04,
     nvcc 12.9.86 (/usr/local/cuda-12.9/bin/nvcc -- NOT the 11.5 first on PATH),
     g++ 11.4.0, glibc 2.35, RTX 5000 Ada cc 8.9 driver 580.97. --use_fast_math: 0.
     WSL2 evidence does not establish native Windows CUDA. -->

# P18.8 — Heat release on device (E6, amended by A2)

## The frozen convention

P18.6 fixed the convention and P18.8 does not revisit it: transported energy is **ρ·cp(T,Y)·T**, the
conserved total is `E = ρcpT + ρΣY_i h_f,i`, and the source is
`S_E = −Σ_i h_f,i ω_i = −Σ_r q_r ΔH_r⁰`.

Both routes are computed on device and both are compared.

## Measured — BITWISE from the device's own `q`

| Route | Result |
| --- | --- |
| `S_E = −Σ_r q_r ΔH_r⁰` from the device's own `q` | **0 / 1 024 differing** (gate), **0 / 4 096** (dry-run) |
| `S_E = −Σ_i h_f,i ω_i` from the device's own `ω` | **0 / 1 024**, **0 / 4 096** |
| `S_E` end to end, propagated bound `8·ε·Σ_r\|q_r ΔH_r\|` | worst **2.73713** against 8, 1 024 exercised |

That the two independent routes agree bitwise is a correctness check, not only an equivalence one:
they are different formulas over different quantities.

Parenthesisation: `ΔH_r⁰` accumulates `total += ν_i · (h_f,i · M_i)`, and `S_E` accumulates
`source -= q_r · ΔH_r`, both ascending.

## Sign equivalence is a hard pass/fail

The frozen criterion makes the sign a pass/fail rather than a magnitude question, and control **N6**
flips it. Its measured effect is **9.44 × 10¹⁸ ULP**.

N6 is also what exposed a defect in the comparison instrument: the first ULP comparator reinterpreted
doubles as signed integers and subtracted, which overflows when the signs differ, and the wrapped
value read as **0** — so the largest error the suite can inject was scored as a perfect match. The
comparator now uses a monotonic key and is self-tested on planted disagreements. See
`negative_controls.md`.

## Under the `--fmad=true` control

`S_E` differs in **1 of 4 096** cells, worst 1 ULP, via `q`; the accumulation itself is unaffected
for the reason given in `reaction_sources.md`.
