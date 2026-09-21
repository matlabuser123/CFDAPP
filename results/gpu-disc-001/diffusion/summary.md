# GPU-DISC-001C — CUDA diffusion

**Result: PASS.** The CUDA implicit diffusion assembly is **bitwise identical** to the CPU
reference on every case run — 528 differential cases across 11 meshes, 3 boundary-condition sets,
4 fields, 2 diffusivity kinds and both settings of the non-orthogonal flag. The sparsity pattern
matches exactly. No tolerance was used anywhere in this gate.

```text
differential        528 cases   0 failures   bitwise (memcmp) on values, RHS and pattern
sparsity pattern    0 mismatches across all 528 cases
non-vacuity         PASS        a one-ULP perturbation is detected by the same comparator
path coverage       PASS        every branch reached; harness fails if not
negative controls   4/4 PASS    detected, restored, rebuilt, freshness verified, re-passed
compute-sanitizer   4/4 clean   memcheck, initcheck, synccheck, racecheck -- 0 errors
device residency    0 D2H       zero device-to-host copies per assembly
```

## 1. What was ported, and what deliberately was not

There are two diffusion implementations in the codebase. `audit.md` §1 establishes which is
production:

* **Ported** — the implicit, matrix-assembling path: `internalFaceDiffusionTerms` /
  `boundaryFaceDiffusionTerms` (`NonOrthogonalDiffusion.cpp`), the single shared implementation
  behind momentum viscous, thermal conduction, species and turbulence diffusion, driven by
  `turbulence::assembleScalarDiffusionContribution`. This is the operator that produces a diagonal,
  off-diagonals and an RHS, supports a spatially varying diffusivity, and is what momentum assembly
  (001F) will consume.
* **Not ported** — `cfd::discretization::diffusion()`, the explicit operator returning
  `div(Gamma grad phi)` as a field. It has **no production caller** in `src/` or `apps/` (recorded
  in `results/p12-mesh-005/summary.md`), and it is where the MESH-005 two-cells-across defect lives.

## 2. Differential — the gate

`differential/differential.log`

Meshes: `cartesian2d 16`, `cartesian2d 40`, `graded2d 16` (stretched), `distorted q16`,
`q16 translated`, `sheared 0.35`, `multiblock L` (3-block, non-rectangular), `two-cell 2x8`,
`cartesian3d 8`, `planar skew 3d 6`, `warped 3d 6`.

Per mesh: {Dirichlet, Neumann, mixed} × {constant, linear, quadratic, manufactured} ×
{uniform Gamma, spatially varying Gamma} × {correction off, on} = 48 cases.

Compared, separately, exactly as the brief asks:

| quantity | result |
| --- | --- |
| sparsity pattern (`rowOffsets`, `columnIndices`) | identical in all 528 cases |
| matrix **diagonal** | 0 differing, maxAbs 0, maxRel 0 |
| matrix **off-diagonal** | 0 differing, maxAbs 0, maxRel 0 |
| **RHS** contribution | 0 differing, maxAbs 0, maxRel 0 |

Each line also prints the largest `|reference|` seen (`scale`), so a pass is visibly over
non-trivial numbers — diagonals 4.2–11.9, RHS 0.86–19.4 — not a field of zeros.

`SparseMatrixBuilder::build()` drops an entry whose accumulated value is exactly 0.0, so the CPU's
pattern is value-dependent. The harness checks the patterns for equality rather than assuming
diffusion never produces an exact zero; **0 mismatches** in 528 cases confirms it does not, but the
check is what makes that a result rather than an assumption.

### Path coverage

```text
P12-DIFF-002 boundary reconstruction  528 cases
correctable internal faces            528
non-orthogonal correction enabled     264
spatially varying diffusivity         264
CPU/GPU sparsity pattern mismatches     0
```

The harness **fails** if any branch count is zero. The non-orthogonal flag is shown to matter
rather than assumed to: on `distorted q16` the diagonal scale moves 5.667 → 5.669 and the RHS
7.317 → 7.295 between `corr=off` and `corr=on`, so a GPU path that ignored the flag would be
caught — and NC2 confirms it is.

## 3. Negative controls

`negative-control/` — each follows inject → build → **verify detected** → restore → **rebuild** →
sha256 match → `ninja` freshness → **differential passes again**. No mutant binary survives.

| control | mutation | detected |
| --- | --- | --- |
| NC1 | face gradient divides by `(dPf+dNf)` instead of multiplying by its reciprocal | 119/528 cases fail — **maxAbs 6.5e-19** |
| NC2 | internal-face correction applied regardless of the `enabled` flag | 120/528 fail, diag maxAbs 0.185, off-diag 0.048 |
| NC3 | neighbour row adds the explicit flux instead of subtracting it | 120/528 fail, RHS maxAbs 0.71 |
| NC4 | `-fmad=false` removed from the diffusion kernel | 468/528 fail, RHS maxAbs 1.78e-15 |

NC1 is the one worth dwelling on. The two `interpolateInternalFace` overloads are genuinely
different expressions — the scalar form divides, the vector form multiplies by the reciprocal — and
diffusion uses **both**, the scalar for `gammaFace` and the vector for the face gradient. Confusing
them produces a discrepancy of **6.5e-19**, which no tolerance-based comparison would ever flag.
The bitwise gate catches it. That is the concrete argument for requiring exact equality here rather
than "within existing tolerances".

NC4 proves the bitwise result **depends on** disabling FMA contraction rather than holding by
accident. The flag is scoped to exactly two translation units — verified in the generated
`build.ninja`: `DeviceGradientKernel.cu` and `DeviceDiffusionKernel.cu`, no others.

## 4. CUDA diagnostics

`cuda-diagnostics/`

```text
memcheck    errors=0
initcheck   errors=0
synccheck   errors=0
racecheck   errors=0
```

Run over `--quick` (distorted q16 + warped 3d 6), which between them reach every branch in 2D and
3D. Each run's own output was checked to contain `DIFFUSION EQUIVALENCE (quick): PASS`, so none of
the four was vacuous.

## 5. How the verified CUDA gradient is reused

The production assembler **always** builds a gradient — P12-DIFF-002 A2 deliberately decoupled the
Dirichlet wall-flux scheme from the non-orthogonal flag — so the port has a hard dependency on one,
and it is the operator qualified in 001B:

* `DeviceDiffusionPlan` **embeds** a `DeviceGradientPlan` rather than rebuilding any geometry, so
  the boundary encodings, skew data and P12-GRAD-002 claim data are shared, not duplicated;
* `assembleScalarDiffusionDevice` calls `greenGaussGradientDevice(...)` on the device-resident
  field with `kGreenGaussSkewCorrectionSweeps`, and the gradient stays on the device;
* the kernels index `gradX/gradY/gradZ` directly — no download, no second gradient implementation.

`GradientScheme::LeastSquares` is **not** ported. Requesting it makes `build()` return false with a
reason rather than silently substituting GreenGauss.

One consequence worth recording: extracting the boundary-condition encoder into
`include/cfd/gpu/BoundaryEncoding.hpp` so both plans share one implementation touched
001B's qualified `DeviceGradientPlan.cpp`. 001B's full 132-case bitwise differential was re-run
after the move — **132/132, still PASS** — so the move is verified, not assumed, to be behaviour
preserving.

## 6. The MESH-005 two-cell defect — not fixed, not reproduced, not silently sidestepped

TODO.md records `Two-cell diffusion() defect`. It lives in `Diffusion.cpp`'s 4-point cubic path,
where `nextInteriorFaceAwayFrom` accepts a **perpendicular** face when the mesh is only two cells
across, so the fit's far point is a transverse cell. `results/p12-mesh-005/summary.md` §28 measures
it: `laplacian(x²+y²)` gives 2.1667 where the exact value is 4.

That function is in the **explicit** operator, which this phase does not port. So the defect is:

* **not fixed** — `src/discretization/Diffusion.cpp` is untouched;
* **not reproduced on the GPU** — the defective code is not in the ported path;
* **not hidden** — a `two-cell 2x8` mesh is in the differential, and CPU and GPU agree bitwise on
  it through the implicit path (48 cases). That is *not* a fix for MESH-005 and makes no claim
  about the explicit operator.

The implicit path's own boundary treatment is different: `boundaryInwardStencil` returns
`valid = false` rather than reaching for a transverse cell, and the assembler falls back to the
documented two-point form.

## 7. Design notes that decided the result

* **Accumulation order is part of the answer.** `SparseMatrixBuilder::build()` stable-sorts by
  `(row, column)`, so repeated triplets keep insertion order — which is **face-id order**. The
  kernel gathers each row over its incident faces sorted by face id, not in `cell.faceIds()` order,
  which would have been an unchecked guess. This matters concretely: a boundary face's far-cell
  entry targets a column an internal face also writes, so that entry really does accumulate twice.
* **Gather, not scatter.** Every write to row P comes from a face incident to P (confirmed:
  `boundaryInwardStencil`'s `farCell` is the cell across the owner's opposite interior face, hence
  already a neighbour). So there are no atomics and no ordering nondeterminism.
* **No synchronization, no transfers.** One kernel launch per assembly, on the default stream; the
  CSR pattern is copied device-to-device from the plan; the result is left device-resident. The
  differential asserts 0 device-to-host copies per assembly.

## 8. Environment

```text
GPU        NVIDIA RTX 5000 Ada Generation Laptop GPU, 15352 MiB, compute capability 8.9
driver     580.97
CUDA       nvcc 12.9.86, architectures 80;89
host       g++ 11.4.0 (Ubuntu 22.04, WSL2), CMake 3.22.1
build      CMAKE_BUILD_TYPE=Release, -O3 -DNDEBUG, CFDAPP_ENABLE_CUDA=ON
           DeviceGradientKernel.cu and DeviceDiffusionKernel.cu additionally -fmad=false
```

## 9. Commands

```text
cmake -S . -B build/cuda && cmake --build build/cuda -j 8
g++ -std=c++20 -O2 -DNDEBUG -I include -I build/cuda/generated/include \
    -I /usr/local/cuda-12.9/include results/gpu-disc-001/diffusion/tools/diffusion_equivalence.cpp \
    -o /tmp/diffusion_equivalence -Wl,--start-group build/cuda/src/libcfdcore.a \
    build/cuda/cuda/libcfdcuda.a -Wl,--end-group -L/usr/local/cuda-12.9/lib64 -lcudart
/tmp/diffusion_equivalence                       # the gate
/tmp/diffusion_equivalence --quick               # sanitizer target
compute-sanitizer --tool {memcheck,initcheck,synccheck,racecheck} /tmp/diffusion_equivalence --quick
ctest --test-dir build/cuda -j 4                 # regression
```

## 10. Files

```text
include/cfd/gpu/DeviceDiffusion.hpp       plan, system, entry point
include/cfd/gpu/BoundaryEncoding.hpp      shared BC encoder (moved out of the gradient plan)
cuda/kernels/DeviceDiffusionPlan.cpp      host plan builder
cuda/kernels/DeviceDiffusionKernel.cu     assembly kernel (-fmad=false)
cuda/CMakeLists.txt                       sources + the no-FMA property
cuda/kernels/DeviceGradientPlan.cpp       encoder extracted; 001B gate re-run, 132/132
include/cfd/gpu/DeviceGradient.hpp        encoding constants now alias the shared ones
```

Evidence:

```text
audit.md                  Phase A -- the CPU path, and which of the two is production
differential/             the gate log
negative-control/         four controls, mutated and restored runs
cuda-diagnostics/         four sanitizer logs
regression.log            full suite
tools/                    harness source
```

## 11. What is NOT claimed

* The **explicit** `diffusion()`/`laplacian()` operator is not ported and is unchanged.
* Convection, boundary-condition integration, momentum assembly and pressure correction are
  untouched. Nothing here is an end-to-end solve result.
* No performance claim is made for diffusion. Correctness was the gate; the operator benchmark
  belongs to GPU-DISC-001's own performance qualification at the end of the sequence.
* Vector-valued boundary conditions (`VectorBoundaryCondition`, needed by momentum) are not
  covered. A mesh whose diffusion plan builds says nothing about them.
