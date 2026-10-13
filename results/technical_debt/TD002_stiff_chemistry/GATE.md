# TD-002 — ACCEPTANCE GATE (frozen before implementation)

```text
item        TD-002  Stiff / bounded chemistry integration · P0
scope       (b) as authorized by the user: the drift fix AND the raw-fields entry point AND the
            ProjectRunner wiring, so a MULTI-STEP reacting march is reachable from a case file.
audit       audit.md   (probe sha256 60b2fd42..., log 01_drift_audit.log)
dry-run     logs/02_dryrun_PRE_FREEZE.log
            sha256 f1f8be3bf2077e3229e2d46f02ec164a6ff5f497ec63d44818496144889ef34d
            probe tools/dryrun.cpp -- run against the UNCHANGED baseline BEFORE this freeze
frozen at   git HEAD 3bd32f918656f471e72b5f6ac7cb6c2cbf75a274
```

**This file is frozen before any production change.** No criterion in it may be weakened to obtain a
PASS (`CLAUDE.md` section 1.4, section 5.3). Section 3.2 records what the dry-run established, and
section 3.1 records a derivation error in C3 that the dry-run exposed -- corrected here BEFORE the
freeze rather than after a failure.

---

## 1. Requirement

A reacting case must be integrable **far beyond** the measured round-off budget, from a case file,
with `sum_i Y_i` inside P18.1's frozen invariant at every step and no negative mass fraction.

```text
case file (steps > 1) -> parser -> builder -> ProjectRunner march -> export -> CLI
                                      |
                                      +-- transport half  (TD-001's, unchanged)
                                      +-- chemistry half  (sub-stepped, COMPENSATED, raw-fields)
```

## 2. The architecture the blast radius forces, gated as criterion C8

`include/cfd/gpu/DeviceSpeciesThermochemistry.hpp` lines 690–698 **duplicate** the reactor's
composition update, and P18.8 gates that restatement **BITWISE** against the host. So the
compensation must NOT go into `advanceClosedReactor`.

It does not need to. Compensated summation with the carry initialised to zero computes
`y = d - 0 = d`, then `t = Y + d` — **bitwise the plain update** for the first step. Compensation
only changes steps *after* the first. Therefore:

* `advanceClosedReactor` keeps its arithmetic, with the carry an **optional** in/out parameter that
  defaults to absent. Absent ⇒ today's code path, unchanged;
* the device restatement needs **no change**, and P18.8's bitwise equivalence is untouched;
* TD-001's production dispatch calls the one-step reactor once, so **its measured numbers and its
  bitwise C10 stay valid**;
* `integrateChemistry`, which is host-only and has no device counterpart, owns the carry across
  sub-steps.

## 3. Criteria

Order is execution order; **execution stops at the first failure** (§5.3).

| # | criterion | metric | acceptance threshold |
| --- | --- | --- | --- |
| C1 | the reproducer stops reproducing | `integrateChemistry`, `dt = 20`, `maximumFractionalChange = 0.0125` on P18.7's problem — the exact call that THROWS today | completes; `max_cell abs(sum_i Y_i - 1) <= 4*eps*N_species` |
| C2 | the accuracy control is tightenable | the same `dt` at controls `0.4, 0.2, 0.1, 0.05, 0.0125, 0.003125, 0.00078125` | **every** control completes; each within `4*eps*N`; sub-step count rises monotonically as the control tightens |
| C3 | **a long march, derived to be decisive** | a march of **>= 30 000 accepted sub-steps** on a drift-exercising problem | `4*eps*N_species` at every sub-step (enforced by `MassFractionState` on each, so completion IS the measurement) |
| C4 | positivity | `min_cell min_i Y_i` over the whole march | `>= 0` exactly, **no tolerance** |
| C5 | accuracy still improves by refinement | relative error in `Y_A` and `T` against P18.7's mpmath reference at controls `0.2 / 0.05 / 0.0125` | strictly decreasing, and no worse at each control than the pre-change build |
| C6a | the zero-reaction limit | `pre_exponential = 0` | composition and `T` **bitwise** unchanged; exactly 1 sub-step |
| C6b | the zero-heat-release limit | all `formation_enthalpy = 0` | composition advances; `T` **bitwise** unchanged |
| C7 | determinism | the identical call repeated 3x | **bitwise** identical composition and `T` |
| C8 | **P18.6 and P18.8 are untouched** | `advanceClosedReactor` with no carry, on a swept case set; plus the P18.8 CPU/GPU equivalence tests | **BITWISE** identical to the pre-change build; P18.8's suite unchanged |
| C9 | **the raw-fields entry point does not normalise** | hand the integrator a composition with `abs(sum Y - 1) = d_in`, TD-001's measured transport closure `5.72e-08` | completes; **and** `abs(sum Y_out - 1) >= 0.5 * d_in` — a normaliser would return `~eps` and FAIL this |
| C10 | **a case file drives a MULTI-STEP march** | a `cases/` case with `reacting.steps > 1` through `cfdapp --case` | returns a reacting result; the exported composition **differs** from the `steps = 1` result by more than the single-step change |
| C11 | **TD-001 is preserved** | `cases/reacting_plug_flow` with `steps = 1` | every TD-001 number reproduced: `D = 5.718198e-08`, `min Y = +1.230741e-03`, `max dY = 1.355075e-03`, `max dT = 2.710194 K`, `max S_E = 2.710138e+04` |
| C12 | no unchecked linear solve added | every solve the march introduces | `SolverStatus` checked; breakdown and non-finite never accepted |

### 3.1 Thresholds — derived, not selected

* **C1, C3, C4** reuse P18.1's frozen `4*eps*N_species` and the tolerance-free positivity bound.
  TD-002 invents **no** composition tolerance, and loosening this one is on its forbidden list.
* **C3's 30 000 sub-steps — the derivation, CORRECTED by the dry-run.**

  The draft derived it as "long enough that the uncompensated version exceeds the bound by 100x",
  from the audit's `0.042-0.21 eps/step`. **The dry-run showed that reasoning cannot apply**, for two
  measured reasons:

  1. **The uncompensated version can never miss by 100x.** `MassFractionState` validates every
     sub-step, so it *throws* the moment the deviation crosses `4*eps*N` — the failure mode is a
     refusal, not a large number. No step count produces a `1 200 eps` reading.
  2. **The drift rate on the drift-exercising problem is `0.006 eps/step`** (measured: `6.00 eps` at
     1 000 steps, log 02 section C3), not `0.042`. The audit's rates came from a fixed non-dyadic
     increment; a real reversible march gives a slower, state-dependent rate.

  The correct derivation is a **margin over the crossing point**:

  ```text
  measured plain drift rate       0.006 eps/step    (6.00 eps at 1 000 steps)
  bound                           8 eps             (4*eps*2)
  plain crosses the bound at     ~1 333 steps
  C3 threshold                    30 000 steps      =  22x the crossing point
  ```

  A 22x margin is what makes N1's failure certain rather than lucky: it cannot pass C3 through a
  favourable rounding pattern or a slightly different problem. Measured in the dry-run — plain
  **THREW** at 30 000 steps, compensated reached 30 000 steps at **0.00 eps** — so both directions of
  the non-vacuity requirement are established before the freeze. The factor 22 follows from the
  measured rate and the frozen bound; nothing in it is fitted to a desired outcome.
* **C2's monotone sub-step count** is a non-vacuity guard on C2 itself: a control that is accepted
  but ignored would pass "every control completes" while measuring nothing.
* **C5 is a direction, not a level.** Accuracy is P18.4–P18.6's already-closed gate; C5 only forbids
  the fix from *degrading* it.
* **C8 is BITWISE** because anything less means P18.6's qualified step and P18.8's equivalence moved,
  which no part of TD-002 is authorized to do.
* **C9's `0.5 * d_in` factor** is the anti-normalisation instrument. A normaliser returns `~eps`,
  which is nine orders below `0.5 * 5.72e-08`; an honest integrator returns the closure it was handed
  plus its own `O(eps)` march error. The factor admits an integrator that legitimately changes the
  closure slightly while rejecting one that erases it.
* **C11 is exact reproduction of named figures**, because TD-001's gate is closed and TD-002 must not
  silently move its results.

### 3.2 What the pre-freeze dry-run established, criterion by criterion

`logs/02_dryrun_PRE_FREEZE.log`, against the unchanged baseline:

| # | baseline behaviour | status as an instrument |
| --- | --- | --- |
| C1 | **THREW** -- `deviation 1.9984e-15 > 1.7764e-15` | real; this is the reproducer |
| C2 | reaches controls `0.4, 0.2, 0.1, 0.05` (64, 136, 284, 584 sub-steps, monotone); **THROWS** at `0.0125, 0.003125, 0.00078125` | real; and the monotone rise is present on every rung it reaches |
| C3 | plain **THREW** at 30 000 steps; plain 1 000 steps = `6.00 eps`; compensated 30 000 steps = `0.00 eps` | real, **both directions** -- see section 3.1 |
| C4 | `min Y = +2.710e-04` (plain), `+9.034e-06` (compensated) over the long march | satisfiable; tolerance-free |
| C5 | baseline errors `1.0097e-01 -> 2.1409e-02 -> 2.1436e-03` for controls `0.2 -> 0.05 -> 0.0125` | strictly decreasing already; C5 is a **preservation** criterion, not a defect detector |
| C6a | `pre_exponential = 0`: 1 sub-step, `Y_A = 1`, `T = 1000` | holds on the baseline; **preservation** |
| C6b | `formation = 0`: 4 sub-steps, `Y_A = 0.86842014905921683`, `T = 1000` exactly | holds on the baseline; **preservation** |
| C7 | 3 identical calls bitwise identical | holds on the baseline; **preservation** |
| C9 | `MassFractionState` **REFUSES** a `5.72e-08` closure outright | **the baseline cannot exhibit the behaviour at all** -- see below |

**C9 needs the section 5.2 extra question, and it is answered here.** The baseline has no path that
accepts a transported composition, so a baseline dry-run proves nothing about C9 -- the same shape of
vacuity DIFF-002's W3b had. The two separate questions section 5.2 demands:

* *can a correct post-change implementation satisfy it?* Yes: a raw-fields entry point that carries
  the closure it is handed, exactly as `arrheniusProgressRatesFromFields` carries an iterate.
* *what can fail it?* Control **N2**, a normalising entry point, whose output closure would be
  `~eps` -- nine orders below `0.5 * 5.72e-08`. C9's anti-normalisation clause exists for precisely
  that control, and without N2 the criterion would be untested.

**Three criteria are preservation checks, not defect detectors** (C5, C6, C7): the baseline already
passes them. They are kept because TD-002 must not *break* them, and they are labelled as such
rather than presented as evidence of a fix.

### 3.3 A probe defect the dry-run found

The first dry-run **aborted**: `ReactionKinetics` refuses reverse Arrhenius parameters on a reaction
constructed without `ReactionDirection::Reversible` -- *"an irreversible reaction has no reverse
rate"*. That is the API declining to be used half-way, correctly, and it was a bug in the probe, not
in the library. Recorded because an aborting probe is one edit away from being read as a library
failure.

## 4. Negative controls

| id | control | must produce |
| --- | --- | --- |
| N1 | the plain, uncompensated update (carry disabled) | **C1 and C3 FAIL** -- the primary reproducer. The failure mode is a **refusal**: `MassFractionState` throws the moment the deviation crosses the bound, so N1 fails by throwing rather than by reporting a large number. Measured in the dry-run at 30 000 steps and at the C1 reproducer. |
| N2 | an entry point that normalises its input composition to sum exactly 1 | **C9 FAILS** — proves the anti-normalisation clause is load-bearing and is not satisfied by a normaliser |
| N3 | `pre_exponential = 0` | **C6a** holds while a reacting march still runs — proves C6a measures the limit, not the absence of a march |
| N4 | rates frozen at the first sub-step instead of recomputed | **C5 FAILS** — proves the refinement criterion detects stale rates, the defect `ChemistryIntegrator.hpp` names |
| N5 | the march removed from `ProjectRunner` (revert C10's hook only) | **C10 FAILS** |
| N6 | `steps = 1` on the new march case | its result equals TD-001's single step **bitwise** — proves `steps` is the only thing the march adds |

**N1 and N2 are the two that matter.** N1 is the defect; N2 is the forbidden fix, and C9 exists
specifically so that taking it would be caught rather than rewarded.

## 5. Regression scope

| suite | reason | expectation |
| --- | --- | --- |
| `CFDSpeciesTests` | chemistry + conservation; the library being changed | all pass; **339 baseline** under its own `-G8PremixedFlameTest.*` exclusion |
| `CFDIoTests` | the new case key | all pass; **328 baseline** (325 + TD-001's 3) |
| `CFDAppLayerTests` | `ProjectRunner` behaviour | all pass; **32 baseline** |
| P18.8 CPU/GPU equivalence | the register names it; the device duplicates the update | unchanged — and C8 is why it should be |
| committed `cases/` | §9 backward compatibility | chemistry-off cases **bitwise**; `reacting_plug_flow` reproduces TD-001's numbers |

A changed count in any baseline, or any new test added, is **enumerated** rather than absorbed.

## 6. Definition of done

1. C1–C12 all PASS, measured, numbers recorded;
2. N1–N6 each demonstrated to FAIL what it guards;
3. the regression baselines met, with any delta enumerated;
4. `git diff --check` adds no warning beyond TD-018's three CRLF files;
5. evidence written: `audit.md`, `GATE.md`, `reproducer.md`, `implementation.md`,
   `negative_controls.md`, `verification.md`, `regression.md`, `summary.md`;
6. `DEBT_REGISTER.md` updated OPEN -> RESOLVED **after** the above.

## 7. Explicitly NOT in this gate

* **an implicit integrator, `d(omega_i)/d(Y_j)`, or a Newton solve.** Audit §4 found no measurement
  demanding one: the defect is round-off accumulation, not stiffness, and accuracy still improves
  under refinement for the mechanisms in scope. Building a Jacobian on speculation would be a
  separate, larger, separately-gateable piece of numerics.
* the repository-wide linear-solver audit — **TD-003**. C12 only forbids TD-002 from *adding* an
  unchecked solve.
* flame, flame-speed extraction, G8 closure — **TD-004 / TD-005 / TD-006**.
* the `5.72e-08` transport closure itself. TD-001 root-caused it to the flow solver's residual
  continuity error; TD-002 must **carry** it without erasing it (C9), not reduce it.
* any performance claim.
