# GPU-PIPE-001 Persistent Fields — the ownership contract

Who owns each production field during a GPU SIMPLE solve, when each side is stale, and what a host
read costs. Stated as a contract because the alternative — an implicit dual-authoritative state —
is exactly what makes residency bugs invisible.

## 1. The authority model

```cpp
enum class FieldAuthority { HostOnly, DeviceOwned, Synchronized };
enum class PersistentField { Velocity, Pressure, MassFlux, Viscosity };
FieldAuthority authority(PersistentField) const noexcept;   // queryable, and tested
```

| state | meaning |
| --- | --- |
| `HostOnly` | no device copy exists, or the one that exists is known stale. Nothing may read it on the device. |
| `DeviceOwned` | the device copy is authoritative. **Any host copy is stale** and must not be presented as current. |
| `Synchronized` | both copies hold the same bytes. |

There is no fourth state and no "probably fine". A field is in exactly one of these at all times,
and `authority()` will say which.

## 2. Transitions

| operation | Velocity | Pressure | MassFlux | Viscosity |
| --- | --- | --- | --- | --- |
| `prepare(mesh, …)` | HostOnly | HostOnly | HostOnly | HostOnly |
| `uploadInitialState(v, p, f, mu)` | Synchronized | Synchronized | Synchronized | Synchronized |
| `beginIterationResident()` | unchanged | unchanged | unchanged | unchanged |
| `setViscosity(mu)` | — | — | — | Synchronized |
| `updatePressure(alpha)` | — | **DeviceOwned** | — | — |
| `correctVelocityResident(…)` | **DeviceOwned** | — | — | — |
| `correctFaceMassFluxResident(…)` | — | — | **DeviceOwned** | — |
| `downloadVelocity(out)` | Synchronized | — | — | — |
| `downloadPressure(out)` | — | Synchronized | — | — |
| `downloadMassFlux(out)` | — | — | Synchronized | — |

`beginIterationResident()` deliberately changes nothing and transfers nothing: the corrections and
the pressure update already left their results in the persistent fields. It exists so that "a new
outer iteration begins from resident state" is an explicit statement in the code rather than an
absence, and it **throws** if `uploadInitialState` never ran.

## 3. What marks each side dirty

* **The device is dirtied** by exactly three operations: `updatePressure`,
  `correctVelocityResident`, `correctFaceMassFluxResident`. Each sets its field to `DeviceOwned`.
* **The host is dirtied** by nothing, because **host-side mutation of a persistent field during a
  solve is unsupported**. There is no API that accepts one. That is enforced by absence rather than
  by a runtime check, and the dirty-state suite asserts the absence rather than pretending support
  exists.
* **Everything is dropped** when a new `GpuSimpleDiscretization` is prepared: a fresh object starts
  `HostOnly` on every field, so no state can survive into an incompatible case.

## 4. When a copy is required

A host read is always an explicit call. There is no accessor that silently returns a field.

```text
downloadVelocity / downloadPressure / downloadMassFlux
    DeviceOwned   -> one D2H, then Synchronized
    Synchronized  -> still copies into the caller's field, because the caller
                     supplied a fresh output object and asked to have it filled
    HostOnly      -> refuses (requireResident throws and names the missing state)
```

**This is deliberately NOT a caching layer.** The brief allows one; the architecture does not need
one. A `Synchronized` re-read still copies because the caller passed an empty field to fill, and
building a cache to avoid that would add invalidation logic — the thing most likely to go wrong —
to save a copy that production never performs twice. The dirty-state suite records the actual
behaviour rather than claiming a caching property the code does not have.

## 5. Who consumes what, during a solve

| field | device consumers | host consumers during the solve | when the host copy is refreshed |
| --- | --- | --- | --- |
| **Velocity** | momentum assembly, convection, Rhie–Chow, both corrections | **`previousU`/`previousV`** — the under-relaxation RHS *and* the momentum solver's warm-start initial guess `toVector(previousU)` | **every iteration** |
| **Pressure** | pressure gradient, Rhie–Chow predictor | none | **once, after the loop** |
| **MassFlux** | momentum convection | `evaluateContinuity(fluxNew)` | **every iteration** |
| **Viscosity** | diffusion | none | only when the turbulence model changes it |

### The velocity download is not a failure to achieve residency

It is a genuine host consumer, and finding it was the sharpest moment of this gate. The audit
originally listed velocity's host consumers as "`allFinite`, final result" and concluded it could
stay resident for the whole solve. That was wrong: `SIMPLE.cpp` reads `previousU`/`previousV` from
the host velocity at the top of **every** iteration, and feeds them to the momentum solver as its
initial guess. Leaving the host copy stale changed the solver's iteration path and broke
equivalence at **outer iteration 2** — caught by `full_solve_equivalence`, not by reasoning.

What residency still buys on this field is the **upload**. The device carries its own corrected
velocity forward, so it never receives those three cell-fields back; the host gets a copy because
the host genuinely needs one. Download and upload are separate costs and only one of them was
avoidable.

### The mass-flux download is required for a different reason

`evaluateContinuity` consumes only two scalars, so a device reduction looks obvious. It is rejected
because `rms` is a sequential left-to-right sum and GPU-DISC-001 established **bitwise**
equivalence: a parallel tree reduction changes the rounding and therefore the residual history.
Reproducing the CPU's order on the device means a serial device loop, which at 409,600 cells costs
far more than the copy it replaces. Documented in `audit.md` §3.

## 6. The one field with no host copy — and its guard

`Pressure` is the only field that has no host copy during the solve, which is what makes its
residency worth having. It is also why `residentPressureAllFinite()` exists: SIMPLE's per-iteration
`NonFiniteState` guard must still run, and for pressure there is nothing on the host to check.

```text
resetNonFiniteCounter  -> cudaMemset, no upload
countNonFiniteDevice   -> one kernel, accumulates into the counter
readNonFiniteCounter   -> ONE 4-byte D2H
```

Velocity and mass flux are downloaded anyway, so SIMPLE checks those on the host for free. Four
bytes cross the boundary per iteration instead of a full pressure field.

**This introduces the first atomic in the codebase.** GPU-DISC-001O's diagnostics audit recorded
"atomics NONE anywhere"; that is no longer true and is stated here rather than left to be
rediscovered. It is a single-counter `atomicAdd`, race-free by construction, and it gives racecheck
an atomic to examine where it previously had none.

A first version of this guard allocated its counter locally and uploaded a zero into it, **per
field per iteration** — 4 allocations, 4 H2D and 4 D2H every outer iteration. It broke the
zero-steady-state-allocation criterion outright and consumed most of the residency win it was meant
to protect. Measured, then fixed.

## 7. Lifecycle

| event | behaviour |
| --- | --- |
| solve starts | `prepare()` builds the plans; every field is `HostOnly` until `uploadInitialState` |
| solve restarts | `uploadInitialState` again — every field returns to `Synchronized` carrying the **host** values, verified by the dirty-state suite |
| case reloaded / mesh size changes | a new `GpuSimpleDiscretization` is constructed and prepared; buffers are sized to the new mesh and nothing survives from the old one |
| backend switches CPU ↔ GPU | the CPU path never constructs the facade, and `prepare()` returns false in a CUDA-disabled build, so there is no device state to go stale |
| solver object reused for another case | each `SIMPLE::solve` call owns its own facade; state cannot leak between cases |
| GPU error or abort | `DeviceBuffer` is RAII; every buffer is released by its destructor on the way out. `checkCuda` throws `NumericalError`, the facade's `requireResident` guards throw `InvalidArgumentError`, and neither leaves a partially-updated host field presented as valid |

## 8. What this contract does NOT cover

* **The linear systems.** `assembleMomentum` and `assemblePressureCorrection` still return host
  `LinearSystem`s, because `LinearSolver` is a host interface. That round trip dominates D2H bytes
  and removing it is the definition of the next item, `GPU-resident pressure solve`. Untouched here.
* **The momentum solutions and p'.** They come from the host solver, so their uploads are forced by
  the same boundary.
* **Any numerics.** No operator changed. The only new arithmetic is the pressure update, which is a
  transcription of the host loop in the same association order with FMA contraction disabled.
