# P15 — Thermal / Energy: closeout

Written for: the person deciding whether P15 closes and whether P16 may start.

**Status: complete. All gates pass.** The five TODO items are implemented and
verified; the final regression is green in all three configurations; every gate
is paired with a control that breaks it.

---

## The five items

| Item | Status | Principal evidence |
|---|---|---|
| Temperature transport | **done** | Cases A–D; `validation.md` |
| Enthalpy / energy formulation | **done** | Closed-form integral + inversion; Case G; NC5/NC6 |
| Thermal boundary conditions | **done** | Robin with both physical limits; Case E; NC1 |
| Temperature-dependent properties | **done** | `TemperatureDependentProperty`; Case F; reachable from a case file end to end |
| Validation suite | **done** | 16 validation + 126 thermal unit + 23 case-format + 5 production-path |

---

## Final numbers

| Gate | Result |
|---|---|
| `release_cuda` | **2160 / 2160**, 0 failures |
| `debug_gui` | **2145 / 2145**, 0 failures |
| `cpu_only` | **2085 / 2085**, 0 failures |
| SIMPLE fingerprint (P13-001) | **IDENTICAL** — bitwise, 10 cases |
| clang-format at CI scope | **clean**, exit 0 |
| Negative controls | **8 / 8 DETECTED**, 0 vacuous |
| GPU encoder criteria 1–7 | **7 / 7 PASS** |
| Mesh convergence | p = 1.77 → 1.87 → 1.93 (→ 2) |
| Timestep convergence | p = 1.015, 1.013, 1.043 (≈ 1) |

CPU configurations gained exactly the same 45 tests; `release_cuda` gained 9
more, which is precisely the GPU-only encoder suite. That arithmetic is the
check that no test silently failed to register — the P13-007 failure mode.

---

## What P15 actually was

Scoped as a **verification and production-integration closeout**, and that
framing held. The audit found the energy equation already had `ScalarField`
property overloads, `ThermalSolver` already ran a Picard loop over them, and
`cfd::physics::TemperatureProperty` had existed since P3-PHYS-003. What did not
exist was any **model** producing the variation, any enthalpy integral, any
Robin wall, any transient term — and **no case-file route to any of it**.

So the work was: supply the missing numerics, and connect what was already
there. `implementation.md` has the detail.

---

## Four findings worth carrying forward

### 1. A capability the solver had and no case could reach

`ThermalSolver`'s variable-property overload has existed since **P3-PHYS-003**.
No case file could reach it — and after P15's parser work, `physics.json` parsed
a property model and `CaseBuilder` then **dropped it**, solving with a constant.
That is the silent fallback the brief forbids, and it is worse than not parsing
the block: the case file would claim a capability the run does not deliver.

Closed by one inheritance edge —
`TemperatureDependentProperty : public cfd::physics::TemperatureProperty` — plus
the routing it unlocks. No new numerical convention, which was the standing
P14/P15 constraint.

**The general lesson:** "the solver supports X" and "a user can ask for X" are
different claims, and only the second is a capability. Nothing in the test suite
distinguished them until a test drove the real production entry point.

### 2. A refusal that was not merely unhelpful but false

GPU criterion 5 failed. The device refused a Robin wall with *"not
bitwise-reproducible as a constant, a shift or an affine function of phi_P"* —
untrue, since a Robin wall **is** affine in T_P. What it cannot obtain is `k`.
A refusal that misstates its cause sends a user to fix a boundary condition that
is not broken.

The correct reason existed in two plans and not a third, and the third ran
first. Fixed by consolidating into `BoundaryEncoding.hpp`, reproducing every
non-Robin message byte for byte. The probe now fails if the false claim returns.

### 3. Bounds must be derived, and a failed guess is not a reason to raise it

Two thresholds in this phase were guessed and failed their first run. Neither
was simply enlarged:

- the closed-form error bound was replaced by a **measurement** — the refinement
  study established second-order convergence, making `5.266e-2` at nx = 20 the
  correct value, so the bound is that plus ~14 %. It still detects a dropped
  model by a factor of 40.
- the convergence-order criterion was replaced by assertions over the **whole
  sequence**, which is sharper than the single threshold it replaced, not looser.

### 4. A self-check earned its place

The negative-control harness rebuilds the restored tree and re-runs the gates.
It reported a failure while sha256 confirmed a byte-exact restore — a
contradiction worth resolving rather than waving away. The cause was a stale
test of mine pinning a contract I had deliberately reversed earlier in P15; it
had been failing since, and my earlier report of "125 thermal unit tests
passing" was wrong (it was 124/125). It is 126/126 now, and the stale test was
re-pointed at where its guarantee moved rather than deleted.

---

## Deferred, with reasons

| Item | Reason |
|---|---|
| Robin on the GPU | `G = 1/(d/k + 1/h_c)` needs runtime `k`; a device plan is built once, before Γ exists. Option C, closed by the user. Mesh classified unsupported, refused gracefully and honestly. |
| Full thermal CUDA solver | Explicitly out of scope by instruction. |
| `linear` / `table` case models | The format already dispatches on `"model"`; adding them needs a parser branch and nothing else. Unknown names are rejected, never defaulted. |
| Merging the two transient implementations | Blocked by the Robin boundary; becomes possible when Option B lands. |
| `metadata.json` model reporting | Found while the regression was running; sources must not change underneath one. See `known_limitations.md` §1 — reporting only, the solve is correct and proven. |

---

## What P15 does **not** authorise

- **`KNOWN-GOOD` stays `548401a`.** The CI blocker for `3bd32f9` (no runner
  assigned) is unresolved and unrelated to P15. A green local regression is not
  a green CI.
- **No commit or push.** Not authorised.
- **P16 is not started.** P15 closing is a precondition, not an authorisation.

One thing for the next phase to consider, surfaced here and deliberately not
acted on: **`cuda/` is in no CI `find`.** 22 pre-existing format deviations live
there, in files P15 never touched. Same shape as the `cpu-only-build-blind-spot`
finding — a directory no routine gate covers. `regression.md` has the evidence.

---

## Evidence index

| File | Contents |
|---|---|
| `audit.md` | What existed before P15 |
| `equations.md` | The governing equations as implemented |
| `implementation.md` | What was built, and the decisions taken |
| `validation.md` | Every analytical case and what breaks it |
| `conservation.md` | Discrete balance, steady and transient |
| `mesh_convergence.md` | Second order, and the bound it derived |
| `time_convergence.md` | First order in time, bracketed both sides |
| `cpu_gpu_equivalence.md` | Device scope, criteria 1–7, the criterion-5 defect |
| `negative_controls.md` | 8 controls, 8 detected, and the self-check finding |
| `p14_classification.md` | Which assembler owns what, and why |
| `known_limitations.md` | Seven gaps, stated plainly |
| `regression.md` | Final three-configuration regression |
| `robin_architecture_decision.md` | Options A/B/C, and why C |
| `parallelism.md` | How `-j 16` was derived |
| `logs/final/` | Raw logs for everything above |
