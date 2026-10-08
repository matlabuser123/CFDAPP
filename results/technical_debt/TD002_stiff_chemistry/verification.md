# TD-002 — VERIFICATION — **ALL CRITERIA PASS**

```text
item      TD-002  Stiff / bounded chemistry integration · P0 · scope (b)
gate      GATE.md sha256 287c7027bc7db99f0d590eaad1b35388803ffec9dfdd182a2af584bf00e581e5
dry-run   logs/02_dryrun_PRE_FREEZE.log  f1f8be3b…  (BEFORE the freeze)
logs      03_library_criteria.log · 04_negative_controls.log · 05_c8_two_library_and_regression.log
verdict   C1-C12 all PASS; N1-N6 each FAIL what they guard; 331 / 32 / 343
```

---

## 1. Criteria

| # | criterion | measured | verdict |
| --- | --- | --- | --- |
| C1 | the reproducer stops reproducing | `dt = 20`, control `0.0125`: **2 445 sub-steps**, deviation `0.000000e+00` (baseline THREW) | **PASS** |
| C2 | the accuracy control is tightenable | all 7 rungs complete; sub-steps **64 → 136 → 284 → 584 → 2 445 → 10 217 → 42 632**, monotone; `0.00 eps` at every rung | **PASS** |
| C3 | a long march ≥ 30 000 sub-steps | **85 472 sub-steps**, worst `abs(sum Y - 1) = 0.000000e+00`; sweep clean to **324 430** sub-steps | **PASS** (2.85× threshold) |
| C4 | positivity, tolerance-free | `min_cell min_i Y_i = +5.00e-01` over the long march | **PASS** |
| C5 | refinement still improves | `1.0097e-01 → 2.1409e-02 → 2.1436e-03`, identical to the baseline level | **PASS** |
| C6a | zero-reaction limit | `pre_exponential = 0`: 1 sub-step, `Y_A = 1`, `T = 1000` | **PASS** |
| C6b | zero-heat-release limit | `formation = 0`: `Y_A = 0.86842014905921683`, `T = 1000` exactly | **PASS** |
| C7 | determinism | 3 identical calls bitwise identical | **PASS** |
| C8 | **P18.6 / P18.8 untouched** | **two-library** sweep, 140 states: both dumps sha256 `23539956869a2f74` — **BITWISE IDENTICAL**; all P18.8 equivalence tests pass | **PASS** |
| C9 | raw-fields path does not normalise | deviation in `5.7182e-08` → out `5.7182e-08`, **ratio 1.000000**; required `>= 0.5 × in` | **PASS** |
| C10 | a case file drives a multi-step march | `cases/reacting_plug_flow_march` (`steps: 20`): `max abs(dY)` `1.355075e-03 → 2.578216e-02` (**19.03×**), `max abs(dT)` `2.710194 → 51.564661 K` (**19.02×**) | **PASS** |
| C11 | **TD-001 preserved** | at `steps: 1`, all five figures **BITWISE**: `D = 5.718197704229766e-08`, `min Y = 0.0012307413645062176`, `max dY = 0.0013550746114409651`, `max dT = 2.7101938457000188`, `max S_E = 27101.379420084206` | **PASS** |
| C12 | no unchecked linear solve | every solve's `converged()` checked; status, iterations, residual exported | **PASS** |

### 1.1 C8 is established by two libraries, not by reading the branch

`CLAUDE.md` §6: *"Attribution needs two libraries."* C8 claims `advanceClosedReactor` is unchanged
when no carry is supplied. Rather than argue about which branch the compiler emits, the library was
built **both ways** — with the carry parameter and with its store branch removed — and a 140-state
sweep (7 temperatures × 5 compositions × 4 `dt`, 17 digits each) dumped from each:

```text
with    the carry parameter   140 states   sha256 23539956869a2f74
without the carry parameter   140 states   sha256 23539956869a2f74
```

Identical hash. `HeatRelease.cpp` was then restored and its sha256 verified against the pre-variant
value. This is what makes "P18.6's qualified step and P18.8's device restatement are untouched" a
measurement.

### 1.2 Why `steps: 1` reduces to TD-001 bitwise, and why that is a property rather than a case

Three things coincide, each by construction:

* the requested `dt` is inside the chemical limit, so the integrator takes **exactly one sub-step**;
* that sub-step is `advanceClosedReactor` with a **zero** carry, and `y = d - 0` is exactly `d`, so
  the store is exactly `fl(Y + d)`;
* the case's initial composition closes **exactly**, so the raw-fields wrapper's shift is exactly
  zero and adding it back changes nothing.

C11 measured the consequence rather than assuming it, and control **N6** confirms it from the other
direction: `steps: 1` on the *march* case is bitwise equal to `cases/reacting_plug_flow`.

## 2. Negative controls

| id | guards | required | measured | verdict |
| --- | --- | --- | --- | --- |
| N1 | C1, C3 | fail | `compensateRoundOff = false`: the 85 472-step march is **REFUSED** (`sum to 0.99999999999999811`). A settings flip, so the control and production are the same binary | **fails** |
| N2 | C9 | fail | a **normalising** entry point returns deviation `2.220446e-16` for an input of `5.7182e-08` — ratio `3.88e-09`, nine orders below the `0.5×` bound | **fails** |
| N3 | C6a | fail | `pre_exponential = 0` on a 20-step march: `max S_E = 0.000000e+00` | **fails** |
| N4 | C5 | fail | rates frozen at the first sub-step: error **stuck at `0.0473867`** across controls `0.2 / 0.05 / 0.0125` — refinement stops working | **fails** |
| N5 | C10 | fail | march removed: no `mass_fraction_*` columns, `metadata species: []` | **fails** |
| N6 | — | `steps: 1` ≡ TD-001 | every exported reacting value **bitwise identical** | **PASS, as required** |

**N2 and N4 are the two that matter.** N2 is the forbidden fix, and C9 exists so that taking it
would be caught rather than rewarded — a normaliser's `~eps` output is unmistakable. N4 is the defect
`ChemistryIntegrator.hpp` warned about in prose; C5 now detects it by measurement.

All source variants were reverted with **sha256 proved** against the pre-variant hashes
(`ChemistryIntegrator.cpp`, `ProjectRunner.cpp`, `HeatRelease.cpp`).

## 3. The two numbers that make this item worth doing

```text
before   the accuracy control could not be tightened past 0.05 without the integrator THROWING,
         and a march was refused after ~1 333 steps
after    85 472 sub-steps at 0.00 eps, and every control rung down to 0.00078125 completes
```

## 4. What is still true

* **No implicit integrator and no `d(omega_i)/d(Y_j)`.** The audit found the defect to be round-off
  accumulation, not stiffness, and accuracy still improves under refinement for the mechanisms in
  scope. Building a Jacobian on speculation was explicitly left out of the gate (§7).
* **The `5.72e-08` transport closure is carried, not reduced.** TD-001 root-caused it to the flow
  solver's residual continuity error; TD-002's job was to carry it through the chemistry without
  erasing it, which C9 measures and N2 guards.
* **`stepsTaken` and `chemistrySubSteps` are reported in `ReactingRunResult` but not exported to
  `metadata.json`.** No criterion requires it; recorded as a gap rather than silently widened.
* **No flame, no flame-speed extraction, no G8 closure** — TD-004 / TD-005 / TD-006.
* **No performance claim.**
