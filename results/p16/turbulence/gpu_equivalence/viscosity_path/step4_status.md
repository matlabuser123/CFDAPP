> **SUPERSEDED.** This records Step 4 at the point where two gates were still
> open. All four closing gates have since passed — see
> [step4_gates.md](step4_gates.md). Kept because the failures below are the
> evidence trail for defects that were real: the 30 steady-state allocations
> (fixed, see [allocations.md](allocations.md)) and the long-horizon
> differential (traced to the wall-override and flux-source defects, see
> [same_state_map.md](same_state_map.md) and
> [continuation_root_cause.md](continuation_root_cause.md)).
>
> Section 1's conclusion — that Tests 1-3 were "NOT CONSTRUCTIBLE" — was
> wrong, and instructively so. It read P13-001 as a prohibition when P13-001
> is a statement about the PRODUCTION restart contract. A test-only injection
> fixture leaves that contract untouched, and the mature same-state map it
> enabled immediately found a real defect that 600-iteration trajectories had
> been hiding.

# Step 4 status — two gates closed, one blocked by a recorded API gap, two real defects found

```text
wrong-flux production negative control     PASS
production residency ledger                FAIL   30 allocations per steady-state iteration
same-state iteration map (Tests 1-3)       NOT CONSTRUCTIBLE   see section 1
long-horizon field differential            FAIL   unchanged, preserved
```

`GPU 188/190`. The two failures are the long-horizon differential and the
residency ledger, both genuine and both preserved.

---

## 1. Tests 1–3 are blocked by P13-001, not by effort

The same-state iteration map, the shadow-step sweep and the cross-backend
restart all require cloning a **mature turbulence state** into both arms.

`KEpsilonModel` has exactly one constructor, taking *uniform scalar* initial
values. There is no field-level restore, and this is not an oversight — it is
`TODO.md`'s recorded P13-001:

> Turbulence state is not checkpointable (P13-001) — `k`, `epsilon`/`omega` and
> `mu_t` cannot be restored, because KEpsilonModel / KOmegaModel / SSTModel are
> constructible only from uniform scalar initial values. **Refused explicitly at
> both ends rather than resumed wrongly.**

So a one-step comparison from iteration 50 or 300 cannot be built without adding
a state-restore API. That is a real change to a deliberately-refused capability
and is not taken unilaterally.

### What the existing sweep already provides

**Iteration 1 IS a same-state one-step map.** Both arms start from byte-identical
state — uniform velocity, zero pressure, uniform k and epsilon — so the first
outer iteration is exactly the experiment Test 1 describes, at one checkpoint:

```text
k       2.098e-15
mu_t    6.344e-12
tau_w   0.000e+00   (exactly)
```

That is three to eleven orders inside the 1e-6 bound and says the GPU implements
the same first iteration as the CPU. It does not extend to mature states, and
this document does not claim it does.

The pressure columns at iterations 1–2 (8.1, 2.1) are relative errors against a
near-zero pressure field and are not meaningful; only the trend from iteration 5
onward should be read.

## 2. Wrong-flux production negative control — PASS

Through the **resident entry point**, the one
`correctFromResidentDiscretization` drives in production, whose `massFlux` input
is a device buffer the caller supplies. Two corrections from the same turbulence
state, differing only in that buffer:

```text
max|dk|    3.282e-05
max|deps|  1.485e-06
```

The first transport assembly responds, so the path genuinely consumes the flux
it is handed rather than recomputing one. `correct()` could not have been used
for this: it derives the flux internally from velocity, so there is nothing to
substitute.

## 3. Production residency ledger — FAIL, and the finding is real

One steady-state turbulent resident outer iteration, measured by differencing a
20-iteration and a 21-iteration production solve so that iteration 0's initial
upload is excluded:

```text
288 cells, 612 faces, 48 wall faces

D2H            31 296 bytes in 678 calls
H2D             2 712 bytes in  13 calls
allocations        30
reallocations       0
```

**30 device allocations per steady-state iteration.** The residency contract
requires zero. They come from locally-scoped `DeviceBuffer`s that are
constructed and destroyed inside every correction — `gammaK`, `gammaEpsilon`,
the four source buffers, the two `DeviceScalarTransportSystem`s (four buffers
each), `deviceK`, `deviceEpsilon`, `newMuT`, six validity-flag buffers, and the
two compact upload buffers in `setWallFaceViscosity`.

Most of these predate this phase — `correctKEpsilonDevice` has always built its
work buffers as locals — but they were never measured, because until Step 4
there was no production turbulent resident iteration to measure. Two of them
(`setWallFaceViscosity`'s) are new in Phase 2.

This is recorded as a failing gate, not worked around.

### H2D is clean, and my first test of it was wrong

The 2 712 bytes are entirely `O(N_wall)` and `O(1)`:
48 wall faces x 8 bytes x 7 arrays = 2 688, plus 24 bytes of validity flags.

A first version of this test compared H2D against *one full cell field* and
failed at 2 712 vs 2 304. That was the **test** being wrong: on this mesh the
legitimate wall traffic simply exceeds one 288-cell field, and a byte count
alone cannot distinguish "one full field" from "a lot of wall faces". The
assertion was replaced; the correct property is that H2D must not grow with the
cell count, which is what the wall-transfer ledger already establishes by
construction.

## 4. What remains before Step 4 can close

1. A decision on the same-state iteration map: either add a turbulence
   state-restore API (touching recorded P13-001 scope), or accept the
   iteration-1 datum as the available same-state evidence and say so explicitly.
2. The 30 steady-state allocations: fix by promoting the correction's work
   buffers to persistent members, or accept and record with justification.
3. The long-horizon differential stays failing at the unchanged 1e-6 bound, with
   the full onset sweep preserved as evidence that no error is injected at
   iteration 1 and that the difference decays monotonically from iteration 25.
