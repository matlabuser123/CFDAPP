# TD-002 — IMPLEMENTATION

```text
item   TD-002  Stiff / bounded chemistry integration · P0 · scope (b) as authorized
gate   GATE.md sha256 287c7027bc7db99f0d590eaad1b35388803ffec9dfdd182a2af584bf00e581e5
```

---

## 1. The defect, in one line

`sum_i Y_i` walked away from 1 **linearly** over a march, and `MassFractionState` -- which validates
every sub-step -- refused the state once it crossed `4*eps*N`. So a long march did not return a
drifted answer, it **stopped**; and tightening the accuracy control below `0.05` made the integrator
throw. The cause is the rounding of the `N` separate stores `fl(Y_i + d_i)`, whose errors are
sign-biased and do not cancel. The increments themselves sum to exactly zero, so it was never a
closure defect in `omega`.

## 2. The change set

| file | what |
| --- | --- |
| `include/cfd/species/HeatRelease.hpp` | `advanceClosedReactor` gained an **optional** `roundOffCarry` (defaults to `nullptr`) |
| `src/species/HeatRelease.cpp` | compensated store behind that pointer; sized and zeroed if the caller hands in a fresh vector |
| `include/cfd/species/ChemistryIntegrator.hpp` | `compensateRoundOff` (default **true**), `worstSumDeviation`, and the `integrateChemistryFromFields` entry point |
| `src/species/ChemistryIntegrator.cpp` | the carry lives for the whole march; **rolled back on a refusal**; the raw-fields path |
| `include/cfd/io/case/PhysicsConfig.hpp`, `src/io/case/PhysicsConfigParser.cpp` | `reacting.steps`, required and validated as a whole number in `[1, 1e6]` |
| `include/cfd/io/SimulationSetup.hpp`, `src/io/CaseBuilder.cpp` | `steps` carried to the setup |
| `include/cfd/app/ProjectRunner.hpp` | `stepsTaken`, `chemistrySubSteps`, `chemistryRefusals`, `maxChemistryInputSumDeviation` |
| `src/app/ProjectRunner.cpp` | TD-001's single split step became a **march** of `reacting.steps` |
| `cases/reacting_plug_flow/physics.json` | `steps: 1` -- reproduces TD-001 bitwise (C11) |
| `cases/reacting_plug_flow_march/` | new: `steps: 20` (C10) |
| `tests/unit/io/test_reacting_case.cpp` | fixture repaired, value asserted, **3** refusal tests added |
| `tests/unit/species/test_chemistry_integrator.cpp` | the loosened assertion **tightened**, **4** tests added |

## 3. The architecture was decided by a blast-radius check, before any code

`include/cfd/gpu/DeviceSpeciesThermochemistry.hpp` lines 690-698 **duplicate** the reactor's
composition update, and P18.8 gates that restatement **BITWISE** against the host. So the
compensation could not go into `advanceClosedReactor`'s arithmetic.

It did not need to. Compensated summation with the carry at zero computes `y = d - 0`, which is
exactly `d`, so the store is exactly `fl(Y + d)` -- **the first step is bitwise the plain update**,
and only later steps differ. That single identity is what let all four of these hold at once:

* P18.6's qualified step keeps its arithmetic (C8, proved with **two libraries**);
* the device restatement needs **no change**, so P18.8's bitwise equivalence is untouched;
* TD-001's production dispatch, which calls the one-step reactor once, stays **bitwise** (C11);
* a long march becomes possible, because the integrator owns the carry across its sub-steps.

The carry is therefore an **optional parameter** on the one-step reactor and **state** in the
integrator -- not a flag, not a global, and not a second copy of the update.

### 3.1 One subtlety that needed its own handling: the carry must be rolled back on a refusal

`advanceClosedReactor` mutates the carry as it stores each species, and a refusal can happen partway
through a cell -- so after a refusal the carry may hold bits belonging to a step that was never
accepted. Applying those to the retried (smaller) step would corrupt the very invariant the carry
exists to protect. Zeroing is the correct rollback rather than save-and-restore: zero is the
identity, so the retried step behaves exactly as a fresh march would, losing only the compensation
earned before the refusal.

## 4. Why the raw-fields entry point SHIFTS rather than normalises

A transported composition closes only to the linear solver's tolerance -- TD-001 measured
`5.718198e-08` -- so `MassFractionState` refuses it and no march could start from it.

The entry point shifts each cell onto an exactly-closing working vector, integrates, and **adds the
same shift back**. The shift is the cell's own deviation, distributed over the species in proportion
to their mass, so it returns to where it came from.

That is not normalising. Normalising would **rescale** and discard the deviation; this subtracts a
known quantity and adds it back, so the deviation is conserved. The difference is measurable, and
C9 measures it: the production path returns `ratio 1.000000`, while control **N2** -- an entry point
that normalises -- returns `2.220446e-16` against an input of `5.7182e-08`, a ratio of `3.88e-09`.
Erasing the closure would have made every downstream conservation figure meaningless, because the
number that disappeared is the **flow solver's** residual continuity error, not the chemistry's.

## 5. `reacting.steps` is required, and that caught the same trap twice

Required rather than defaulted, for the reason TD-001 gave for `time_step`: the integration extent
decides the reported composition, so a silently chosen value makes two runs of the "same" case differ
by a build.

Making it required took `CFDIoTests` to **327/328** with only the positive test failing -- while the
six refusal tests went on passing, refused for the **missing key** instead of for the defect each one
names. That is the second time one new key has done this to `test_reacting_case.cpp` (`time_step` did
it in TD-001). Both keys now live in the fixture helper, and the step count is asserted against
`kSteps = 3` rather than `1`, so a setup that silently defaulted to a single step fails there.

## 6. One assertion was TIGHTENED, which is the opposite of the forbidden fix

`test_chemistry_integrator.cpp` asserted `EXPECT_NEAR(a + b, 1.0, 1.0e-14)` -- **5.6x looser** than
the `4*eps*2` that `MassFractionState` enforces on every sub-step, so it asserted less than the code
already guaranteed. TD-002's audit found it while asking why a loosened `sum Y` tolerance was in the
suite at all. It now asserts `massFractionSumTolerance(2)`. Loosening that tolerance is on TD-002's
forbidden list; tightening it to the frozen invariant is the point of the item.

## 7. Deliberately NOT done

* no implicit integrator, no `d(omega_i)/d(Y_j)`, no Newton solve -- the audit found round-off, not
  stiffness, and the gate excluded it (§7);
* no change to the `5.72e-08` transport closure itself -- TD-002 carries it, TD-001 root-caused it,
  and reducing it means converging the flow harder;
* no export of `stepsTaken` / `chemistrySubSteps` to `metadata.json` -- no criterion requires it, and
  scope was already widened once to (b); recorded as a gap in `summary.md`;
* no flame, no flame-speed extraction, no G8 closure -- TD-004 / TD-005 / TD-006;
* no performance claim.
