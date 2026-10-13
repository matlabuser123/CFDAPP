# TD-047 — RESOLVED. A failed reacting solve now reaches both the exit code and the report, and the reacting path was driven end-to-end for the first time.

```text
STATUS:      **RESOLVED.** All six frozen criteria pass.
               C1 the status rule, exhaustive over 6 x 2         PASS  4/4 tests
               C2 exit code 0 -> 4 on a failed reacting solve    PASS
               C3 NO false positive: converged case still 0      PASS  CLI exit 0
               C4 the CLI reports the reacting block             PASS  10 lines, was 0
               C5 anyLinearSolveFailed has a reader              PASS  2 readers, was 0
               C6 regression across ProjectRunner's consumers   PASS  113/113
             **And the reacting path has now been driven end-to-end by something for the first
             time** -- 80 cells, 20 steps, species A/B/N2 and the temperature half all
             converged. Nothing had ever run it (section 3.1).
SCOPE:       src/app/ProjectRunner.cpp, include/cfd/app/ProjectRunner.hpp, apps/cli/main.cpp,
             tests/unit/app/test_project_runner.cpp. 125 insertions, 0 deletions, 4 files.
             Production LOGIC added: one pure function, one call site, one report block.
GATE:        GATE.md, frozen `fcb95e58700c9cb9ac80aed9d61ddf1a7641ef7db10961dfe315eabc813771a5`
             BEFORE any edit (logs/00_freeze.log).
EVIDENCE:    logs/00_freeze.log · logs/01_verification.log · logs/02_cli_reacting.log
COMMIT:      carried as evidence.
```

---

## 1. THE DEFECT WAS WORSE THAN REGISTERED, AND THE GATE CORRECTED IT FIRST

TD-047 was registered as *"Only the per-species status reaches stdout."* **That was wrong.**

```text
ProjectRunner.hpp:326   ProjectRunResult::speciesResults    <- what printReport iterates
ProjectRunner.hpp:151   ReactingRunResult::speciesResults   <- what the reacting path fills
ProjectRunner.cpp:696   out.speciesResults.push_back(...)   NON-reacting path
ProjectRunner.cpp:1000  run.speciesResults.push_back(...)   REACTING path
apps/cli/main.cpp       grep "reacting" -> ZERO hits
```

Different fields on different structs. So a reacting run printed **no species lines at all**, and
its failures reached neither stdout nor the exit code. The correction was made in GATE.md §1
before any code was touched, and is carried forward into the register.

## 2. THE FIX

### 2.1 The rule is derived, not invented

`statusFor(SIMPLEStatus)` at `:164`-`:185` already maps the FLOW's own linear-solve failures —
`MomentumFailure`, `PressureCorrectionFailure` — to `NumericalFailure`, exit 4. A reacting
linear-solve failure is the same kind of event, so it maps the same way. `Cancelled` is preserved
because a cancelled run's reacting failure follows from the cancellation rather than standing on
its own.

### 2.2 Why a pure function

`statusWithReactingOutcome(flowStatus, reactingSolveFailed)` is pure and header-declared, so the
rule is testable over its whole `6 x 2` input space **without engineering a reacting solve that
genuinely fails** — which is a research task, not a fixture. Its call site reads
`out.reactingResult->anyLinearSolveFailed`, which also discharges the "zero readers" half.

```text
production logic added, comments excluded:
  a 7-line pure function
  one call site:  out.status = statusWithReactingOutcome(out.status, ...->anyLinearSolveFailed);
  a 16-line CLI report block
```

## 3. THE MEASUREMENTS

```text
C1  ASucceedingReactingMarchLeavesEveryFlowStatusUntouched   PASS  (the `false` row, all six)
    AFailedReactingSolveEscalatesToNumericalFailure          PASS  (the `true` row, five)
    CancellationSurvivesAFailedReactingSolve                 PASS  (the one exception)
C2  TheExitCodeChangesFromZeroToFour                         PASS  0 -> 4, and 0 stays 0
C5  anyLinearSolveFailed readers: ProjectRunner.cpp:1059, apps/cli/main.cpp:237   (was 0)
C6  CFDAppLayerTests                       36 tests, 36 PASSED, 6492 ms
    full tree build                        rc=0
    every ProjectRunner::run consumer      113 tests, 113 PASSED, 0 failed, 5.44 s
      (-R 'ProjectRunner|AppLayer|Cli|CLI|ReactingCase|Visualization|CaseReader|CaseWriter|
        Checkpoint' -- 61 unit, 41 integration, 9 validation, 2 numerical)
```

### 3.1 C3 and C4, from the first end-to-end reacting run there has ever been

```text
Reacting species A converged: yes          Reacting species A iterations: 5
Reacting species B converged: yes          Reacting species B iterations: 5
Reacting species N2 converged: yes         Reacting species N2 iterations: 2
Reacting temperature converged: yes        Reacting temperature iterations: 6
Reacting any linear solve failed: no       Reacting steps taken: 20
CLI exit code = 0
```

**C4**: 10 `Reacting ` lines where there were none. **C3**: exit `0` on a case whose solves all
converge, so the escalation cannot turn a good run bad.

The run was done on a **copy outside the repository**, and `git status --porcelain cases/` came
back empty afterwards, so the tracked case outputs were not rewritten. (`§7` warns that runs
rewrite tracked outputs with timing noise; this avoids the problem rather than cleaning up after
it.)

**This is also the first time anything has driven the reacting path end-to-end.** Measured in
GATE.md §3.2: 22 files call `ProjectRunner::run` and **none** mentions `reacting`; the 20 cases
tests drive by name do **not** include `reacting_plug_flow_march`.

## 4. THE NEGATIVE CONTROL

The control is the old behaviour stated as a number, not a story:

```text
before: exitCodeFor(out.status) == 0 for a reacting run with every solve failed, because
        out.status was set at :501 from the flow and never revisited.
after:  == 4   (C2 asserts both halves)
and     C1's `false` row proves the escalation fires ONLY on failure -- without it, a blanket
        downgrade would pass every failure row and still be wrong.
```

## 5. VERIFICATION LADDER

```text
§7 clang-format   all four changed files pass (apps/cli/main.cpp needed reformatting and was
                  reformatted before the build)
build             CFDAppLayerTests + cfdapp, rc=0
focused tests     4 new, 4 passed
target regression CFDAppLayerTests 36/36
end-to-end        the CLI on the reacting case, exit 0, report verified
```

## 6. WHAT IS NOT ESTABLISHED

```text
NOT established  that a real reacting failure produces exit 4 END TO END. C1/C2 verify the rule
                 at its seam and C3/C4 verify the success path through the CLI, but no run has
                 ever had a reacting solve actually fail. The path from a genuine failure to a
                 nonzero exit is correct by construction, not by execution. Making a reacting
                 linear solve fail on demand is the missing fixture.
NOT established  that NumericalFailure is the only defensible mapping. DidNotConverge is
                 arguable; NumericalFailure is chosen because it is what the flow's own
                 linear-solve failures already produce. Recorded as a choice, not a derivation
                 from physics.
NOT run          the full §8 regression -- the complete Release suite (2840 tests),
                 Debug+GUI, and the sanitizer configurations. What WAS run: a full tree build
                 (rc=0) and 113 tests covering every consumer of `ProjectRunner::run` plus the
                 CLI, IO, checkpoint and visualization paths, all passing. The escalation is
                 additionally unreachable unless `out.reactingResult` has a value, which needs a
                 case with a `reacting` block and no other committed case has one -- so the
                 blast radius is bounded by both a suite run and the guard, but §11.4 is still
                 not discharged.
NOT fixed        that the reacting path is driven by no automated test. §3.1 measured it; this
                 item did not add one. The CLI run here was manual verification, not a test, so
                 a regression in the reacting march would still be caught by nothing. **That is
                 worth its own item.**
NOT addressed    TD-003's §12 stop, TD-006's dependencies, or anything in G8.
```
