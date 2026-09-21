# GPU-DISC-001G — CUDA momentum response coefficients

**Result: PASS.** The CUDA response coefficient is **bitwise identical** to the CPU on every value
compared — 44,434 values across 260 cases, synthetic and integrated.

```text
synthetic cases        36      (L1: diagonals constructed directly)
integrated cases      224      (L2: CPU assembly->response vs CUDA assembly->response)
values compared        44,434
bitwise-identical      44,434  (100%)
max absolute error     0
max relative error     0
negative controls      6/6 detected, each restored and re-passed
compute-sanitizer      4/4 clean, non-vacuous
operator gates         7/7 still green
full regression        1998/1998 passed, 0 failed, 392.7 s
                       `ninja: no work to do` both BEFORE and AFTER ctest
```

## 1. Mathematical definition, from the source

`cfd::pressure_velocity::computeMomentumResponseCoefficient`
(`PressureCorrectionEquation.cpp:103`) is four lines:

```text
d_P = V_P / aP_P
```

per cell, with `V_P = cell.volume()` and `aP_P` an entry of a `MomentumAssembly::diagonal`. The
project's own header states the same form, so this **is** the textbook `V/aP` — confirmed from the
code, not assumed.

**No density. No explicit relaxation. No component argument. No dimensional behaviour. No
clipping.** Each of those was checked against the implementation; `audit.md` §2 tabulates it.

### Relaxation enters indirectly, and that shaped the test design

The function takes no `alpha`, yet its output does depend on it — because the diagonal it is handed
is the **relaxed** one, `aP_unrelaxed / alpha` in effect. So relaxation could not be tested by
varying an argument; it had to be swept through the **assembly**, which is what the integrated L2
layer does (α = 1.0, 0.7, 0.3, 0.05).

### Component dependence is in the caller

The function is component-agnostic; `SIMPLE.cpp:474-477` calls it once per component (W only on a
3D mesh), `PISO.cpp:270-271` for U and V (PISO is 2D-only), `CompressibleSIMPLE.cpp:339-341` for U
and V. Downstream consumers — `rhieChowMassFlux`, `assemblePressureCorrection`, `correctVelocity` —
are all deferred and untouched.

## 2. Edge behaviour: the contract is upstream, and no safeguard was added

This is the part worth reading carefully. The CPU divides **unguarded**, and its header says why:

> `momentumDiagonal` is a `MomentumAssembly` diagonal (already positive, finite, and nonzero per
> `MomentumEquation.hpp`'s own guarantees).

The guarantee is enforced at assembly time (`RelaxedMomentum.cpp:98` throws `NumericalError` on a
non-finite system), not in this function. So:

| input | CPU behaviour | device |
| --- | --- | --- |
| size mismatch | `InvalidArgumentError` — the only check | same check, same error |
| `aP = 0` | `±inf`, unguarded | reproduced, unguarded |
| `aP` tiny but valid | large finite `d`, not clipped | reproduced |
| `aP` negative | negative `d`, unguarded | reproduced |
| `aP` non-finite | propagates; prevented upstream | reproduced |
| `V = 0` | `d = 0`; degenerate cells rejected by `MeshQuality` | reproduced |

**No new production safeguard was added.** Adding one would change numerical semantics rather than
port them, which this phase is not authorized to do. Instead the differential tests the **valid
domain and its extremes**: `aP = 1e-300`, `aP = 1e300`, and `aP` at
`std::numeric_limits<double>::min()` (denormal-adjacent). Those produce response coefficients from
`1.6e-302` to `7.0e+305` — all bitwise identical.

## 3. Differential

`differential/differential.log`

**L1 synthetic** — 6 diagonal families × 6 meshes (`cartesian2d 8`, `cartesian2d 24`,
`graded2d 12`, `distorted q16`, `cartesian3d 4`, `warped 3d 3`): uniform 1.0, uniform 3.7,
nonuniform, small 1e-300, large 1e300, denormal-adjacent.

**L2 integrated** — the chain the brief asks for:

```text
CPU momentum assembly -> CPU computeMomentumResponseCoefficient
CUDA momentum assembly -> CUDA computeMomentumResponseCoefficientDevice
```

224 cases: components U (96) / V (96) / W (32, 3D only) × 4 convection schemes × 4 relaxation
factors × 6 meshes, with the all-five vector boundary set and a mixed pressure set. The response
step performs **zero** device-to-host copies — asserted per case.

2D: 128 integrated cases. 3D: 96.

The harness **fails** if a component, a dimension, or the valid-domain extremes are not exercised.

## 4. Negative controls — 6/6 detected

`negative-control/` — each: inject → build → check → restore → rebuild → sha256 → freshness →
re-pass.

| control | mutation | detected |
| --- | --- | --- |
| R1 | `1/aP` instead of `V/aP` (cell volume dropped) | 92/92 |
| R2 | response sign reversed | 92/92 |
| R3 | `aP/V` instead of `V/aP` | 92/92 |
| R4 | diagonal read with an off-by-one index | 88/92 |
| R5 | assembly reports the **unrelaxed** diagonal (stale relaxation data) | 60/92 — only the α ≠ 1 cases, as it should |
| R6 | assembly reports the row's first entry instead of the diagonal | 80/92 |

R1 covers both "using `1/aP`" and "dropping cell volume". R5 covers both "wrong relaxation
dependence" and "stale diagonal data", and its detection pattern is itself informative: it fires
on exactly the relaxed cases and not the α = 1 ones.

**One category from the brief has no applicable mutation, and that is stated rather than papered
over:** *wrong 2D/3D indexing*. This kernel has no dimension-dependent indexing at all — `d = V/aP`
is a flat per-cell map, and `cell.volume()` is the mesh's own volume in either dimension. R4
(off-by-one indexing) is the closest real defect and is detected. 2D and 3D are both covered by the
differential; there is simply no 2D/3D branch here to break. *Wrong component diagonal* is likewise
a caller-wiring error rather than something expressible in this function, which takes the diagonal
as its argument; 001F's control A5 already proved component-lookup errors are detected in the
assembly that feeds it.

No provably-null mutation was counted as detected.

## 5. CUDA diagnostics

`cuda-diagnostics/` — memcheck, initcheck, synccheck, racecheck: **0 errors** each over `--quick`.
Every run's output was checked to contain `MOMENTUM RESPONSE EQUIVALENCE: PASS`, so none was
vacuous. `-fmad=false` now covers seven kernels.

## 6. Operator regressions

Re-run after this phase, all unchanged:

```text
gradients (001B)             132 cases   0 failures
diffusion (001C)             528         0
scalar convection (001D)    1684         0
momentum convection (001D) 10352         0
boundary conditions (001E)    75         0
momentum assembly (001F)   27744         0
momentum response (001G)     260         0
```

## 7. Files

```text
new
  include/cfd/gpu/DeviceMomentumResponse.hpp        entry point
  cuda/kernels/DeviceMomentumResponseKernel.cu      one kernel (-fmad=false)
modified
  cuda/CMakeLists.txt                               source + the no-FMA property
```

No CPU production file was modified. The implementation is deliberately free-standing and small so
the Rhie–Chow work can call it directly.

## 8. What is NOT started

Rhie–Chow / predicted face flux, pressure-correction assembly, velocity correction, face-flux
correction, SIMPLE integration, GPU-PIPE-001 residency.

## 9. Environment

```text
GPU     NVIDIA RTX 5000 Ada Generation Laptop GPU, compute capability 8.9, driver 580.97
CUDA    nvcc 12.9.86, architectures 80;89
host    g++ 11.4.0 (Ubuntu 22.04, WSL2), CMake 3.22.1
build   Release, -O3 -DNDEBUG, CFDAPP_ENABLE_CUDA=ON; seven operator kernels also -fmad=false
```
