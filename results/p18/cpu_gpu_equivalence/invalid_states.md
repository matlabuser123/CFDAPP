<!-- P18.8 evidence. Environment for every number in this file: WSL2 Ubuntu 22.04,
     nvcc 12.9.86 (/usr/local/cuda-12.9/bin/nvcc -- NOT the 11.5 first on PATH),
     g++ 11.4.0, glibc 2.35, RTX 5000 Ada cc 8.9 driver 580.97. --use_fast_math: 0.
     WSL2 evidence does not establish native Windows CUDA. -->

# P18.8 — Invalid-state parity on device (E2's decision clause, E11)

**The device must refuse exactly what the host refuses, and name the SAME FIRST offender.**

## Why the reason and not only the refusal

The CPU throws at the first invalid thing it meets, so its reason is always the **root cause**. A
device status written last-write-wins would instead name whatever failed **last** — a downstream
consequence. The two backends would then reject the same state for different reasons, which the
frozen criterion calls a divergence in the admissible set.

## This caught a real device defect

Not hypothetical. The first version of the shared header overwrote the status freely, and control
**N11** caught it: a `T = −5` state reported `NonFiniteEnergy` (12), a consequence, where the CPU
reports `NonPositiveTemperature` (2), the cause. Preserved verbatim at
`logs/02_first_failure_PRESERVED.log` and fixed with `setStatusOnce()`, so **first failure wins**,
with every status write routed through it.

## Measured

| State | Expected reason | Host | Device |
| --- | --- | --- | --- |
| `T = −5` K | `NonPositiveTemperature` | ✓ | ✓ |
| `T = 0` K | `NonPositiveTemperature` | ✓ | ✓ |
| `T = NaN` | `NonFiniteTemperature` | ✓ | ✓ |
| `T = +∞` | `NonFiniteTemperature` | ✓ | ✓ |
| `ρ = 0` | `NonPositiveDensity` | ✓ | ✓ |
| `ρ = −1` | `NonPositiveDensity` | ✓ | ✓ |
| a valid state | `Ok` — **accepted** | ✓ | ✓ |

Three assertions per row: the host reason matches the table (so a changed host reason is a stale
expectation rather than a silent pass), the device reason matches the table, and the two match each
other.

**Non-vacuity:** the table must contain states that are refused **and** one that is not. Six of the
seven rows are refusals and the seventh must be accepted — otherwise the suite would only ever test
rejection, and a device that refused everything would pass.

## The sixteen reasons

`DeviceSpeciesStatus` carries 16 distinct values, from `NonFiniteTemperature` through
`TemperatureNotBracketed`. The refusal of an out-of-range temperature recovery
(`TemperatureNotBracketed`) is the device's mirror of P18.6's fix: refuse rather than extrapolate
outside the species' common validity range.

## Controls

N11 (invalid state silently accepted) — a flag on the production path, so a control run and a
production run are the same binary. Exercised in link one; it is the control that found the defect
above.
