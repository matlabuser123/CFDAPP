# TD-001 — IMPLEMENTATION

```text
item      TD-001  Reacting-species production reachability
gate      GATE.md    sha256 0749efbf1120db1fa6689a5b86be2e5045a20722413e6c2770e4605d5c7c319b
amendment GATE_A1.md sha256 53aee7afa7b38193fee1d045d43f35720d038fb8b0b6d7851809c0b9969f43b3
root      results/technical_debt/TD001_reacting_production/audit.md
```

This file records WHAT was built and WHY each decision was forced. The measurements are in
`verification.md`, the controls in `negative_controls.md`, the suite counts in `regression.md`.

---

## 1. The defect, restated in one line

`grep -c 'setup.reacting' src/app/ProjectRunner.cpp` returned **0**. `CaseReader` parsed a
`reacting` block, `CaseBuilder` turned it into live `ReactionMechanism` / `ReactionKinetics` /
`MassFractionState` objects — and `ProjectRunner` never read them. A case file declaring a
mechanism produced a complete result set with `"species": []` and no reaction quantity anywhere
(`reproducer.md` variant 3). Everything up to the builder worked; the dispatch did not exist.

## 2. The change set

| file | layer | what |
| --- | --- | --- |
| `src/io/case/Parsers.hpp` | parser | `parseBoundaryConfig` gained `bool compositionSpecies` |
| `src/io/case/BoundaryConfigParser.cpp` | parser | the `sum_i Y_i = 1` per-patch constraint (gate C11) |
| `src/io/case/PhysicsConfigParser.cpp` | parser | `reacting.time_step` required and validated; `thermal` required alongside `reacting` |
| `include/cfd/io/case/PhysicsConfig.hpp` | config | `ReactingPhysicsConfig::timeStep` |
| `src/io/CaseReader.cpp` | reader | reacting species names reach the boundary parser; `compositionSpecies = true` |
| `include/cfd/io/SimulationSetup.hpp` | setup | `ReactingSetup::timeStep`, `ReactingSetup::compositionBoundaries` |
| `src/io/CaseBuilder.cpp` | builder | both carried through; per-patch per-species boundary fill |
| `include/cfd/app/ProjectRunner.hpp` | app | `ReactingRunResult`, `ProjectRunResult::reactingResult` |
| `src/app/ProjectRunner.cpp` | app | **the dispatch**, plus the reacting species' export metadata |
| `cases/reacting_plug_flow/` | case | the case file that drives it (new) |
| `tests/unit/io/test_reacting_case.cpp` | test | fixture repaired for the two new mandatory keys; 3 tests added |

Nothing in `src/species/` or `include/cfd/species/` was touched by TD-001. The P18 library is
reused, not modified — which is what lets `CFDSpeciesTests` stand as an untouched-library check.

## 3. The dispatch is ONE OPERATOR-SPLIT TIME STEP, and that was forced by measurement

### 3.1 The steady pass was implemented first, and failed in two different ways

The first implementation followed the shape of every other optional physics block in
`ProjectRunner`: a steady transport solve per species carrying its reaction source, then a steady
thermal solve carrying the heat release. Two source treatments were implemented and measured.

```text
cell-averaged source     Y_A = -0.297                        -- a DIFFERENT equation, not an
                                                                approximation: the reaction zone's
                                                                full consumption applied to cells
                                                                with almost no reactant left
per-cell, Patankar       every Y_i >= 0,  max|sum Y - 1| = 3.00e-01
per-cell, fully explicit max|sum Y - 1| = 4.92e-07,  Y_A = -0.297
```

Both per-cell variants are preserved in `verification.md` and both are reused as amendment A1's
negative controls.

### 3.2 Why neither can be repaired within a steady pass

P18.4 guarantees `sum_i omega_i = 0` for the SOURCE. It does not survive the Patankar split of
that source, and the reason is structural rather than a coding error:

```text
consuming species   Sp = omega_i / Y_i  enters the diagonal  -> the term SELF-LIMITS as Y_i falls
producing species   Su = omega_i        stays on the RHS     -> the term runs at the INITIAL rate
```

So the *effective* source the matrix actually applies is asymmetric, and its species sum is not
zero even though the source's is. Making it symmetric means giving up either positivity (go fully
explicit, measured `Y_A = -0.297`) or conservation (keep the split, measured `3.00e-01`). There is
no third source treatment: the asymmetry is what the split is FOR.

### 3.3 Splitting removes both failures, because neither half carries the other's defect

```text
STEP 1  CHEMISTRY, local, over dt
        P18.6's advanceClosedReactor. Applies sum_i omega_i = 0 per cell and recovers T from the
        conserved energy. REFUSES a step that would drive any rho*Y negative rather than clipping
        it -- so positivity is a property of the step, not a repair applied afterwards.

STEP 2  TRANSPORT, over the same dt, carrying NO reaction source
        Convection + diffusion + the implicit-Euler transient term. An M-matrix, so every Y_i
        stays bounded; and because this case's species share one matrix (equal diffusivities,
        equal boundary types on every patch) the summed equations have the constant 1 as their
        EXACT discrete solution -- which is the premise amendment A1 derives criterion C4b from.
```

The temperature half uses P15's **transient** energy assembly, not `ThermalSolver`. `ThermalSolver`
solves the STEADY energy equation; a steady temperature solve inside a split time step would
advance the composition by `dt` while advancing the temperature to infinity. The heat release is
**not** passed to the energy assembly — step 1 already applied it through the reactor's own energy
balance, and passing it again would double-count it.

### 3.4 Why CHEMISTRY FIRST, which is not the order the A1 rationale text describes

Both orders are first-order Lie splittings and neither is more accurate at this order, so the order
is free to be chosen for a structural reason. There is one.

`advanceClosedReactor` consumes a `ReactorState`, which holds a `MassFractionState`, whose
constructor enforces P18.1's `4*eps*N_species` closure. **A transported composition cannot meet
that bound** — its closure is the linear solve's, eleven orders coarser. That is the same wall the
P18 iterate path hit, and the reason `arrheniusProgressRatesFromFields` exists;
`SpeciesTransport.hpp` states the policy directly: *the invariant is enforced at step boundaries,
where it must hold; an iterate is a working vector.*

Chemistry-first puts the validated state exactly at the step boundary. The transport half then
consumes raw fields, through the raw-fields overloads that exist for this purpose.

The two alternatives were rejected:

* **normalise the transported composition** so the constructor accepts it — this is the silent
  renormalisation TD-002's authorization forbids, and it would additionally make C4b **vacuous**,
  driving the measured deviation to `eps` regardless of the solver tolerance;
* **add a raw-fields reactor entry point** mirroring `arrheniusProgressRatesFromFields` — correct,
  but it is library work on `HeatRelease.{hpp,cpp}` outside TD-001's scope, and P18.6 deliberately
  keeps the reactor's arithmetic in one place.

**This does not alter any A1 criterion.** C4a–C4d measure the result, not the order. A1's §2
derivation needs only that the transport half carry no reaction source and that the level fed to
its transient term sum to 1 — which the post-chemistry composition does, to P18.6's reactor
invariant. That adds a floor of order `4*eps*N` to the measured deviation, thirteen orders below
the linear-solve term C4b is testing, so the scaling criterion is unaffected. The deviation from
A1's prose is recorded here rather than silently taken.

### 3.5 Consequence: this is ONE step, and a march needs TD-002

A second step's chemistry half would start from the first step's TRANSPORTED composition and hit
the `MassFractionState` constructor again. So the march is not available until the raw-fields
reactor entry point exists, and that belongs to **TD-002**, which owns the integrator. Recorded in
`ProjectRunner.hpp` beside the scope statement, and listed as a limitation in `summary.md` rather
than left for a reader to discover.

## 4. The time step is derived, not chosen

`time_step` is **required**, not defaulted: a silently chosen `dt` would make the reported
composition depend on a number the case never stated, so two runs of the "same" case could differ
by a build. `cases/reacting_plug_flow` declares `0.1 s`, derived before it was run:

```text
chemistry      k(T0) = A exp(-Ea / R T0) = 5.0 * exp(-30000 / (8.314 * 1000)) = 0.1354 1/s
               tau_c = 1/k                                                    = 7.39 s
convection     dx = L/nx = 1.0/20 = 0.05 m,  u_in = 0.05 m/s
               dx/u                                                           = 1.00 s
dt = min(tau_c, dx/u) / 10                                                    = 0.1 s
```

A tenth of the faster of the two physical timescales. The factor of ten is the conventional
splitting margin and is labelled **empirical**; the two timescales it is applied to are not.

Two a-priori predictions were written down before the case was run, as a check on the dispatch
rather than on the case:

```text
predicted  dY_A = (dt/tau_c) * Y_A,0 = 0.0135 * 0.1          = 1.35e-03
predicted  dT   = dY_A * |h_f,B| / cp = 1.35e-03 * 2e6 / 1000 = 2.71 K
```

Both are confirmed by the measured run in `verification.md`. The agreement is the evidence that the
dispatch applies the Arrhenius law the case declared, and not merely that something changed.

## 5. Two defects found in the test suite while implementing, both reported rather than papered over

### 5.1 A contaminated instrument, revealed by making `time_step` required

`tests/unit/io/test_reacting_case.cpp` builds nine cases from one helper, `reactingPhysics()`. The
moment `time_step` became mandatory the suite went **324/325** — one positive test failed. The
other eight still passed, and that was the problem: six of them are **refusal** tests, and they
were being refused for the MISSING KEY rather than for the defect each one names. They passed and
measured nothing. This is exactly CLAUDE.md §5.3's contaminated instrument, and it was only visible
because one test in the same file failed loudly.

Fixed by adding both now-mandatory keys to the helper, which restores every refusal test's own
instrument.

### 5.2 An ambiguous anchor created by the fix itself

Adding the mandatory `thermal` block gave the fixture a SECOND `"initial_temperature": 298.15`.
`AnUnknownKeyInTheReactingBlockIsRefused` locates its injection point with a plain `find()`, which
now matched the thermal block first — so the test would have injected `mechanism_file` into
`thermal` and passed by measuring the thermal parser. Fixed by searching from the `"reacting": {`
offset, with the reason recorded at the call site.

### 5.3 Three tests added

The two new constraints had no coverage, so the suite would have been relying on the fixture's own
correctness to exercise them:

```text
G8ReactingCaseTest.AReactingBlockWithoutATimeStepIsRefused
G8ReactingCaseTest.ANonPositiveTimeStepIsRefused               (0.0 and -0.125)
G8ReactingCaseTest.AReactingBlockWithoutAThermalBlockIsRefused
```

`CFDIoTests` therefore runs **328**, not 325. The gate's §4 expectation is `all pass; 325 baseline`,
so the delta is stated explicitly rather than absorbed: all 325 baseline tests still pass, and the
three additions are enumerated above. `regression.md` reports both numbers.

## 6. Every linear solve added here checks its status (gate C12)

Four solves are added — one per species plus the temperature half. Each one's
`SolverResult::converged()` is checked, the failure is recorded in `ReactingRunResult`, and the
field falls back to its pre-solve value rather than to the solver's output. P18.7 measured BiCGSTAB
returning `-3.05e12` while itself reporting `Breakdown`; an unchecked status is precisely how a
solver failure becomes a physics result.

A refused chemistry step is treated the same way: `chemistryRefusal` carries P18.6's own
diagnostic, and nothing is repaired.

## 7. Deliberately NOT done

* no stiff integrator, no `d(omega_i)/d(Y_j)`, no sub-stepping — **TD-002**;
* no repository-wide linear-solver audit — **TD-003**; C12 only forbids TD-001 from *adding* an
  unchecked solve;
* no flame, no flame-speed extraction, no G8 closure — **TD-004 / TD-005 / TD-006**;
* `PremixedFlame.{hpp,cpp}` neither used nor validated here;
* no performance claim of any kind.
