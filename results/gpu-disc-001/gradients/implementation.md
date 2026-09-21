# GPU-DISC-001B Phase 2 — implementation notes

What was built, and the three decisions that decide whether the gate is passable at all.

## 1. Shape

```text
include/cfd/gpu/DeviceGradient.hpp     DeviceGradientPlan + greenGaussGradientDevice
cuda/kernels/DeviceGradientPlan.cpp    host builder -- immutable, mesh+BC dependent
cuda/kernels/DeviceGradientKernel.cu   six kernels -- field dependent only
```

`DeviceGradientPlan::build(mesh, boundaries)` runs once per mesh. It calls the **production**
`MeshGeometry` and boundary functions, never its own geometry formulas, so the device cannot
disagree with the CPU about the mesh. `greenGaussGradientDevice(plan, phi, sweeps, gx, gy, gz)`
then evaluates only what depends on the field, and leaves the result on the device — the
differential asserts **zero** device-to-host copies per evaluation.

Kernel order per sweep, all on the default stream, no `cudaDeviceSynchronize` anywhere (stream
ordering already guarantees what one would buy — GPU-PIPE-001 Phase 3):

```text
K1 interiorFaceValues  K2 boundaryFaceValues   ->  KC claimValues  K3 greenGaussSum
then, per sweep:  K4 skewCorrect  K2b obliqueNeumann  ->  KC claimValues  K3 greenGaussSum
```

`K3` gathers **per cell over that cell's own faces**, in CSR row order, which is exactly
`cell.faceIds()` order. No atomics and no scatter: floating-point addition is not associative, so
the summation order is part of the answer.

## 2. Three things that decided bitwise equality

### 2.1 Fused multiply-add had to be turned off

The CPU library is built for baseline x86-64, which has no FMA instruction, so `a*b + c` there is a
rounded multiply followed by a rounded add. nvcc contracts that into a single FMA by default —
*more* accurate, and therefore a different number. `DeviceGradientKernel.cu` is compiled with
`-fmad=false`, applied to that **one source file** so no already-qualified kernel changes
behaviour. Verified in the generated `build.ninja`: 8 CUDA objects, 1 carrying the flag.

This is load-bearing, and is proven so rather than asserted — negative control NC2 removes the flag
and the differential fails.

### 2.2 `cfd::dot` is not the plain three-term sum

`Vector3.hpp:78` drops the z term entirely when it is exactly zero, so a 2D field never turns `+0.0`
into `-0.0`. The device `dotGuarded` reproduces that branch. Without it, every 2D case with a
non-zero skew or tangential offset would differ in the sign of a zero — a bitwise difference.

### 2.3 A Neumann condition is a shift, not an affine form with b = 1

This is the one that actually failed first, and it is worth recording because the first design was
wrong in a way that looked fine.

The plan encodes each boundary face's `ScalarBoundaryCondition::boundaryValue(phi_P, d)` so the
device can evaluate it. The first encoding was `a + b*phi_P` with

```text
a = boundaryValue(0, d)
b = boundaryValue(1, d) - a
```

For a Neumann condition, `boundaryValue(phi, d) = phi + (g*d)`, so `a = g*d` exactly and `b` *should*
be 1. It is not: `fl(1 + a) - a` differs from 1 whenever `a` carries bits below one ULP of `(1 + a)`,
which is most of the time. Every Neumann case was rejected by the encoder's own bitwise
verification — **32 of 80 cases, failing closed rather than silently producing wrong numbers.**

The fix is to reproduce the condition's *arithmetic*, not just its value in exact arithmetic. Three
forms, each verified bitwise against the condition itself over 11 probe values:

```text
kBoundaryConstant  value = a            FixedValue, FixedTemperature, WallOmega
kBoundaryShift     value = phi_P + a    FixedGradient, Adiabatic, HeatFlux
kBoundaryAffine    value = a + b*phi_P  anything else still affine in phi_P
```

A condition matching none of them marks the mesh **unsupported** and `build()` returns false with a
reason. Nothing is approximated silently.

## 3. What is precomputed, and why that is not a reformulation

Per the audit, almost every hard quantity in this operator is a function of the mesh, not the field:
opposite-face topology, crossing weights, boundary-line intersections, and therefore the winner of
the P12-GRAD-002 claim conflict (`min` by `(antiParallelAlignment, boundaryFaceId)`). Those are
built once on the host and uploaded. Every arithmetic expression is preserved verbatim and in the
same order; only the *timing* of the geometry lookups changes.

Two consequences worth stating plainly:

* The conflict resolution happens on the host, so the device sees at most one claim per
  `(cell, target face)` — the same deterministic winner the CPU picks.
* `slotClaim` maps each CSR `(cell, face)` slot to a claim index, so `K3` substitutes a claimed
  value without searching, preserving the CPU's "claimed value if claimed, else `faceValues[f]`".

## 4. Two predicates are restated, not called

`Gradient.cpp` keeps `obliqueNeumannFace` (P12-MESH-001) and `boundaryTransferNeeded`
(P12-GRAD-002) in an anonymous namespace, so the plan builder restates them expression for
expression. **This is a real drift risk and is not hidden.** It is covered by the differential,
which includes meshes where each predicate is live: `graded2d`, `distorted q16`, `sheared 0.35`,
`planar skew 3d` and `warped 3d` all report non-zero oblique faces under Neumann conditions, and 21
mesh/BC pairs report `boundaryTransfer=yes`. If either restatement drifts, those cases stop being
bitwise equal.

`prescribesBoundaryValue` was already public (`NonOrthogonalDiffusion.hpp`), so it is called, not
copied.

## 5. Sweep count is part of the discretization

`greenGaussGradient` runs its sweep loop only when
`!skewedFaces.empty() || !obliqueFaces.empty() || boundaryTransfer`. Claims alone do **not** trigger
it — on an aligned Cartesian mesh every claim exists but every transfer offset is exactly zero, so
the CPU does a single sweep. `DeviceGradientPlan::sweepsNeeded()` mirrors that condition exactly.
An early version used `claims > 0` instead, which would have run 4 sweeps where the CPU runs 1.

## 6. Deliberately unchanged

* The **Green–Gauss four-sweep limitation** is recorded technical debt. It is **reproduced, not
  fixed**: the device path uses the same `kGreenGaussSkewCorrectionSweeps = 4`.
* `LeastSquares` is a different scheme with its own entry point — out of scope.
* Vector-field gradients (`computeVelocityGradient`) belong to momentum assembly (001F).

## 7. One additive change outside this phase's files

`DeviceMesh` gained `boundaryFaceCount()` — a const accessor returning `boundaryFaceIds_.size()`,
needed to launch the boundary kernel. The buffer and its contents are unchanged from 001A's
qualification.
