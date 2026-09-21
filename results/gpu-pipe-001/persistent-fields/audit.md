# GPU-PIPE-001 Persistent Fields — Phase A: residency audit

Every field a production GPU SIMPLE solve touches, who owns it, and what crosses the PCIe boundary
per outer iteration. **Measured, not estimated** — `tools/transfer_probe.cpp` fits
`calls(n) = setup + perIteration · n` from runs at 4, 8 and 16 outer iterations and checks the fit
is linear.

## 0. The measurement

`transfers/baseline.log`, 160² lid-driven cavity, 25,600 cells / 51,520 faces.

```text
arm                                     H2D/iter   H2D bytes/iter   D2H/iter   D2H bytes/iter
gpu-disc  (GPU solver + GPU disc)          21.0        6,950,384     ~1156*      ~10,572,597*
disc-only (CPU solver + GPU disc)          12.0        2,664,960       16.0        8,368,664
gpu-pipe  (GPU solver + CPU disc)           9.0        4,285,424     ~1140*       ~2,203,933*

setup allocations 371 (gpu-disc) / 345 (disc-only) / 26 (gpu-pipe)
per-iteration allocations  0        per-iteration reallocations  0
one cell-field 204,800 B     one face-field 412,160 B
```

`*` The two GPU-**solver** arms are deliberately marked non-linear: their D2H is dominated by
BiCGSTAB reduction round trips, which scale with Krylov iterations, not outer iterations. The probe
detects and reports that rather than fitting a straight line through it. **`disc-only` is therefore
the clean view of FIELD traffic** — same discretization, no reduction noise, and it fits exactly.

## 1. Field-by-field map

Sites are in `cuda/kernels/GpuSimpleDiscretizationCuda.cpp` unless noted. "Per-iteration transfers"
counts **full-field** crossings only.

| field | host owner | device owner | allocation site | upload site | download site | lifetime | read/write stages | required host consumers | required device consumers | per-iteration transfers | target residency |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| **velocity** U,V,W | `SIMPLE::solve` local `velocity` | `Impl::velocity` | `beginIteration` resize | `beginIteration` ×3 | — | whole solve | momentum assembly, Rhie–Chow, BC eval | `allFinite`, final `result.velocity` | momentum, convection, flux | **3 H2D** | **device**, carried from `correctedVelocity` |
| **pressure** | local `pressure` | `Impl::pressure` | `beginIteration` | `beginIteration` ×1 | — | whole solve | pressure gradient, Rhie–Chow | `allFinite`, final `result.pressure` | gradient, predicted flux | **1 H2D** | **device**, updated on device |
| **massFlux** | local `massFlux` | `Impl::massFlux` | `beginIteration` | `beginIteration` ×1 | — | whole solve | momentum convection | continuity, final `result.massFlux` | convection, assembly | **1 H2D** | **device**, carried from `correctedFlux` |
| **effectiveViscosity** | `activeModel->effectiveViscosity()` | `Impl::viscosity` | `beginIteration` | `beginIteration` ×1 | — | whole solve | diffusion | none | diffusion | **1 H2D** | **device**, re-upload only when the model changes it |
| velocityStar U,V,W | `uResult.solution` etc. | `Impl::velocityStar` | `setMomentumSolution` | `setMomentumSolution` ×2/3 | — | one iteration | response, flux, correction | none | correction, flux | **2–3 H2D** | host-forced — the solver is on the host |
| pPrime | `pResult.solution` | `Impl::pPrime` | `setPressureCorrection` | ×1 | — | one iteration | corrections, pressure update | pressure update (host today) | both corrections | **1 H2D** | host-forced this gate |
| momentum systems U,V,W | `MomentumAssembly` | `Impl::systemU/V/W` | `assembleMomentum` | — | `assembleMomentum` returns host `LinearSystem` | one iteration | assembly → solver | the linear solver | response coefficients | **3 D2H (systems)** | host-forced — **next gate** |
| pressure system | `PressureCorrectionAssembly` | `Impl::pressureSystem` | `assemblePressureCorrection` | — | returns host `LinearSystem` | one iteration | assembly → solver | the linear solver | flux correction (`faceCoefficient`, `explicitFaceFlux` stay resident) | **1 D2H (system)** | host-forced — **next gate** |
| responseU/V/W | — | `Impl::responseU/V/W` | `computeResponseCoefficients` | — | — | one iteration | response → flux, corrections | none | Rhie–Chow, corrections | **0** — already resident | unchanged |
| predictorFlux | — | `Impl::predictorFlux` | `computePredictedFaceFlux` | — | — | one iteration | flux → pressure assembly, flux correction | none | assembly, correction | **0** — already resident | unchanged |
| gradPx/y/z | — | `Impl::gradPx/y/z` | `computePredictedFaceFlux` | — | — | one iteration | pressure gradient for Rhie–Chow | none | Rhie–Chow | **0** — already resident | unchanged |
| correctionGradX/Y/Z | — | `Impl::correctionGrad*` | `correctVelocity` | — | — | one iteration | p' gradient | none | velocity correction | **0** — already resident | unchanged |
| correctedVelocity | `velocityNew` | `Impl::correctedVelocity` | `correctVelocity` | — | `correctVelocity` ×3 | one iteration | correction output | `allFinite`, carried to next iteration | next iteration's velocity | **3 D2H** | **device**; download only when the host genuinely needs it |
| correctedFlux | `fluxNew` | `Impl::correctedFlux` | `correctFaceMassFlux` | — | ×1 | one iteration | correction output | **`evaluateContinuity`**, carried | next iteration's massFlux | **1 D2H** | **stays a download** — see §3 |
| previousU/V/W | `previousU` etc. | `Impl::previousU/V/W` | `assembleMomentum` | inside `assembleMomentum` | — | one iteration | under-relaxation RHS | none | momentum assembly | folded into assembly | unchanged |
| previousPPrime | — | `Impl::previousPPrime` | non-orthogonal passes | — | — | one iteration | pressure passes | none | pressure assembly | **0** | unchanged |
| device mesh, BC encodings, CSR patterns, plans | — | the five plan objects | `prepare()` | `prepare()` | — | **whole case** | every stage | none | every stage | **0** — already once-per-case | unchanged |

### The 2D/3D contract, already established and to be preserved

* `Vector2` is an alias of `Vector3` with `z` value-initialised to exactly `+0.0`.
* On a 2D mesh the velocity-gradient path leaves `gradW` **empty** — GPU-DISC-001D's initcheck
  incident. `responseW`, `systemW`, `previousW` are likewise unused in 2D.
* `correctVelocity` writes `outZ = 0.0` in 2D rather than carrying the predictor's z.

Persistent storage must not start allocating W/gradW data in 2D.

## 2. What is avoidable, precisely

Of the **21 H2D calls per outer iteration** on the production `gpu-disc` path:

```text
 6  beginIteration re-uploads      velocity.x, velocity.y, velocity.z, pressure,
                                   viscosity, massFlux              <- AVOIDABLE
 3  setMomentumSolution            forced: the linear solver is on the host
 1  setPressureCorrection          forced: same
11  the GPU linear solver's own    matrix + rhs + x0 per solve       <- NEXT GATE
```

**Those 6 uploads are avoidable because the device produced 5 of them itself:**

| re-uploaded field | where the host value came from |
| --- | --- |
| velocity ×3 | `correctVelocity` downloaded it from the device last iteration, and `SIMPLE.cpp` assigned `velocity = velocityNew` unchanged |
| massFlux | `correctFaceMassFlux` downloaded it last iteration; `massFlux = fluxNew` unchanged |
| pressure | the host loop `pressure[c] + alpha·pPrime[c]` — and **both operands are already device-resident** |
| viscosity | `LaminarModel::effectiveViscosity` returns `mu` everywhere, **constant for the whole solve** |

So the device downloads a field, the host stores it unmodified, and the device uploads the identical
bytes back one iteration later. That round trip is the thing this gate removes.

At 160²: 6 calls × 1,640,960 bytes per iteration — **61.6% of the field-path H2D bytes** (measured
on `disc-only`, where field traffic is not mixed with reduction traffic).

## 3. What is NOT avoidable, and why — stated rather than optimistically assumed

**`evaluateContinuity(mesh, fluxNew)` genuinely requires the full face field on the host.**
`SIMPLE.cpp` consumes only two scalars from it (`rms(cellImbalance)` and `abs(globalNetFlux)`), so
a device reduction is the obvious idea. It is rejected **for this gate**:

> GPU-DISC-001 established **bitwise** CPU/GPU equivalence, and `rms` is a sequential left-to-right
> sum over cells. A parallel tree reduction produces a different rounding and would change the
> residual history — breaking `full_solve_equivalence`, which compares whole histories bitwise.
> Reproducing the CPU's summation order on the device means a serial device loop, which at 409,600
> cells is far slower than the 412 KB copy it would replace.

So the corrected flux keeps its D2H. That is one face-field per iteration and it is honest cost, not
an oversight. The same argument applies to `allFinite`, except that `allFinite` is a **predicate**,
not a value feeding the solution — a device reduction there changes no number, so it is available if
the velocity download is deferred.

**The linear systems' D2H is not this gate's.** `assembleMomentum` and `assemblePressureCorrection`
return host `LinearSystem`s because `LinearSolver` is a host interface. Removing that round trip is
exactly the definition of `GPU-resident pressure solve`, the next unchecked item. It dominates D2H
bytes and is deliberately left alone.

## 4. Ownership contract to implement

Today the device state is **implicitly** re-established from the host every iteration, which is why
no dirty-state model exists: there is nothing to track when you re-upload unconditionally. Making
fields persist requires making authority explicit.

```text
FieldAuthority:
  HostOnly      the device copy does not exist or is known stale
  DeviceOwned   the device copy is authoritative; the host copy is stale
  Synchronized  both copies hold the same bytes
```

Transitions, per persistent field:

| operation | effect |
| --- | --- |
| `uploadInitialState(...)` | HostOnly → Synchronized |
| a device stage writes the field | → **DeviceOwned** (host copy now stale) |
| `downloadX(out)` | DeviceOwned → Synchronized, and fills `out` |
| host mutates the field during a solve | **unsupported** — the contract forbids it, and it is enforced rather than silently tolerated |
| mesh/case change | every field → HostOnly, buffers invalidated |
| backend change | device state dropped; the CPU path never consults it |

Host access stays **explicit**: a caller that wants a field calls a download, and a download of a
`Synchronized` field performs no transfer. Nothing is silently stale behind an accessor that
promises current data.

## 5. Planned change, and what it must not touch

```text
new     cuda/kernels/DevicePersistentFieldsKernel.cu   device-side pressure update and
                                                       device-to-device field carry
extend  include/cfd/gpu/GpuSimpleDiscretization.hpp    authority model + explicit downloads
extend  cuda/kernels/GpuSimpleDiscretizationCuda.cpp   persistence, carry, authority
extend  src/gpu/GpuSimpleDiscretization.cpp            the CPU-only stub keeps parity
edit    src/pressure_velocity/SIMPLE.cpp               GPU branch only
edit    cuda/CMakeLists.txt                            add the new kernel to -fmad=false
```

**The pressure update must move to the device to keep `pressure` resident, and it must be bitwise
identical to the host loop** `pressure[c] + (alpha * pPrime[c])`. That expression is precisely the
`c + a*b` form nvcc contracts into an FMA by default, and `DeviceVectorOpsKernel.cu` is **not** in
the `-fmad=false` list — so reusing its `axpy` would silently change the answer. The new kernel gets
its own translation unit **in** that list.

Untouched: every operator kernel qualified by GPU-DISC-001, the CPU discretization path, the CPU
branch of `SIMPLE::solve`, every tolerance and every convergence criterion.
