# TD-004 A6 — C6a-bal PASSES, C6a-ord is UNRESOLVED. The gate still stops at C6a, now for a stated reason.

```text
STATUS:      C6a-bal SATISFIED. C6a-ord UNRESOLVED, which GATE_A6 §3(a) defines as a STOP and not
             a pass. C6a therefore does NOT pass, the gate STOPS at C6a, and C6b and C7 are NOT
             evaluated. TD-004 remains OPEN / STOPPED on two blockers.
SCOPE:       C6a only. No production change; TD-004's only production change is still the A2
             locator fix.
GATE:        GATE_A6.md 7bedb728db0332ab0a938188ef4055b722331251f98e2e3955ffaacd80b3f8fb
             frozen in logs/A6_00_freeze.log, RECOMPUTED at the start of this run and matched.
             It amends C6a of GATE.md 12ccc5dd…, which is otherwise unchanged.
RESULT:      admissibility ADMISSIBLE · C6a-bal PASS · C6a-ord UNRESOLVED
EVIDENCE:    logs/A6_03_verification.log  (fresh; reuses no earlier result, §5.3 step 6)
             logs/A6_01_species_suite_regression.log  (C7's question, answered independently)
             logs/A6_02_thousand_cell.log  (why C6a's cheap triplet is forced)
REGRESSION:  NOT RUN. No production change, and TD-035 blocks the §8 ladder.
COMMIT:      none — not authorized.
```

---

## 1. The fresh run reproduces the dry-run to the digit

```text
cells    S_L cm/s     fuel bal   res cells  converged   wall s      dry-run (logs/A5_01)
   63     4.51340   5.1972e-01       5.07        yes        7       4.51340 / 0.5197
  125     4.90527   1.5048e-01      12.62        yes       17       4.90527 / 0.1505
  250     4.94863   2.8278e-02      29.06        yes       88       4.94863 / 0.0283
  500     4.95533   6.3030e-03      59.82        yes      713       4.95533 / 0.0063
```

Identical `S_L` at every level and balances agreeing to their printed precision, on a separately
built binary. **The fixture is reproducible**, which is reported rather than assumed — and it is the
precondition for reading anything else here.

## 2. The verdicts, in GATE_A6's order

### 2.1 Admissibility — carried over verbatim from GATE.md, and it holds

```text
every level converged                              yes
every level > 4 cells of front   12.62, 29.06, 59.82   yes
differences shrink    |6.7040e-03| < |4.3356e-02|      yes
no sign change                                     yes
-> ADMISSIBLE
```

### 2.2 C6a-bal — PASS

```text
125 cells   1.5048e-01  <  0.3200   PASS
250 cells   2.8278e-02  <  0.0800   PASS
500 cells   6.3030e-03  <  0.0200   PASS
```

The clause that previously failed two of three levels now passes all three, **and the 500-cell bound
is the unchanged `0.02`** — so the pass at the reference configuration is not an artefact of the
amendment. The amendment corrected a dimensional error at the coarse levels and left the reference
alone.

### 2.3 C6a-ord — UNRESOLVED

```text
quantum at the middle level     3.350868e-03 cm/s
differences in quanta           n1 = 12.94   n2 = 2.00     floor = 4
-> UNRESOLVED
```

`n2 = 2.00` is below the derived 4-quantum floor, so the observed order **cannot be determined at
this tolerance**. Per GATE_A6 §3(a) that is a STOP.

**This is not a pass and must not be read as one.** It is also not the FAIL that A5 recorded: the
measurement is not outside the band, it is incapable of saying whether it is inside. The honest
verdict is the third state the amendment exists to express.

## 3. THE STOP

```text
C6a does not pass. The frozen order is C6 -> C6a -> C6b -> C7, so the gate STOPS at C6a and
C6b and C7 are NOT evaluated.
```

> **UPDATED 2026-10-10 — BLOCKER 1 IS DISCHARGED.** TD-040's premise was refuted by its own
> dead-band ladder (`S_L` identical at every dead band including recentring OFF), so translation
> does not bias the eigenvalue and the stop's chain breaks at its third link. The dt ladder then
> excluded the pre-fix eigenvalue, so the +6.07 % move was TOWARD the verified limit. TD-040 is
> reclassified N/A as a defect and **no longer carries this stop**. See
> `TD040_recentre_conservation/verification_disposition.md` §4. ONE blocker remains: C6a-ord.

TD-004 still carries **two** independent blockers:

```text
TD-040   the S_L +6.07 % stop, registered in A2 before the run. TD-040's dt ladder has since
         EXCLUDED the pre-fix eigenvalue, so the move was toward the truth -- but the recentring
         operator's conservation defect remains unfixed, and that is what the stop is about.
C6a-ord  the order is unresolvable at settings.tolerance = 1e-3. Resolving it needs a finer
         tolerance (TD-043), which is a production change outside this gate -- and which the
         evidence suggests would CONFIRM a band violation at p ~ 2.69 rather than clear it.
```

## 4. What the amendment achieved, stated precisely

```text
BEFORE   C6a FAILED on two clauses. One failed on the BASELINE at all three levels (a broken
         criterion), and the other was computed from a fine difference of 2 lattice quanta, giving
         an order interval (2.00, 3.81) whose band edge lay inside it. Neither failure said
         anything about the solver.
AFTER    C6a-bal PASSES against a bound derived from the scheme's formal order, with the reference
         threshold unchanged and the baseline still failing all three levels. C6a-ord reports
         UNRESOLVED with a derived floor and a named remedy.
```

**The gate is in the same place and the record now says why.** That is the whole of what this
amendment bought: it did not unblock anything, and GATE_A6 §4 said so before it was frozen.

## 5. The two things recorded alongside, neither a gate verdict

```text
C7's question    CFDSpeciesTests excluding G8PremixedFlameTest: 343 tests, 343 PASSED, 14 863 ms,
                 against C7's recorded 343-test baseline. EXACT match -- no silent change elsewhere
                 in the species suite. NOT a C7 verdict: the gate stopped at C6a. (logs/A6_01)
the 1000-cell    tested against the REAL ctest --timeout 7200 cap and KILLED at exit 124. So the
level            expensive {250,500,1000} triplet is unaffordable and C6a's cheap triplet is
                 FORCED. logs/07 had ruled it out at 1750 s on a pipeline's masked `exit 0`; both
                 defects are corrected and the conclusion survives. (logs/A6_02)
```

## 6. What is NOT established

```text
NOT established  that p ~ 2.69 is the family's true order, or that it violates the band. C6a-ord
                 reports UNRESOLVED precisely because this measurement cannot say.
NOT established  that a finer tolerance would make C6a pass. The point estimate and the interval
                 both sit above 2.5, so the likelier outcome is a DEFINITE failure. Tightening
                 tolerance is a remedy for the UNRESOLVED state, not for the order.
NOT established  that h^2 is the balance's true rate. Measured 2.17-2.41 over two intervals on the
                 fixed tree; h^2 is used as a conservative formal-order bound.
NOT established  that 63 cells belongs in any family. It passes C6a's own resolution guard (5.07
                 cells of front) yet {63,125,250} gives p = 3.176 here and p = 0.523 pre-fix, so
                 it fails on BOTH libraries. The asymptotic range starts above 63 cells. Recorded
                 because it bounds any future attempt to cheapen the fixture.
NOT evaluated    C6b and C7 as gate criteria. The gate stopped.
NOT run          the §8 regression. No production change, and TD-035 blocks it.
NOT closed       TD-004. Two blockers, §3.
```

**§6's fourth item is the one to carry forward.** The cheap triplet is forced by the cap, the
expensive one is unaffordable, and the family's asymptotic range begins somewhere above 63 cells and
below 125 — which is a narrow window for a criterion that needs three self-similar levels inside it.
