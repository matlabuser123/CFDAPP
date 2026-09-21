# GPU-PIPE-001 — Persistent GPU fields

**Result: PASS.** Production SIMPLE state now persists on the device across outer iterations. The
six full-field uploads that happened every iteration are gone, steady-state allocations stay at
zero, and every numerical gate remains bitwise.

```text
per-iteration full-field H2D    6 removed   (velocity x3, pressure, viscosity, massFlux)
H2D calls/iteration             21.0 -> 15.0   production path (gpu-disc)
                                12.0 ->  6.0   field-only view (disc-only)
H2D bytes/iteration          2,664,960 -> 1,228,800   (-53.9%, field-only view)
steady-state allocations         0 -> 0        reallocations 0
GPU-DISC gates                  15/15 bitwise
dirty-state / lifecycle         all properties pass
negative controls               7/7 observable detected, 1 null confirmed
CUDA diagnostics                12/12 clean and non-vacuous
CPU backend                     1932/1932, 0 nvcc invocations, no libcudart
performance                     within measured spread at every grid; crossover unchanged
```

## 1. What was made persistent, and what was not

| field | before | after |
| --- | --- | --- |
| velocity U,V,W | re-uploaded every iteration | **device-resident**; carried from its own corrected result. Still **downloaded** each iteration — see §3. |
| pressure | re-uploaded every iteration | **device-resident**; updated on the device, downloaded **once** after the loop |
| massFlux | re-uploaded every iteration | **device-resident**; carried from its own corrected result |
| effectiveViscosity | re-uploaded every iteration | uploaded **once**, and again only when the turbulence model changes it |
| momentum solutions, p' | uploaded per iteration | unchanged — the linear solver is on the host |
| momentum / pressure systems | downloaded per iteration | unchanged — **the next gate** |

## 2. The ownership model

`ownership.md` is the contract; the short form:

```cpp
enum class FieldAuthority { HostOnly, DeviceOwned, Synchronized };
FieldAuthority authority(PersistentField) const noexcept;
```

Three device operations mark a field `DeviceOwned` (`updatePressure`, `correctVelocityResident`,
`correctFaceMassFluxResident`); three explicit downloads return it to `Synchronized`. Host-side
mutation during a solve is **unsupported and enforced by absence** — no API accepts one — rather
than silently tolerated. A freshly prepared facade starts `HostOnly` on every field, so nothing can
leak between cases.

There is deliberately **no caching layer**. A `Synchronized` re-read still copies, because the
caller passed an empty field to fill; building invalidation logic to avoid a copy production never
performs twice would add the most bug-prone part of a cache for no gain. The dirty-state suite
records what the code actually does rather than claiming a property it does not have.

## 3. The miss that equivalence caught

The audit listed velocity's host consumers as "`allFinite`, final result" and concluded it could
stay resident for the whole solve. **That was wrong**, and `full_solve_equivalence` found it:

```text
FAIL H  cavity 2d 8   first divergent iteration: 2
FAIL H  channel 2d 12 first divergent iteration: 2
FAIL H  cavity 3d 4   first divergent iteration: 2
```

`SIMPLE.cpp` reads `previousU`/`previousV` from the **host** velocity at the top of every
iteration, and feeds them to the momentum solver as its **warm-start initial guess**
(`toVector(previousU)`). Leaving the host copy stale changed the solver's iteration path from
iteration 2 onward.

The fix keeps downloading velocity — it is a genuine per-iteration host consumer — while still
never re-uploading it, because the device carried its own corrected copy forward. **Download and
upload are separate costs and only one of them was avoidable.** After the fix: 15/15 gates green.

## 4. Transfers — measured before and after

`transfers/baseline.log`, `transfers/after.log`. 160² cavity, fitted as
`calls(n) = setup + perIteration · n` from runs at 4, 8 and 16 outer iterations, with the fit's
linearity checked rather than assumed.

```text
disc-only (CPU solver + GPU disc -- the clean FIELD view, no Krylov reduction noise)
                          BEFORE        AFTER      change
  H2D calls/iteration       12.0          6.0      -50.0%
  H2D bytes/iteration  2,664,960    1,228,800      -53.9%
  D2H calls/iteration       16.0         17.0      +1  (the 4-byte finiteness counter)
  D2H bytes/iteration  8,368,664    8,368,668      +4 bytes
  allocations, setup         345          347      +2 (pressureNext, nonFiniteCounter)
  allocations/iteration        0            0      unchanged -- criterion 5
  reallocations/iteration      0            0

gpu-disc (the production path)
  H2D calls/iteration       21.0         15.0      -28.6%
  H2D bytes/iteration  6,950,384    5,514,224      -20.7%
  allocations/iteration        0            0
```

The 6 remaining H2D calls per iteration on the field-only view are all host-forced: two momentum
solutions, one p', and the `previous` components the under-relaxation RHS needs — every one of them
a value the host solver produced.

**Initialization-only traffic** is the one-time `uploadInitialState` (6 fields) plus the plan/mesh
setup already established in GPU-DISC-001. **Final-output traffic** is one velocity and one
pressure download after the loop.

## 5. Dirty state and lifecycle

`dirty-state/`. Every property the brief names, tested against the production facade with the
transfer counters as the instrument:

```text
PASS  before upload, every field is HostOnly
PASS  after uploadInitialState, every field is Synchronized
PASS  after updatePressure, pressure is DeviceOwned (host copy stale)
PASS  downloading a DeviceOwned field costs exactly ONE D2H
PASS  the device pressure update is bitwise 0 + 0.3*1.0 == 0.3 in every cell
PASS  beginIterationResident transfers nothing at all
PASS  setViscosity costs exactly ONE H2D when the model changed it
PASS  no API exposes host-side field mutation mid-solve
PASS  a freshly prepared object starts HostOnly -- no stale state carried in
PASS  a larger mesh's fields are sized to the LARGER mesh
PASS  beginIterationResident throws when there is no resident state to continue from
PASS  a restart via uploadInitialState returns every field to the HOST values
```

The bitwise `0.3` check is the direct proof that the device pressure update does not contract into
an FMA — the reason it needed its own `-fmad=false` translation unit.

## 6. Negative controls — including two that change no number

`negative-controls/driver.log`. Run through the GPU-DISC-001P engine, so the Phase D discipline is
the one already qualified rather than a second implementation.

| control | detector | result |
| --- | --- | --- |
| `pf1` initial upload skipped | full-solve | DETECTED |
| `pf2` pressure carry omitted | full-solve | DETECTED, **first divergence iteration 2** |
| `pf3` velocity carry omitted | full-solve | DETECTED, **iteration 2** |
| `pf4` flux carry omitted | full-solve | DETECTED, **iteration 1** |
| `pf5` `-fmad=false` removed from the update | full-solve | DETECTED, **iteration 3** |
| `pf6` authority lies after a device write | **dirty-state** | DETECTED |
| `pf7` re-upload every iteration | **transfer guard** | DETECTED |
| `pf8` viscosity never refreshed | full-solve | **null**, confirmed undetected |

7/7 observable detected, all restored byte-exact and re-passing.

**`pf7` is why this gate needed a transfer detector.** Re-uploading the whole field set every
iteration is numerically **identical** — every equivalence gate in this project passes it. Without
a detector that counts transfers, "the fields are resident" would be an unfalsifiable claim.
`tools/transfer_guard.cpp` measures per-iteration H2D on the disc-only arm and fails above the
qualified budget.

The divergence *ordering* is itself a check that the controls test what they claim: the flux carry
breaks iteration 1 (the flux feeds convection immediately), the velocity carry iteration 2 (via the
warm start), and the FMA contraction iteration 3 (a rounding difference needs iterations to grow).

**`pf8` is null only because the test set is laminar**, and that is a stated coverage limit rather
than a claim about the code. `LaminarModel` returns a constant viscosity, so never refreshing it
changes nothing. A transport turbulence model would make this control observable, and the guard it
protects is real.

## 7. CUDA diagnostics

`cuda-diagnostics/`. All four tools across 2D, 3D and the GPU-solver mode, on the residency path:

```text
memcheck / initcheck / synccheck / racecheck  x  cavity2d / case3d / gpusolver
12/12 runs, 0 errors, 0 hazards, every one non-vacuous
kernel counts 1080 / 3059 / 38032   (up from 1000 / 2911 / 37932 -- the new
                                     pressure-update and finiteness kernels)
```

Clean on exactly the categories a residency change threatens: lifetimes, stale pointers, buffers
reused after resize, 2D/3D component contracts.

**This gate introduces the first atomic in the codebase.** GPU-DISC-001O's audit recorded "atomics
NONE anywhere"; that is no longer true and is recorded here rather than left to be rediscovered.
It is a single-counter `atomicAdd` in the finiteness check, race-free by construction, and racecheck
now has an atomic to examine where it previously had none.

## 8. CPU backend

`cpu-backend/run.log`. A from-scratch CUDA-disabled configure, build and full test suite:

```text
nvcc invocations in build.ninja   0
cfdapp links libcudart            no
tests                             1932/1932 passed, 0 failed
```

The CPU-only stub was extended to keep exact parity with the new header. Following GPU-DISC-001R,
that file is now actually compiled by this gate rather than left to drift.

## 9. Performance — honest result

`performance/cavity.log`, compared against the GPU-DISC-001Q qualified baseline.

```text
grid       before   after    change    measured spread
20x20       8.123   8.239    +1.4%       6.2%
40x40      14.282  14.201    -0.6%       5.2%
80x80      17.511  17.319    -1.1%       2.0%
160x160    10.153   9.666    -4.8%       1.2%
320x320     7.381   7.064    -4.3%       6.6%
640x640     8.799   9.282    +5.5%       9.3%

crossover  160^2 -> 160^2   (unchanged)
both BITWISE equivalence pairs still pass
```

**Every change sits inside the measured run-to-run spread. There is no measurable end-to-end
effect, and no regression.** That is reported as the result rather than the two favourable rows
being quoted as an improvement.

The reason is arithmetic, not disappointment: at 640² the six saved uploads are ~23 MB against
~317 MB of H2D per iteration — about 7% of H2D bytes, and H2D is itself small beside the linear
solve. **The dominant traffic is the linear-system round trip**, which this gate deliberately did
not touch. Residency is a correctness-and-architecture prerequisite for removing that round trip;
the speed-up belongs to the gate that removes it.

What residency does deliver now: 6 fewer transfers and 1.44 MB less traffic per iteration, zero
steady-state allocations, and an explicit ownership model that makes the resident pressure solve
implementable at all.

## 10. Full regression

`regression/ctest.log`

```text
ctest --test-dir build/final --output-on-failure
  100% tests passed, 0 tests failed out of 1998   (2043 discovered, 45 disabled)
  Total Test time (real) = 1310.81 s
  `ninja: no work to do` after ctest
```

Counts match the GPU-DISC-001R baseline exactly. No earlier gate regressed: the 15 GPU-DISC
differential gates are green on the same binaries, `integrated_simple_equivalence` and
`full_solve_equivalence` among them.

## 11. Files changed

```text
new     include/cfd/gpu/DevicePersistentFields.hpp
new     cuda/kernels/DevicePersistentFieldsKernel.cu      (-fmad=false)
edit    include/cfd/gpu/GpuSimpleDiscretization.hpp       authority model + resident API
edit    cuda/kernels/GpuSimpleDiscretizationCuda.cpp      persistence, carry, authority
edit    src/gpu/GpuSimpleDiscretization.cpp               CPU-only parity
edit    src/pressure_velocity/SIMPLE.cpp                  GPU branch only
edit    cuda/CMakeLists.txt                               register the kernel, -fmad=false
```

The CPU branch of `SIMPLE::solve` is untouched: the relaxed pressure update, the velocity/pressure
carry and the `allFinite` guard all keep their original host form under `if (!useGpuDiscretization)`.

## 12. What is NOT started

* **`GPU-resident pressure solve` — not started.** The linear systems still round-trip through host
  `LinearSystem`s, which is what that item exists to remove and what dominates D2H bytes.
* **`GPU-resident SIMPLE loop` — not started.**
* **`Final CPU/GPU equivalence` — not started.**
* No numerics changed; no tolerance or convergence criterion moved.
* Nothing committed or pushed.
