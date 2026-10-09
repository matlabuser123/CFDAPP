# Phase 1 — mu_eff on the device

`mu_eff[i] = mu + mu_t[i]`, computed on the device, handed to the resident
momentum path without either field crossing PCIe.

**Scope, stated precisely.** Phase 1 delivers and qualifies the *mechanism*. It
does **not** change what production does: `SIMPLE` still calls
`effectiveViscosity()` and `setViscosity()`, because switching it over means
widening the laminar resident guard, and that is blocked on the wall-face
viscosity override the device does not have yet
([audit.md](audit.md) §4). So the measured saving below is measured on the
handoff, which is what Phase 1 was scoped to.

---

## 1. The operation

`computeEffectiveViscosityDevice(mu, mu_t, out)` in `DeviceTurbulence`.

**Deliberately not `computeEffectiveDiffusivityDevice(mu, mu_t, 1.0, out)`.**
That computes `mu + mu_t/sigma`, a turbulence closure parameterised by a
Prandtl–Schmidt number; it coincides with `mu + mu_t` at `sigma = 1` by
arithmetic accident, not by meaning. The host keeps
`TurbulenceModel::effectiveViscosity` and `computeEffectiveDiffusivity` as two
separate functions for exactly that reason, and sharing one device routine would
let a later change to the closure — a floor, a different convention — silently
change the momentum viscosity.

**Fail-closed, replacing a host walk.** `validateTurbulentViscosityField`
inspects every cell for non-finite or negative `mu_t`. The kernel checks the
same condition and reports one 4-byte flag. That is what lets the resident path
drop the `mu_t` download outright rather than keeping it alive only to validate
it — the same pattern `assembleScalarTransportDevice` already uses.

## 2. The handoff

`adoptDeviceEffectiveViscosity(discretization, ResidentTurbulentViscosity)`,
declared in the CUDA-only `GpuSimpleResidentTurbulence.hpp` and friended from
the CUDA-free `GpuSimpleDiscretization.hpp` — the same opaque-struct facade
already used for `ResidentTurbulenceInputs` in the other direction. No CUDA type
is named by a header a CPU-only target compiles.

Ownership uses the mechanism that already existed rather than a new one: on
success the discretization's viscosity becomes `FieldAuthority::DeviceOwned`,
whose documented meaning is *"the device copy is authoritative; any host copy is
stale"*. The buffer written is `Impl::viscosity`, the one
`assembleRelaxedMomentumDevice` already reads — no second allocation, nothing
aliased, nothing outliving the call.

`downloadViscosity()` was added alongside. Viscosity was the only one of the
four tracked persistent fields with no explicit host access — harmless while it
was always `Synchronized` because the host had just uploaded it, and a real gap
once it can be `DeviceOwned`, since then nothing could read it back at all.

## 3. Transfer

```text
per turbulent outer iteration      before            after (handoff)
mu_t full D2H                      8N                0
effective-viscosity full H2D       8N                0
validity flag                      -                 4 bytes each way
```

At the 128-cell fixture: 1 024 bytes down and 1 024 up, replaced by 4 and 4.
Gated by `ResidentHandoffTakesDeviceAuthorityWithoutTransfer`, which requires
both directions to stay strictly below one full field and the authority to end
as `DeviceOwned`.

## 4. Equivalence

Bitwise against `TurbulenceModel::effectiveViscosity`, at n = 1, 255, 257 and
1000, for four regimes: zero `mu_t`; ordinary turbulent magnitudes;
`mu_t ~ mu = 1.0`; and `mu_t` twenty decades *below* `mu`, where the addition
rounds `mu_t` away entirely and the point is that the device rounds it away
identically.

Bitwise is the right bar and not an over-claim: one IEEE addition per cell,
cell-local, no reduction, nothing to reassociate. A tolerance would be covering
for a different operation rather than for arithmetic.

## 5. Controls

| control | asserts |
|---|---|
| `PerturbingOneCellChangesExactlyThatCell` | the perturbed cell moves to `mu + mu_t`, every other cell is bitwise unchanged |
| `ChangingMolecularViscosityShiftsEveryCell` | both `mu` values reproduce `mu + mu_t[i]` exactly |
| `RejectsInvalidInputsExactlyAsTheHostDoes` | non-finite/non-positive `mu`, and negative or NaN `mu_t`, all throw |
| `RepeatedComputationIsBitwiseReproducible` | four repeats, bitwise |
| **host mirror poisoned, device fixed** | recomputing reproduces the first result exactly — no host path feeds the resident field |
| **device perturbed, host fixed** | exactly that cell moves, to `mu + mu_t_perturbed` |

The last two are a pair on purpose. The poisoned-mirror control alone would pass
if the resident field ignored *both* inputs; the device-perturbation control is
what makes it non-vacuous.

## 6. Suites

```text
GPU            169/169      (162 before, +7)
SIMPLE         123/123      CUDA and CUDA-OFF
turbulence     246/246      CUDA and CUDA-OFF
CPU-only build clean
```

No production turbulent result changed, and could not have: the resident path is
still laminar-gated and nothing calls the new handoff yet.

## 7. Two test defects found and fixed while writing this

Recorded because both produced confident-looking failures with unrelated
messages:

1. `muTPattern` computed `std::pow(10.0, (i % 7) - 3)` where `Index` is
   `size_t`, so the exponent wrapped for `i % 7 < 3` and `mu_t` became `inf`.
   The fail-closed validation caught it — the check earning its place on its
   first outing.
2. The resident test probed cell 200 on a 16×8 = 128-cell fixture, reading past
   the end of two vectors. The probe index is now derived from `n`.

---

## Next, and explicitly not done here

Sparse GPU wall-face viscosity override. Until it exists,
`muFace = effectiveViscosity[ownerId]` on the device has no wall-function
override, and widening the resident guard would discard the wall shear
coupling entirely. Phase 1 changes nothing about that and does not move the
guard.
