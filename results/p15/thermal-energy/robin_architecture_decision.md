# P15 — where the Robin (convective) boundary condition belongs

A focused architecture audit before changing anything, as instructed. Measured, then decided.

---

## The problem, stated exactly

```
-k dT/dn = h_c (T_w - T_inf)
```

Eliminating `T_w` against the one-sided conduction estimate:

```
T_w = ( (k/d) T_P + h_c T_inf ) / ( k/d + h_c )
```

The face value depends on the **local conductivity**. `ScalarBoundaryCondition::boundaryValue(
ownerValue, normalDistance)` is not given it and cannot be: `k` varies per cell, and after P15 it
varies with temperature. A BC caching its own `k` would silently disagree with the material model
the moment `k(T)` moved.

## What the codebase actually contains

```text
scalar BC implementations                                  7
vector BC implementations                                  5
call sites touching boundaryValue()                       51  across 22 files
device BC representation                   BoundaryEncoding.hpp: PROBES boundaryValue()
                                           and fits constant / shift / affine
Gamma referenced in the device BC encoding                 0
```

The device encoding is the load-bearing discovery. `encodeBoundaryCondition` evaluates
`bc.boundaryValue(phi, d)` at eleven probe points and fits `a + b*phi`, returning `false` when the
condition is not affine. **Robin *is* affine in `T_P`** — so if `boundaryValue` had `k`, the
existing device path would handle it unchanged.

But the plan is built from **mesh and boundaries only**, before any `Gamma` field exists, and
`Gamma` then varies per cell and per outer iteration. An affine encoding would have to be rebuilt
whenever `k` changed, which is a change to the device plan's lifetime model, not just its
signature.

---

## The three options, assessed against that

### Option A — the BC contributes directly to the matrix

`(a_P, b_P)_f = BC(phi_P, Gamma_f, geometry, face, time)`

| consequence | |
|---|---|
| generic transport | would work |
| existing scalar BCs | all 7 need a new method |
| vector BCs | 5 more, for symmetry that buys nothing |
| **CUDA** | **breaks the model entirely** — a device kernel cannot call a host virtual, and the probe-and-fit encoding has nothing to probe |
| test burden | every BC, every assembler |

**Rejected.** It inverts the split `BoundaryCondition.hpp` states ("BCs specify the mathematical
information; discretization operators decide how it modifies the matrix") and it has no device
story.

### Option B — richer boundary evaluation context

`boundaryValue(BoundaryContext{phi_P, Gamma_f, d, face, time})`

| consequence | |
|---|---|
| generic transport | would work, and Robin becomes reusable by species and turbulence |
| existing scalar BCs | 7 signature changes; 51 call sites to audit |
| vector BCs | changed only if symmetry is forced — it need not be |
| **CUDA** | encoding still fits affine, **but** the plan must rebuild whenever `Gamma` changes, which it currently never does |
| test burden | large: every BC, the device encoder, every plan |

**Right long-term answer, wrong milestone.** It is the design that makes Robin available to every
scalar equation, and P15 is not the phase to change a base-class signature used by 12
implementations and 51 call sites while also delivering thermal physics.

### Option C — thermal-specific assembly handling  ← **CHOSEN**

The BC carries only the physics (`h_c`, `T_inf`); `assembleThermalBoundaryFaceContribution`
computes the coefficient from the face's own `k`, which it already has in scope.

```
G   = 1 / (d/k + 1/h_c)        series: wall conduction, then film
a_P += G A_f
b_P += G A_f T_inf
```

| consequence | |
|---|---|
| generic transport | **Robin unavailable there** — the one real cost, stated not hidden |
| existing scalar BCs | untouched |
| vector BCs | untouched |
| CUDA | untouched, **except the defect below, which must be fixed** |
| species / turbulence | unaffected; they do not need Robin today |
| API stability | preserved |
| test burden | the thermal boundary path only |

**Chosen because it is the smallest design that is mathematically correct**, and because the
conductivity dependence is handled at the layer that owns the conductivity rather than smuggled
into one that does not.

---

## A defect this BC introduces, found by the audit

`encodeBoundaryCondition` calls `bc.boundaryValue(0.0, distance)` **unconditionally**. Since
`ConvectiveHeatTransfer::boundaryValue` throws by design, building a GPU diffusion or convection
plan on a mesh carrying a convective wall would **throw out of the plan constructor** instead of
returning `false` with a reason — the graceful "this configuration is not GPU-supported" path that
every other unsupported condition takes.

That is a regression in GPU robustness caused by adding the BC, and it is in scope to fix: the
encoder must recognise the type and report it unsupported, not propagate an exception.

```text
before   GPU plan build on a convective wall   -> throws InvalidArgumentError
after    GPU plan build on a convective wall   -> returns false, reason names the condition
```

## Consequences recorded for later phases

* **P16/P18 do not need Robin.** Turbulence and species have no convective-wall requirement today,
  so Option C costs them nothing now.
* **When a second equation needs Robin, do Option B**, not another copy of Option C. The moment a
  second module needs a Γ-dependent boundary, the duplication argument that drove P14 applies here
  too.
* **The device plan lifetime is the real blocker for Option B**, not the signature. Anyone
  attempting it should start there.
