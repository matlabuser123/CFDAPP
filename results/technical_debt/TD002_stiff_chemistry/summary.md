# TD-002 — SUMMARY — **RESOLVED** (against its frozen gate; see §5 for what was carved out)

```text
item      TD-002  Stiff / bounded chemistry integration · P0 · scope (b) as authorized
gate      GATE.md sha256 287c7027bc7db99f0d590eaad1b35388803ffec9dfdd182a2af584bf00e581e5
verdict   C1-C12 all PASS · N1-N6 each fail what they guard · 331 / 32 / 343
git HEAD  3bd32f918656f471e72b5f6ac7cb6c2cbf75a274   (no commit, none authorized)
```

---

## 1. What was wrong, and what it cost

`sum_i Y_i` drifted linearly over a march and `MassFractionState` — which validates **every**
sub-step — refused the state once it crossed `4*eps*N`. So a long march never returned a drifted
answer; it **stopped**. Concretely: **tightening the accuracy control made the integrator throw**.

```text
before    control 0.05 -> 584 sub-steps ok;  0.0125 / 0.003125 / 0.00078125 -> THREW
          a drift-exercising march was REFUSED after ~1 333 steps
after     85 472 sub-steps at 0.00 eps;  every rung down to 0.00078125 completes
          sweep clean to 324 430 sub-steps
```

The cause was not stiffness and not a closure defect in `omega` — the increments sum to exactly zero.
It was the rounding of the `N` separate stores `fl(Y_i + d_i)`, sign-biased so the errors accumulate
instead of cancelling. The fix carries the low-order bits each store cannot represent into the next
increment, so **nothing is discarded** and `sum_i Y_i = 1` follows from `sum_i omega_i = 0` rather
than being imposed. Not clipping, not normalisation, not a loosened tolerance — all three forbidden,
and all three act *on* the sum, which this never touches.

## 2. The architecture was decided by a blast-radius check before any code was written

`DeviceSpeciesThermochemistry.hpp` **duplicates** the reactor's composition update and P18.8 gates it
**bitwise**. Compensation with the carry at zero computes `y = d - 0 = d`, so the store is exactly
`fl(Y + d)` — the first step is bitwise the plain update. That one identity let four things hold at
once:

```text
P18.6's qualified step   unchanged -- C8, proved with TWO LIBRARIES (140 states, same sha256)
P18.8 CPU/GPU            untouched -- the device restatement needed no edit at all
TD-001's dispatch        bitwise  -- C11, all five recorded figures reproduced exactly
a long march             possible -- the integrator owns the carry across its sub-steps
```

So the carry is an **optional parameter** on the one-step reactor and **state** in the integrator.
One subtlety needed its own handling: the carry is **rolled back on a refusal**, because a refusal can
happen partway through a cell and those bits belong to a step that was never accepted.

## 3. Scope (b): a march is now reachable from a case file

TD-001 shipped one split step and recorded why it could not march — step 2's chemistry would start
from a **transported** composition, closing only to `5.718198e-08`, which `MassFractionState`
refuses. `integrateChemistryFromFields` shifts each cell onto an exactly-closing working vector,
integrates, and **adds the same shift back**, so the closure is conserved rather than erased.

That distinction is measured, not asserted:

```text
production         deviation in 5.7182e-08  ->  out 5.7182e-08   ratio 1.000000      C9 PASS
N2 (normalising)   deviation in 5.7182e-08  ->  out 2.220446e-16  ratio 3.88e-09     C9 FAILS
```

`cases/reacting_plug_flow_march` (`steps: 20`) drives it through the real CLI: `max abs(dY)` rises
`19.03x` and `max abs(dT)` `19.02x` over 20 steps — under 20 because the fuel depletes, which is
correct.

## 4. Two defects found in the suite, both reported rather than papered over

* **The loosened assertion.** `test_chemistry_integrator.cpp` asserted `sum Y` to `1.0e-14`, **5.6x
  looser** than the `4*eps*2` the state class already enforced — it asserted less than the code
  guaranteed. Now tightened to `massFractionSumTolerance(2)`. Loosening that is forbidden; tightening
  it is the point of the item.
* **The same contamination trap, a second time.** Making `reacting.steps` required took `CFDIoTests`
  to **327/328** with only the positive test failing, while six refusal tests went on passing —
  refused for the **missing key** instead of for the defect each names. `time_step` did exactly this
  in TD-001. Both keys now live in the fixture helper.

## 5. What is NOT closed, and the honest effect on the count

**The register files two different problems under TD-002**: the step budget, and
*"`d(omega_i)/d(Y_j)` does not exist, so the Patankar `Sp` is zero"*. Its own **gate** text requires
only the budget, and TD-002's frozen gate excluded the Jacobian explicitly (audit §4, gate §7) —
because the audit found the defect to be round-off, not stiffness, and no measurement demanded an
implicit method for the mechanisms in scope. Accuracy still improves under refinement
(`1.0097e-01 -> 2.1409e-02 -> 2.1436e-03`).

So the budget half is **RESOLVED** and the Jacobian half is **carved out as TD-019, OPEN**. The open
count therefore goes `13 -> 13`, not `13 -> 12`. Stating that plainly matters more than showing a
reduction: three TODO.md lines ("the implicit Patankar `Sp` is still zero", recorded by P18.4, P18.5
and P18.6) belong to TD-019 and **must not** be checked.

Also still true:

* the `5.72e-08` transport closure is **carried, not reduced** — TD-001 root-caused it to the flow
  solver's residual continuity error;
* `stepsTaken` / `chemistrySubSteps` are in `ReactingRunResult` but **not exported** to
  `metadata.json`. No criterion requires it and scope was already widened once; recorded as a gap;
* no flame, no flame-speed extraction, no G8 closure — TD-004 / TD-005 / TD-006;
* no performance claim; no GUI claim.

## 6. Required final report

```text
STATUS:            TD-002 RESOLVED against GATE.md 287c7027…
                   The d(omega)/d(Y) half of the register's TD-002 entry is carved out as TD-019, OPEN.
SCOPE:             (b) -- the drift fix, the raw-fields entry point, and the ProjectRunner march.
                   NOT done: implicit integrator / Jacobian (TD-019), TD-003, TD-004/5/6,
                   metadata export of the march counters, any performance or GUI claim.
GATE:              GATE.md 287c7027bc7db99f0d590eaad1b35388803ffec9dfdd182a2af584bf00e581e5
RESULT:            C1 2 445 sub-steps/0.00 eps · C2 all 7 rungs, 64->42 632 monotone · C3 85 472
                   sub-steps/0.00 eps · C4 min Y +5.00e-01 · C5 refinement preserved · C6a/C6b
                   unchanged · C7 bitwise · C8 TWO-LIBRARY bitwise, 140 states · C9 ratio 1.000000
                   · C10 19.03x / 19.02x · C11 five figures BITWISE · C12 checked.  None failed.

FILES CHANGED:     10 production, 2 test, 1 case edited, 1 case added
EVIDENCE:          results/technical_debt/TD002_stiff_chemistry/

FOCUSED TESTS:     G8ChemistryIntegratorTest 13/13 · G8ReactingCaseTest 15/15
REGRESSION:        CFDIoTests 331/331 (328 + 3 enumerated) · CFDAppLayerTests 32/32
                   CFDSpeciesTests 343/343 (339 + 4 enumerated), baseline conditions
                   Debug+GUI, sanitizers, clang-format, full ctest: NOT RUN (regression.md §4)
SANITIZERS:        NOT RUN (no commit authorized; one vector-of-vector added, no raw ownership)

KNOWN FAILURES:    G8PremixedFlameTest -- TD-004 territory, excluded from the 339 baseline by that
                   baseline's own record. Not measured or relied on here.
NEW FAILURES:      none

GIT HEAD:          3bd32f918656f471e72b5f6ac7cb6c2cbf75a274
GIT DIFF --CHECK:  clean apart from TD-018's three pre-existing CRLF files
TODO UPDATED:      yes -- reconciled against the register; see §5 on which boxes must stay unchecked
ROADMAP UPDATED:   no -- proposed, not written unasked
COMMIT:            none
PUSH:              none
```
