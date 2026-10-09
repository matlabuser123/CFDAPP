<!-- P18.8 evidence. Environment for every number in this file: WSL2 Ubuntu 22.04,
     nvcc 12.9.86 (/usr/local/cuda-12.9/bin/nvcc -- NOT the 11.5 first on PATH),
     g++ 11.4.0, glibc 2.35, RTX 5000 Ada cc 8.9 driver 580.97. --use_fast_math: 0.
     WSL2 evidence does not establish native Windows CUDA. -->

# P18.8 — Energy update and temperature recovery on device (E7, NOT amended)

**BITWISE. A2 does not touch this criterion**, and it passes at zero tolerance.

A2 mislabelled it — it occupied the E7 number for mixture properties, which are E1/E2. Corrected in
`acceptance_gate_A2_erratum.md` §1; the substance below is unaffected.

## What the frozen criterion demands

> the energy source, the updated sensible energy, the recovered `T`, and the recovery STATUS; then
> the full coupled chain `T_n → k_n → q_n → ω_n / S_E,n → energy update → T_(n+1) → k_(n+1)` with
> **EVERY INTERMEDIATE** compared, not only the final `T`

## Measured

| Quantity | Result |
| --- | --- |
| sensible energy density, after the step | **BITWISE**, 1 024 / 1 024 |
| chemical energy density, after the step | **BITWISE**, 1 024 / 1 024 |
| recovered `T` | **BITWISE**, 1 024 / 1 024 |
| recovery status and its reason | **BITWISE**, and it must name the same **first** offender |
| every branch index | **BITWISE** |

## Why the state is bitwise although the rates are not

This is measured, not assumed. The `≤ 3 ULP` difference in `ω` enters the state as
`Y + dt·ω/ρ`, where it lands **below one ULP of `Y`** and is rounded away. `T` is then recovered by
bisection — 200 halvings, the same rule as the CPU — which returns the unique double in its bracket
and absorbs what remains.

So the temperature recovery acts as a rounding filter: a few-ULP perturbation of the energy does not
change which double the bisection lands on.

## Temperature recovery

Bisection over the species' **common** validity range, refusing rather than extrapolating — the fix
P18.6 made after the recovery was found bracketing from `T = 1` K, outside the `cp` polynomial's
validity range. The device mirrors that rule, including the refusal.

Composition update is **additive in mass-fraction space**, not a `(ρY + dt·ω)/ρ` round trip: that
round trip drifted `ΣY` to 0.99999999999999722, past P18.1's 2.66e-15 budget. Also P18.6's fix,
mirrored here.

Zero increment is an **exact no-op** and the bisection is skipped when nothing moved, so the
non-reacting limit is bitwise rather than drifting ~1 ULP per step.

## Controls

N7 (stale temperature) and N9 (stale composition in recovery), both exercised in link one.
