# GPU-DISC-001F — CUDA momentum assembly

**Result: PASS.** The CUDA SIMPLE momentum assembly is **bitwise identical** to the CPU assembled
linear system — matrix structure, diagonal, every off-diagonal, RHS and the reported diagonal
vector — across 27,744 cases. No tolerance was used.

```text
differential          27,744 cases   0 failures
bitwise-identical     27,744 of 27,744
max absolute error    0
max relative error    0
sparsity mismatches   0
negative controls     7/7 detected, each restored and re-passed
compute-sanitizer     4/4 clean, non-vacuous
operator regressions  gradients 132 | diffusion 528 | scalar convection 1684
                      | momentum convection 10352 | boundary conditions 75 -- all 0 failures
full regression       1998/1998 passed, 0 failed, 408.3 s
                      `ninja: no work to do` both BEFORE and AFTER ctest
```

Assembly only. No response coefficients, no predicted face flux, no Rhie–Chow, no
pressure-correction assembly, no velocity correction, no SIMPLE integration.

## 1. Decomposition, and what was reused

`audit.md` establishes the production entry point as
`pressure_velocity::assembleRelaxedMomentumComponent` (`RelaxedMomentum.cpp:30`):

```text
momentum assembly = diffusion + convection + pressure source [+ buoyancy] [+ momentum source]
                    + implicit under-relaxation
```

| term | source |
| --- | --- |
| convection face terms | **001D**, shared via `DeviceConvectionTerms.hpp` |
| velocity gradient | **001D** `computeVelocityGradientDevice`, called unchanged |
| pressure gradient | **001B** `greenGaussGradientDevice`, called unchanged |
| diffusion face terms | **001C**, shared via `DeviceDiffusionTerms.hpp` |
| vector boundary values | **001D/001E** `VectorBoundaryEncoding.hpp` |
| device mesh, vector BC encoding, CSR pattern | reused from the 001D convection plan, not rebuilt |
| pressure source, under-relaxation, assembly glue | new |

### Why two headers had to be extracted

The obvious implementation — call 001D's convection kernel, call a diffusion kernel, add the
subtotals — is **wrong**, and the reason is worth recording:

> `SparseMatrixBuilder` sums repeated `(row, column)` entries in **insertion order**, and the CPU
> inserts every diffusion triplet before every convection one. Each entry therefore accumulates
> `d1+d2+…+dk+c1+c2+…+ck` strictly left to right. Adding a convection subtotal to a diffusion
> subtotal computes `(d1+…+dk) + (c1+…+ck)` instead — algebraically identical, **not** bitwise
> identical.

So the momentum kernel walks each row's faces twice, once per contribution, adding into the same
running value. To do that without a second copy of either operator, the face-term implementations
were **extracted** into shared headers:

```text
include/cfd/gpu/DeviceDiffusionTerms.hpp    from DeviceDiffusionKernel.cu   (001C)
include/cfd/gpu/DeviceConvectionTerms.hpp   from DeviceMomentumConvectionKernel.cu (001D)
```

Both donor kernels were rewired onto the shared headers and **re-verified**: diffusion 528/528,
momentum convection 10,352/10,352, both still bitwise. The extractions are proven behaviour-
preserving, not assumed to be.

## 2. Under-relaxation — the part most likely to be quietly wrong

`UnderRelaxation.cpp:13`. Three details are load-bearing and all three are reproduced:

1. **`alpha == 1.0` returns early.** No relaxation triplet is appended at all. Computing
   `extra = diag * 0.0` and adding it would append `+0.0` to every diagonal — usually invisible,
   but a different code path.
2. **`factor = (1.0 / alpha) - 1.0`** — a reciprocal then a subtraction. Rewriting it as
   `(1 - alpha) / alpha` is algebraically identical and **not** bitwise identical. Negative control
   **A4** does exactly that rewrite and is detected, at a discrepancy of **1.39e-17**.
3. **`extra` comes from the diagonal of the UNRELAXED matrix** — after diffusion and convection are
   both summed — and is appended last.

Relaxation factors covered: **α = 1.0, 0.7, 0.3, 0.05**, giving 20,832 relaxed cases alongside the
unrelaxed path.

## 3. Differential

`differential/differential.log`

Meshes: `cartesian2d 10`, `graded2d 10`, `distorted q16`, `cartesian3d 4`, `warped 3d 3`.

| axis | coverage |
| --- | --- |
| components | U 11,560 · V 11,560 · W 4,624 (3D only) |
| convection schemes | upwind / central / linear_upwind / quick — 6,936 each |
| velocity | zero, uniform, linear, nonuniform |
| pressure | uniform, gradient, nonuniform |
| viscosity | low (1.8e-5), high (0.05), spatially varying |
| mass flux | geometric swirl, mixed-sign with exact ±0.0, and reversed |
| boundary conditions | all-Wall and an all-five set (Wall, MovingWall, Inlet, Outlet, Symmetry) on one mesh |
| relaxation | α = 1.0, 0.7, 0.3, 0.05 |
| optional terms | momentum source (6,912 cases), non-orthogonal correction (6,912 cases) |

Compared per case: sparsity structure, matrix **diagonal**, every **off-diagonal**, **RHS**, and the
**diagonal vector** `MomentumAssembly` reports. All `differing = 0`, max abs 0, max rel 0.

The harness **fails** if any scheme, component, or the relaxation / source / non-orthogonal
branches are not exercised.

## 4. Negative controls — 7/7 detected

`negative-control/` — each: inject → build → check → restore → rebuild → sha256 → freshness →
re-pass. All seven categories the brief names.

| control | mutation | detected |
| --- | --- | --- |
| A1 | boundary diffusion diagonal sign reversed | 11560/11560 |
| A2 | pressure-source sign reversed | 11560/11560 |
| A3 | relaxation RHS correction dropped | 8680/11560 |
| A4 | relaxation factor rewritten `(1-α)/α` | 5800/11560, **maxAbs 1.39e-17** |
| A5 | boundary velocity always reads the U component | 3468/11560 |
| A6 | internal diffusion off-diagonal dropped | 11560/11560 |
| A7 | boundary RHS uses the diagonal coefficient instead of the prescribed-value one | 5780/11560 |

No provably-null mutation was counted; all seven are genuine detections.

A4 is the one to note: it is not a bug in any normal sense — the expression is mathematically the
same — and it is caught only because the gate is bitwise. That is the concrete justification for
the audit's instruction not to algebraically rewrite CPU expressions.

## 5. CUDA diagnostics

`cuda-diagnostics/` — memcheck, initcheck, synccheck, racecheck: **0 errors** each over `--quick`
(distorted q16 + warped 3d 3). Every run's output was checked to contain
`MOMENTUM ASSEMBLY EQUIVALENCE: PASS`, so none was vacuous. `-fmad=false` is scoped to exactly six
kernels.

## 6. Scope — what is NOT covered

Recorded plainly rather than implied:

* **PISO's transient momentum** (`assembleTransientMomentumComponent`) is a *different* entry
  point, is 2D-only, and adds `implicitEulerTimeDerivative` + `applyTransientTerm`. Not ported.
* **Compressible momentum** uses the **constant**-viscosity diffusion overload, which takes
  `muFace = dynamicViscosity` on every face instead of interpolating a field. That is a genuinely
  different expression, so passing a uniform field would not reproduce it bitwise. Not ported.
* **Buoyancy** is not exercised as a separate term: it is a pure RHS addition applied at the same
  point as the momentum source, so the device has one path for both and the differential drives it
  through `momentumSource`. `assembleBuoyancySourceContribution` itself is not compared.
* Momentum response coefficients, Rhie–Chow, pressure correction, velocity correction, SIMPLE
  integration, GPU-PIPE-001 residency: not started.

## 7. Files

```text
new
  include/cfd/gpu/DeviceMomentumAssembly.hpp        plan, system, entry point
  include/cfd/gpu/DeviceDiffusionTerms.hpp          shared diffusion face terms (from 001C)
  include/cfd/gpu/DeviceConvectionTerms.hpp         shared convection face terms (from 001D)
  cuda/kernels/DeviceMomentumAssemblyPlan.cpp       host plan builder
  cuda/kernels/DeviceMomentumAssemblyKernel.cu      assembly kernel (-fmad=false)

modified (extraction + accessors only; both gates re-verified)
  cuda/kernels/DeviceDiffusionKernel.cu             now calls the shared terms
  cuda/kernels/DeviceMomentumConvectionKernel.cu    now calls the shared terms
  include/cfd/gpu/DeviceMomentumConvection.hpp      read-only accessors for reuse
  include/cfd/gpu/DeviceConvection.hpp              kVelocityU/V/W moved here (no cycle)
  cuda/CMakeLists.txt                               sources + the no-FMA property
```

No CPU production file was modified.

## 8. Environment

```text
GPU     NVIDIA RTX 5000 Ada Generation Laptop GPU, compute capability 8.9, driver 580.97
CUDA    nvcc 12.9.86, architectures 80;89
host    g++ 11.4.0 (Ubuntu 22.04, WSL2), CMake 3.22.1
build   Release, -O3 -DNDEBUG, CFDAPP_ENABLE_CUDA=ON; six operator kernels also -fmad=false
```
