# Who actually needs the published host k, epsilon and mu_t

After the convergence reduction moved to the device, the remaining full-length
D2H per correction is the publication of three fields:
`3 * sizeof(double) * N`. This audit answers whether any of it can be deferred,
**before** anything is deferred.

**Result: no, not as the code stands — and the reason differs per field.** One
of the three is genuinely load-bearing every outer iteration, one is
load-bearing only on the path production currently takes, and one has no
consumer at all but cannot be dropped on its own without weakening a deliberate
invariant for no gain.

---

## The three fields are not alike

| field | on the `TurbulenceModel` base contract? | production consumer | needed every outer iteration? |
|---|---|---|---|
| `mu_t` | **yes** — `turbulentViscosity()` is pure virtual and returns `const ScalarField&`, a reference to live host storage | `SIMPLE` via `effectiveViscosity()`; `ThermalSolver` holds a **raw pointer** to it | **yes** |
| `k` | no — `KEpsilonModel::k()` is model-specific | `correctOnDevice` reads host `k_` for `computeWallTreatment` | **yes on the current path**, no on the resident path |
| `epsilon` | no | **none found** in `src/` or `include/` outside `KEpsilonModel` itself | no |

---

## 1. Does the public contract require them immediately after `correct()`?

**`mu_t`: yes.** `TurbulenceModel::turbulentViscosity()` returns a reference, so
the host field must exist and be current the moment `correct()` returns.
`SIMPLE.cpp:547-548` calls `correct()` and `effectiveViscosity()` on
consecutive lines, every outer iteration.

**`k` and `epsilon`: not by the base contract** — neither accessor is on
`TurbulenceModel`. They are `KEpsilonModel`'s own.

## 2. Does resident SIMPLE read the host copies next iteration?

**`mu_t`: yes, and it round-trips.** `effectiveViscosity(mu)` builds `mu + mu_t`
as a *host* `ScalarField`, and `SIMPLE.cpp:573-575` then uploads it back:

```text
device mu_t --D2H--> host mu_t --(+mu, host)--> host effectiveViscosity
                                                        |
                                              setViscosity()  --H2D--> device
```

`sameField()` suppresses the upload when nothing changed, which makes it free
for a laminar solve and *never* free once turbulence is active. So mu_t crosses
PCIe twice per outer iteration today. This is the same shape of defect as the
wall-production round trip already removed, one level up.

**`k`: depends on which path is production.** `correctOnDevice` — today's GPU
path — evaluates the wall treatment on the host from `k_`
(`KEpsilonModel.cpp:144-146`), so the host copy is load-bearing. The resident
path does not: `correctResidentWithWallTreatment` gathers `k[owner]` compactly
from the device, which is exactly what the Step 3 work built.

**`epsilon`: no.** No reader found.

## 3. Do logging, diagnostics, export or checkpoint need them per iteration?

* **Checkpoint: no.** Turbulence state is explicitly not checkpointable —
  P13-001, recorded in `TODO.md`: `k`, `epsilon`/`omega` and `mu_t` cannot be
  restored, and the refusal is explicit at both ends.
* **Export: no consumers** of `k`/`epsilon` found under `src/io/`.
* **Thermal: an aliasing constraint, not a read we control.**
  `ProjectRunner.cpp:599` stores `&activeTurbulenceModel->turbulentViscosity()`
  — a raw pointer into the model's host field — in the thermal config. Whenever
  thermal is active that field must stay valid *and current*; there is no call
  site to defer, because the consumer holds the address.
* Wall diagnostics (`yPlus_`, `wallShearStress_`, `wallViscosity_`) are
  published separately and are **not** part of the `3N`.

## 4. Can the authoritative state stay on device, syncing at a boundary?

* **`epsilon`: yes, trivially — and it is not worth doing alone.** See §5.
* **`k`: yes, once the resident path *is* the production path.** The mechanism
  already exists and is qualified; what blocks it is that `correctOnDevice`, not
  `correctResidentWithWallTreatment`, is what `KEpsilonModel::correct()` calls.
* **`mu_t`: not without one of two changes.**
  1. a device-side effective viscosity plus a `setViscosityFromDevice` entry
     point, so `SIMPLE` never needs the host copy to feed the GPU
     discretization; or
  2. a change to `TurbulenceModel`'s interface so `turbulentViscosity()` is not
     a reference to host storage.

  (1) is much the smaller change, is confined to the GPU path, and is exactly
  the edge the resident architecture needs. (2) touches every model.

## 5. What would change if publication were deferred today?

* **`mu_t` deferred → physics changes.** `effectiveViscosity()` would return the
  previous iteration's viscosity, momentum would assemble with it, and thermal's
  aliased pointer would read stale data with no call site to intercept. Not
  deferrable.
* **`k` deferred → physics changes on the current path.** The host wall
  treatment would run against a stale `k`, moving `y+`, `tau_w` and the imposed
  epsilon. Not deferrable while `correctOnDevice` is production.
* **`epsilon` deferred → no observable change found.** But deferring it *alone*
  saves `N` of `3N`, and it would break the publication invariant the code
  states deliberately: *"Publication, still atomic and still last"*
  (`KEpsilonModel.cpp:169-171`) — all three fields become visible together, only
  after both solves succeed, so a failed correction can never leave the model
  half-updated. Trading that for a third of one category is a bad exchange.

---

## Recommended policy

**Keep publication exactly as it is for now.** It is `3N` per correction, it is
recorded here rather than hidden, and none of it can be deferred without either
changing physics or weakening the atomic-publication invariant.

**The next step that actually moves the number is the mu_t round trip**, and it
is an architectural decision rather than a cleanup: a device-side effective
viscosity plus `setViscosityFromDevice` would remove one `N` D2H *and* one `N`
H2D per outer iteration, and would take `mu_t` off the host path that
`SIMPLE` currently depends on. That is a prerequisite for the resident target
diagram, not a side quest — but it is **not** authorised under the current task,
and it is not being done unilaterally.

Once the resident path becomes production, `k`'s host copy also stops being
load-bearing, and only `mu_t` remains contractually required.

### Steady-state transfer per correction, for the record

```text
                      before this phase    now
wall gather               O(N_wall)        O(N_wall)     unchanged, compact
wall production RT        16 * N_wall      0             removed
convergence residual      32 * N           0             removed (device reduction)
publication               24 * N           24 * N        audited, contractually required
Krylov partials           O(iters*N/256)   O(iters*N/256) unchanged
control                   52 B / 9 calls   52 B / 9 calls unchanged
```
