# GPU-DISC-001E — reusable CUDA boundary-condition layer

**Result: PASS.** One shared device BC layer, qualified in isolation against the CPU, with all
eleven production boundary-condition types covered and all four already-qualified operators
re-verified unchanged.

```text
isolated differential   75 cases   0 failures   bitwise (memcmp)
                        25,320 scalar comparisons
BC types covered        11 of 11
negative controls       6/6 detected, each restored and re-passed
compute-sanitizer       4/4 clean, non-vacuous
integration             gradients 132 | diffusion 528 | scalar convection 1684
                        | momentum convection 10352 -- all still 0 failures
full regression         1998/1998 passed, 0 failed, 387.3 s
                        `ninja: no work to do` both BEFORE and AFTER ctest
```

## 1. What was built

```text
include/cfd/gpu/DeviceBoundaryConditions.hpp   container + __device__ evaluators (header-inline)
cuda/kernels/DeviceBoundaryConditions.cpp      host builder
cuda/kernels/DeviceBoundaryConditionsKernel.cu isolated driver (-fmad=false)
```

The layer holds, per **face id**:

```text
type              one of eleven ids mirroring BoundaryConditionType
prescribesValue   the stored prescribesBoundaryValue classification
scalar encoding   (kind, a, b) at the straight-line owner-to-face distance
alt encoding      (kind, a, b) at a caller-chosen SECOND per-face distance
vector encoding   (kind, constant, unit normal)
distance          the straight-line owner-to-face distance
```

and exposes five `__device__` evaluators — `bcEvaluateScalar`, `bcEvaluateScalarAlt`,
`bcEvaluateVector`, `bcGhostValue`, plus `bcType` / `bcPrescribesValue`. They are header-inline so
every operator compiles the same code rather than each re-implementing `a + b*phi`.

### Reuse, not duplication

The two encoders from earlier phases are **reused as-is**:

```text
include/cfd/gpu/BoundaryEncoding.hpp         scalar: constant | shift | affine   (001B, shared into 001C)
include/cfd/gpu/VectorBoundaryEncoding.hpp   vector: constant | identity | symmetry (001D)
```

Both are already bitwise-verified; a second copy is precisely what would drift. What this phase
**adds** is what none of them exposed:

1. the **type** id — `PressureCorrectionEquation.cpp:252` dispatches on it
   (`if (bc.type() != FixedValue) continue;`), and nothing on the device carried it;
2. `prescribesBoundaryValue` as a stored per-face flag rather than a predicate each operator
   re-derives;
3. a **second scalar encoding at a different distance on the same face** — the gradient's
   oblique-Neumann re-evaluation uses `d·n`, not the straight-line `d`, and the layer now supports
   both without a second container;
4. the ghost/mirror construction as a shared evaluator.

## 2. Isolated differential

`differential/differential.log`

Driven directly — no equation, no operator, no assembly — so a failure localises to the layer.

Meshes: `cartesian2d 8`, `cartesian2d 20`, `distorted q16`, `cartesian3d 5`, `warped 3d 4`.
Fifteen boundary-condition sets per mesh: each of the eleven types alone, a six-type scalar mix, a
five-type vector mix, a Dirichlet+Neumann mix, and a wall+outlet+symmetry mix — so combinations of
different types on one mesh are covered, not just uniform patches.

Compared per boundary face, all bitwise:

| quantity | result |
| --- | --- |
| scalar `boundaryValue` at the straight-line distance | 0 differing |
| scalar `boundaryValue` at the **alternative** distance | 0 differing |
| vector `boundaryValue`, all three components | 0 differing |
| ghost/mirror value (against `upwindBoundaryFaceValue` itself) | 0 differing |
| condition **type** | 0 differing |
| `prescribesBoundaryValue` | 0 differing |

```text
type FixedValue  FixedGradient  Wall  MovingWall  Inlet  Outlet  Symmetry
     FixedTemperature  HeatFlux  Adiabatic  WallOmega          -- 11 of 11 covered
ghost inflow / outflow / zero-flux     1024 / 992 / 1360
faces with an alternative distance     6330
total scalar comparisons               25320
```

The harness **fails** if any of the eleven types is not reached, if any ghost branch is missed, or
if the symmetry/identity vector forms are never exercised.

**One honest gap in coverage:** the scalar **affine** form (`a + b·phi` with `b ∉ {0, 1}`) is
reached by **zero** faces — no condition in the codebase today needs it. The branch exists so a
future affine condition works without a code change, and it is verified by construction (the
encoder only selects it after a bitwise check), but it is **not** exercised by any production
condition and is not claimed to be.

## 3. Negative controls — 6/6 detected

`negative-control/` — each: inject → build → check → restore → rebuild → sha256 → freshness →
re-pass. All six required categories, all detected.

| control | mutation | detected |
| --- | --- | --- |
| B1 | recorded boundary **type** shifted by one | 75/75 cases |
| B2 | vector evaluator swaps the **x and y components** | 15/75, vector d=64 |
| B3 | **ghost mirror** drops the factor of two | 40/75, ghost d=11 |
| B4 | constant vector condition returns zero for x (**wrong wall/inlet value**) | 15/75, vector d=32 |
| B5 | symmetry projection ignores the z term (**3D-only defect**) | 6/75, **only on 3D meshes** |
| B6 | `prescribesBoundaryValue` classification inverted | 75/75 |

B5 is the 2D/3D discrimination the brief asked for, and it behaves exactly as a 3D-only defect
should: undetectable on every 2D mesh (where `n.z == 0` makes the z term vanish anyway), detected
on `cartesian3d 5` and `warped 3d 4`. No provably-null mutation was counted as a detection.

## 4. CUDA diagnostics

`cuda-diagnostics/` — memcheck, initcheck, synccheck, racecheck: **0 errors** each, over
`--quick` (distorted q16 + warped 3d 4). Every run's output was checked to contain
`BOUNDARY CONDITION EQUIVALENCE: PASS`, so none was vacuous.

Interior face entries are `cudaMemset` before the kernel runs rather than left as whatever the
allocator returned — the same class of defect initcheck caught in 001D, avoided by construction
here.

## 5. Integration — the new layer changed nothing

Re-run after the layer was added:

```text
gradients (001B)            132 cases    0 failures
diffusion (001C)            528 cases    0 failures
scalar convection (001D)   1684 cases    0 failures
momentum convection (001D) 10352 cases   0 failures
boundary conditions (001E)   75 cases    0 failures
```

This is a genuine integration check but a weak one by construction, and that should be said: the
four operators still use the encodings they built for themselves, so the layer could not have
altered them. See §6.

## 6. Scope — what this phase deliberately did NOT do

**The four qualified operators were not rewired onto the new layer.** Gradients, diffusion, scalar
convection and momentum convection each build their own plan arrays today, using the same shared
encoders. Moving them onto `DeviceBoundaryConditions` is mechanical, but it would touch four green
gates at once for no functional gain in this phase. The layer is verified to produce exactly the
same encodings those operators build — same encoder functions, same distances, same bitwise
verification — so the rewiring is safe whenever it is done, and the integration gates above are
what would protect it.

Also not started: momentum assembly, pressure-correction assembly, momentum response coefficients,
Rhie–Chow, GPU-PIPE-001 residency.

## 7. Environment

```text
GPU     NVIDIA RTX 5000 Ada Generation Laptop GPU, compute capability 8.9, driver 580.97
CUDA    nvcc 12.9.86, architectures 80;89
host    g++ 11.4.0 (Ubuntu 22.04, WSL2), CMake 3.22.1
build   Release, -O3 -DNDEBUG, CFDAPP_ENABLE_CUDA=ON; the five operator kernels also -fmad=false
```

## 8. Evidence

```text
audit.md                  Phase A -- the eleven types, what each operator consumes, the mapping
differential/             the isolated gate log
negative-control/         six controls, mutated and restored runs
cuda-diagnostics/         four sanitizer logs
regression.log            full suite
regression_freshness.log  freshness before and after ctest, plus source fingerprints
tools/                    harness source
```
