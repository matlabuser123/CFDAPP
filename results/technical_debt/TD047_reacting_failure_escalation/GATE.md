# TD-047 — frozen gate. A failed reacting solve must reach the exit code AND the report.

```text
SCOPE        src/app/ProjectRunner.cpp + include/cfd/app/ProjectRunner.hpp: escalate
             out.status when the reacting march had a failed linear solve, via a new PURE
             function so the rule is unit-testable without engineering a failing solve.
             apps/cli/main.cpp: report the reacting block, which it currently does not do at all.
             tests/unit/app/test_project_runner.cpp: the truth table + the CLI-visible fields.
FROZEN       before any edit.
```

---

## 1. THE DEFECT, CORRECTED — it is worse than TD-047 was registered as

TD-047 was registered saying *"Only the per-species status reaches stdout."* **That is wrong, and
this gate corrects it before fixing anything.**

```text
include/cfd/app/ProjectRunner.hpp:326   ProjectRunResult::speciesResults   <- what the CLI prints
include/cfd/app/ProjectRunner.hpp:151   ReactingRunResult::speciesResults  <- what reacting fills
src/app/ProjectRunner.cpp:696           out.speciesResults.push_back(...)  NON-reacting path
src/app/ProjectRunner.cpp:1000          run.speciesResults.push_back(...)  REACTING path
apps/cli/main.cpp                       greps ZERO times for "reacting"
```

They are **different fields on different structs**. The reacting branch fills
`ReactingRunResult::speciesResults`; `printReport` iterates `ProjectRunResult::speciesResults`,
which a reacting run leaves empty. So:

```text
exit code    out.status is set ONCE at :501 from the FLOW solve and never revisited
stdout       the CLI never mentions the reacting block in any form
the field    run.anyLinearSolveFailed has ONE writer (:1017) and ZERO readers anywhere
=> a reacting march whose every linear solve FAILED is invisible in BOTH channels.
```

## 2. THE FIX, AND WHY THIS SHAPE

### 2.1 The status rule is DERIVED from how the flow's own failures are already treated

`statusFor(SIMPLEStatus)` at `:164`-`:185` maps `MomentumFailure` and
`PressureCorrectionFailure` — the flow's own linear-solve failures — to
`ProjectRunStatus::NumericalFailure` (exit 4). A reacting linear-solve failure is the same kind
of event, so it maps the same way. **Nothing is invented; the existing mapping is extended to a
path it was never applied to.**

`Cancelled` is preserved: a cancelled run's reacting failure is a consequence of the
cancellation, not an independent fault, and `Cancelled` is the more informative verdict.

### 2.2 It goes in a PURE function so it can be tested without a failing solve

No test anywhere drives the reacting path end-to-end (section 3.2), and engineering a reacting
case whose linear solve genuinely fails is a research task, not a test fixture. So the rule is
extracted:

```cpp
ProjectRunStatus statusWithReactingOutcome(ProjectRunStatus flowStatus, bool reactingSolveFailed);
```

which is exhaustively unit-testable over its 6 x 2 input space. The call site then reads the
field, which also discharges the "zero readers" half of the defect.

**This is deliberately NOT a test that makes a solve fail.** Such a test would be better, and it
is recorded as not established in section 5.

## 3. CRITERIA — frozen in this order, stop at the first failure

```text
C1  statusWithReactingOutcome is exhaustive and correct over all 6 ProjectRunStatus values
    x {false, true}:
      reactingSolveFailed == false  ->  returns flowStatus UNCHANGED, for all six
      reactingSolveFailed == true   ->  returns NumericalFailure, EXCEPT Cancelled which is
                                        preserved
C2  exitCodeFor(statusWithReactingOutcome(Converged, true)) == 4, where today it is 0.
    This is the defect's headline, stated as a number.
C3  NO FALSE POSITIVE: a reacting run with every solve converged still yields Converged and
    exit 0. Verified against the committed case's own recorded metadata, which reports all
    three species, thermal and solver as Converged -- so reactingSolveFailed is false there
    and that case's verdict cannot move. See section 3.1.
C4  the CLI reports the reacting block: per-species status from ReactingRunResult (the field
    it actually fills), the temperature solve, and the aggregate flag. Asserted on the
    report STRING, not on a golden file.
C5  run.anyLinearSolveFailed has at least one reader in src/. Grep-checkable; it is the
    defect's literal symptom.
C6  REGRESSION: no non-reacting case changes behaviour. The escalation is reachable only when
    out.reactingResult has a value, which requires a case with a `reacting` block.
```

### 3.1 Why C3 is safe to assert

```text
cases/reacting_plug_flow_march/results/metadata.json
  species[0..2].status  Converged      thermal.status  Converged
  solver.status         Converged
```

All solves converged, so `anyLinearSolveFailed` is false for the only committed reacting case
and C3 cannot be violated by it.

### 3.2 Why the blast radius is small — measured, not assumed

```text
22 files call ProjectRunner::run                       ZERO mention "reacting"
cases/ driven by tests, by name                        20 cases, reacting_plug_flow_march NOT
                                                       among them
tests comparing CLI stdout verbatim                    NONE -- the CLI/GUI equivalence test
                                                       compares result FIELDS and bit patterns
```

**So no existing test can observe this change except through the new tests.** That is also a
finding in its own right: the reacting path has a committed case with committed results that
nothing exercises, so a regression in it would be caught by nothing. Recorded, not fixed here.

## 4. NEGATIVE CONTROL

```text
the control is the OLD rule, stated as a value: today
  exitCodeFor(out.status) == 0 for a reacting run with every solve failed,
because out.status is set at :501 from the flow and never revisited.
C2 asserts the new value is 4. The old value 0 is recorded here so the control is a NUMBER
rather than a story, and C1's `reactingSolveFailed == false` row is the other half: it proves
the escalation fires ONLY on failure and is not a blanket downgrade.
```

## 5. WHAT THIS GATE DOES NOT CLAIM

```text
NOT claimed  that a reacting solve failure is REPRODUCED. C1/C2 test the rule at its seam, not
             the integration. Making a reacting linear solve genuinely fail on demand is not a
             fixture task, and until it exists the end-to-end path from a real failure to a
             nonzero exit code is verified by construction, not by execution.
NOT claimed  that the reacting path is now tested. It is still driven by no test (3.2).
NOT claimed  that NumericalFailure is the only defensible mapping. DidNotConverge is arguable;
             NumericalFailure is chosen because it is what the flow's own linear-solve
             failures already map to.
```
