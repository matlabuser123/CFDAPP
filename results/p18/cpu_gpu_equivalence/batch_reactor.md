<!-- P18.8 evidence. Environment for every number in this file: WSL2 Ubuntu 22.04,
     nvcc 12.9.86 (/usr/local/cuda-12.9/bin/nvcc -- NOT the 11.5 first on PATH),
     g++ 11.4.0, glibc 2.35, RTX 5000 Ada cc 8.9 driver 580.97. --use_fast_math: 0.
     WSL2 evidence does not establish native Windows CUDA. -->

# P18.8 — The marched trajectory on device (E9, amended by A2)

**The state stays BITWISE over a march in which the state actually moves.**

## Measured

256 cells, 3 species, 2 reactions, `dt = 1e-5`, device resident (no host round trip) against an
independent host march:

| step | max \|ΔT\|/T | max \|ΔY\| | worst `T` ULP |
| --- | --- | --- | --- |
| 1 | 0 | 0 | 0 |
| 5 | 0 | 0 | 0 |
| 20 | 0 | 0 | 0 |
| 50 | 0 | 0 | 0 |

and the state moved: **`max |T − T₀| = 28.6389` K**, **`max |Y − Y₀| = 0.0075085`**.

## The ladder of zeros was vacuous on the first attempt

The first version of this probe used `dt = 1e-7` and reported zero at every sample **for the wrong
reason**: at that step size neither `T` nor `Y` moved by one ULP, so it was comparing two unchanged
initial states and printing exactly what success looks like.

The probe now asserts the state moved by more than one ULP of its own magnitude before the ladder is
allowed to mean anything, and the figures above are from the version that passes that check.

## Control N10 — the trajectory must be sensitive to the integration

A bitwise agreement is only interesting if the thing being compared is doing work. The frozen control
table lists "N10 — different timestep" against E9; it was **not implemented** until
`coverage_map.md` exposed the omission.

The same total time, marched two ways:

| | | |
| --- | --- | --- |
| `dt = 1e-5 × 50` vs `dt = 5e-6 × 100` | `max \|ΔT\| = 0.0124043` K | `max \|ΔY\| = 3.26358e-06` |

Bounded away from zero (so the march integrates something) and from a blow-up (so it is a
first-order truncation difference and not instability). Both are asserted.

## Residency during the march

The resident loop performs the device-to-device hand-off by swapping buffers; measured **0 H2D
calls, 0 D2H calls, 0 bytes** across 10 steps. See `transfer_residency.md`.

## Limit

The march is **explicit and first order**, with a measured step-size budget: P18.1's sum-to-one
tolerance is exceeded after roughly 72 steps for 3 species, and sooner for 2. A stiff or implicit
integrator is not implemented in any P18 phase. This is a reactor march, not a reacting-flow solve;
**G8 remains unclosed**.
