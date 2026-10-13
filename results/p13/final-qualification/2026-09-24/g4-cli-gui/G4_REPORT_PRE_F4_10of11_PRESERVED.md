# P13 Final Qualification — G4, CLI / GUI consistency: final report

**HISTORICAL — the 10-of-11 result as recorded BEFORE the F4 repair was authorized.**
Preserved per the F4 authorization: the original N2a UNDETECTED outcome is not hidden.
Superseded by G4_REPORT.md. Only this header was changed.

Original result line, as recorded: "Result: 10 of 11 criteria PASS. G4 is NOT
closed — one non-vacuity control is genuinely UNDETECTED (finding F4)."

Date 2026-09-24 (UTC) / 2026-09-25 local. Tree `3bd32f918656-modified`.
Historical partial: `STATUS_PARTIAL_7of11_PRESERVED.md`.
Fingerprint amendment: `AMENDMENT_G4_fingerprint.md`.

---

## 1. Scorecard

```text
Common production execution path    PASS    A1/A2/A3/P2, structural
Field equivalence                   PASS    E1, E2, E3 bitwise
Iteration-count equivalence         PASS    E4
Residual-history equivalence        PASS    E5 bitwise
Mass-balance equivalence            PASS    E6 bitwise
Resolved backend equivalence        PASS    E7
Export equivalence                  PASS    E8 byte-identical
CLI checkpoint -> GUI resume        PASS    P4, pre-existing row
GUI checkpoint -> CLI resume        PASS    P4, NEW row (F1), measured directly
Negative controls                   MIXED   5 DETECTED, 1 UNDETECTED (F4)
SOURCE_FINGERPRINT_V2               MATCH   modulo the named authorized F1/F2 edit

Per-control restoration             VERIFIED  (6 of 6, by SHA-256)
Unexpected RC=99 restore failures   0

G4 -> NOT CLOSED
```

The harness now registers **9** CLI/GUI tests (was 7) plus **7** CLI-flag ctests.
All 16 pass at baseline: `logs/G4_after_controls.log`.

## 2. The exclusive-tree requirement was met

The other session's G3 controls were allowed to finish. Verified before any
mutating control ran:

```text
control processes running                     0
load average                                  0.00
apps/gui/tests/test_cli_gui_equivalence.cpp   bb1a3cfa...  at baseline
apps/gui/SimulationController.cpp             3e28d817...  at baseline
apps/cli/main.cpp                             708b0ca1...  at baseline
src/io/case/JsonUtil.cpp                      6ee654f7...  matches the foreign control's restore line
src/io/CaseReader.cpp                         c5d6d9fc...  matches the foreign control's restore line
```

Every mutating control then ran **serially**, one patch/restore cycle at a time.
At no point did two patch/restore harnesses operate concurrently.

## 3. F1 — GUI checkpoint → CLI resume, now measured

`CliGuiCheckpointTest.ACheckpointWrittenByTheGuiResumesInTheCli`.

The direction is measured, not inferred from P3's byte-identity:

```text
GUI  SimulationController::setCheckpoint -> run()   writes gui_written.ckpt
CLI  ProjectRunner::run(case, {resumeFrom = gui_written.ckpt})
     resumedFromIteration        8      (the GUI's file was genuinely consumed)
     resumedFromContinuation     true
     iterations                  20
```

and the resumed numerical state is then compared **against the CLI's own
checkpoint resumed the same way**, through the same frozen bitwise contract the
E rows use (`expectSimpleResultsAgree`: velocity x/y/z, pressure, mass flux,
iterations, resolved backend, face-flux scheme, all five residual histories,
global mass imbalance, final continuity residual). No new tolerance exists — the
comparison is bit-for-bit.

The byte-identity of the two checkpoints is asserted too, but only so a failure
reads as "the resume diverged" rather than "the inputs differed". It is not the
evidence for the direction.

## 4. F2 — the E-thermal coverage row, now implemented

`CliGuiEquivalenceTest.AThermalCaseAgreesBitwise`, using `cases/heated_cavity`
(20×20, left wall 310 K, right wall 290 K, bottom adiabatic). No `tests/data`
fixture carries a valid thermal block — the only one mentioning thermal is
`invalid_3d_thermal`, which exists to be rejected — so a production case is used
rather than a new case file being invented.

Non-vacuity is asserted inside the row, because `ProjectRunner` runs the thermal
solve only when the SIMPLE result is fully finite, so a case can carry a thermal
block and still never solve one:

```text
thermalResult.has_value()        both arms          required
thermalResult->iterations > 0                       required
temperature field is NOT uniform                    required
```

and the thermal result is then compared bitwise: status, iterations, linear
iterations, final residual, max temperature change, the whole temperature field,
and both thermal histories. The existing three E rows are unaffected — they pass
`ThermalExpectation::NotRequired`, the default.

Control **N5** proves the row is not vacuous: pointed at `valid_cavity`, which has
no thermal block, it FAILS.

## 5. F3 — the missing controls N2 and N4, now implemented

`tools/G4_controls_n2_n4.sh`. Frozen definitions used as written.

```text
N2a  the CLI arm is replaced by the GUI arm (frozen definition, literal)
     PLANTED DEFECT     yes
     CRITERION          PASSED -- the harness did NOT refuse
     DETECTION          UNDETECTED            <-- finding F4, section 6
     RESTORE            VERIFIED by SHA-256
     RETURN CODE        0 -- NOT expected

N2b  the GUI produces no results (the authorization's added requirement)
     PLANTED DEFECT     yes                   snapshot_ = VisualizationSnapshot{}
     CRITERION          FAILED as expected (4 tests)
     DETECTION          DETECTED
     RESTORE            VERIFIED by SHA-256
     RETURN CODE        nonzero (8), expected

N4   the CLI binary loses its --checkpoint-every validation
     PLANTED DEFECT     yes
     CRITERION          FAILED as expected (2 tests)
     DETECTION          DETECTED
     RESTORE            VERIFIED by SHA-256
     RETURN CODE        nonzero (8), expected

N5   the new E-thermal row is pointed at a case with no thermal block
     PLANTED DEFECT     yes
     CRITERION          FAILED as expected (1 test)
     DETECTION          DETECTED -- MustHaveRun fired
     RESTORE            VERIFIED by SHA-256
     RETURN CODE        nonzero (8), expected
```

And the two frozen controls that already existed, re-run unmodified
(`results/p13/cli-gui/tools/controls.sh`; the 2026-09-21 logs were copied into
this tree first, as `logs/PRESERVED_2026-09-21_*`):

```text
N1   one byte differs in the GUI's export
     DETECTION          DETECTED -- 4 equivalence tests fail   (2026-09-21: 3)
     RESTORE            VERIFIED by SHA-256
N3   the GUI stops forwarding its checkpoint options
     DETECTION          DETECTED -- 3 checkpoint-parity tests fail   (2026-09-21: 2)
     RESTORE            VERIFIED by SHA-256
```

Both detect *more* than in 2026-09-21 because the new F1 and F2 rows depend on
the same criteria — the rows are wired into the contract, not sitting beside it.

### N4's frozen definition was not implementable literally

"Point the CLI-flag ctests at a binary without the flags." Those ctests resolve
their binary through `$<TARGET_FILE:cfdapp>` at configure time and cannot be
repointed without reconfiguring the project. The defect planted instead removes
the `--checkpoint-every` value validation so a bad interval becomes a silent 0 —
which is the exact failure mode V3's own comment names ("`--checkpoint-every ten`
meaning `never checkpoint`"). Recorded in the script and here rather than done
silently. **This is a deviation from the frozen wording; it is not a redefinition
of what N4 tests.**

## 6. Finding F4 — the harness cannot detect a self-comparison

**This is what stops G4 closing.** It is preserved, not redefined.

N2's frozen requirement is that comparing the GUI arm against itself must make
the harness "declare the comparison vacuous and fail, not pass". It does not.
With the CLI arm replaced by the GUI arm, all equivalence tests **pass**.

The harness's own header claims otherwise:

> NON-VACUITY. A harness that compared one arm with itself would pass for free.
> Every case asserts the two results came from *different clients* before
> comparing a single value ... Control N2 exists to show that assertion can fail.

The assertions actually present — `controller.hasResults()` and the `completed()`
signal — prove the GUI arm **ran**. They do not prove the second arm is a
**different client**. N2b shows the first property is genuinely guarded; N2a shows
the second never was. The comment describes a protection the code does not have,
and the control that was supposed to reveal that was the one never written.

Nothing numerical is wrong: the 9 rows do compare a real GUI run against a real
CLI run today. The defect is that the harness would not notice if they stopped.

**Not fixed here.** The minimum fix is an assertion inside `expectClientsAgree`
that the two results came from different clients. That is a further source
change beyond the F1/F2 additions authorized, so it is reported rather than made.

## 7. Source integrity

Full detail in `AMENDMENT_G4_fingerprint.md`.

```text
SOURCE_FINGERPRINT_V2 initial  6639759722a622db99c03f27cfb5855a66a2b21a9f05ff4f8714924d7ae2024b
SOURCE_FINGERPRINT_V2 final    c6f01eeac4daed3cae0d34a13a7d9ab13d0362439d043150fce9dbae6f2d2156
```

They differ, under the named-authorized-change exception. Attribution is proven,
not asserted:

```text
V2 tracked component      2f71bb14...  ->  2f71bb14...   IDENTICAL
solver-output sub-hash    3bf622aa...  ->  3bf622aa...   IDENTICAL
untracked engineering     a59e51fe...  ->  3b6599a1...   changed
files modified since the baseline:
  apps/gui/tests/test_cli_gui_equivalence.cpp    <- exactly one; F1 + F2, authorized §3/§4
```

No production source changed. `SimulationController.cpp` and `main.cpp` were
touched only inside controls and restored to their exact baseline SHA-256s. No
numerics, solver algorithm, tolerance, checkpoint format or CLI/GUI behaviour was
modified.

A control side effect was found and cleaned: N4's planted defect made a rejection
ctest actually run, writing `x.ckpt` into the repository root. Preserved as
`logs/N4_SIDE_EFFECT_x.ckpt`, removed from the tree, V2 re-taken. See the
amendment §5 — it also records the pre-existing test-hygiene debt that those two
ctests name a repo-root-relative checkpoint path.

## 8. What G4 closure now requires

One decision: **F4.** Either

* authorize the harness fix — an assertion in `expectClientsAgree` that the two
  arms came from different clients — after which N2a is re-run and must be
  DETECTED; or
* accept F4 as a recorded limitation, in which case "Negative controls DETECTED"
  is true for N1, N2b, N3, N4 and N5 and false for N2a, and G4 closes with that
  stated rather than with an unqualified DETECTED.

Everything else in the closure table is satisfied and evidenced.
