# Decision 2 — steady-state turbulence allocations, 30 → 0

The production residency ledger measured **30 device allocations per
steady-state turbulent resident outer iteration**, against a contract of zero.
This is the audit and the fix.

```text
per turbulence correction (isolated)  16  ->  0   allocations
                                       0  ->  0   reallocations
```

`ResidentChannelProduction.RepeatedCorrectionsAllocateNothing` is green.

Measured by differencing a 20-iteration and a 21-iteration production solve, so
iteration 0's initial upload is excluded and what remains is one steady-state
iteration.

---

## 1. The audit — all 30 classified

Located by instrumenting `correctKEpsilonDevice` at stage boundaries under an
env guard (since reverted), which showed the allocations arriving in two equal
bursts at the k and epsilon assemblies and nowhere else.

| source | count | classification | disposition |
|---|---:|---|---|
| `correctKEpsilonDevice` locals: `gammaK/gammaEpsilon`, four source buffers, `solvedK`, `solvedEpsilon`, `freshMuT` | 9 | reusable workspace | promoted to `DeviceKEpsilonCorrection` |
| two `DeviceScalarTransportSystem` locals (4 buffers each) | 8 | reusable workspace | promoted to `DeviceKEpsilonCorrection` |
| `assembleScalarTransportDevice`'s `DeviceDiffusionSystem` + `DeviceConvectionSystem` scratch (4 each, twice per correction) | 8 | reusable scratch | moved into `DeviceScalarTransportSystem` |
| one-`int` validity flags (assemble ×2, fix-imposed-rows, effective diffusivity ×2, effective viscosity) | 6 | unavoidable per-call check | reused via function-local static |
| `setWallFaceViscosity` compact staging buffers | 2 | reusable staging | promoted to `GpuSimpleDiscretization::Impl` |
| the assembly moving OUT of its own scratch | 4 per assembly | the real cause | swap instead of move, see section 4 |

## 2. Publication had to change from move to swap

```cpp
state.k = std::move(deviceK);   //  before
std::swap(state.k, deviceK);    //  after
```

Once the solve outputs are persistent workspace, moving them into `state` would
leave the workspace empty and force a fresh allocation on the very next
correction — reintroducing exactly what was removed. The swap rotates the
buffers instead: afterwards the workspace holds the previous iteration's
fields, already the right size, so the next `resize` is a no-op.

Publication stays **atomic and last**: all three land together, only once both
solves have succeeded, exactly as before.

## 3. Separation preserved

The assembly scratch is carried by the **system** rather than by a
function-local static, so the k and epsilon assemblies keep their own storage —
the same separation their plans and Krylov workspaces already have. Two
equations sharing one scratch buffer is a defect waiting for a reordering.

The one-`int` flags do use function-local statics, following the precedent
`reduceToHost`'s `partialSumsCache` already sets for exactly this pattern: a
single-element buffer, written and read within one call.

Unchanged: separate k/epsilon plans, separate Krylov workspaces, old-state
source semantics, atomic publication, and every numerical result.

## 4. The last four, and why promoting the scratch was not enough

Moving the scratch into `DeviceScalarTransportSystem` removed 8 of 16 and left
4 per assembly. Stage probes put them inside `assembleScalarDiffusionDevice`,
where every visible `resize` targets a persistent member -- which is exactly why
reading the function was not enough to find them.

The cause was one line further on, in the merge:

```cpp
system.rowOffsets = std::move(diffusion.rowOffsets);   // and three more
```

`diffusion` **is** `system.diffusionScratch`. Moving out of it left the scratch
empty on every call, so the next `assembleScalarDiffusionDevice` reallocated all
four buffers. Promoting the scratch had made it persistent in lifetime but not
in contents.

Fixed the same way as the publication -- swap, not move:

```cpp
std::swap(system.rowOffsets, diffusion.rowOffsets);
```

The scratch then inherits the previous iteration's buffers, already sized.

## 5. The gate needs TWO warm-up corrections, and that is a property

A swap rotation fills in two stages. Call 1 sizes the system and hands the
scratch the system's old, empty buffers; call 2 sizes the scratch and hands it
call 1's sized ones. Only from call 3 is every buffer in the rotation correctly
sized.

So the gate permits two warm-up corrections and then requires **zero** across
the next five. Permitting only one would charge it the cost of filling the
second half of a double buffer -- a one-off, not a steady state. That cost is
exactly what the "8 across 5 corrections" reading was, and its non-integer
per-correction average was the clue that it was not recurring at all.

```text
5 further corrections: 0 allocations, 0 reallocations
```
