# P13 Final Qualification — G4, CLI / GUI consistency: final report

**Result: all 11 criteria PASS. G4 → PASS.**

Date 2026-09-24 (UTC) / 2026-09-25 local. Tree `3bd32f918656-modified`.

Evidence lineage, none of it overwritten:

```text
STATUS_PARTIAL_7of11_PRESERVED.md          the 7/11 partial, before F1-F3
G4_REPORT_PRE_F4_10of11_PRESERVED.md       the 10/11 result, N2a UNDETECTED
AMENDMENT_G4_fingerprint.md                V1 withdrawn, V2 adopted
logs/PRESERVED_2026-09-21_*                the original P13-003 control logs
logs/PRESERVED_preF4_*                     the UNDETECTED N2a run, raw
```

---

## 1. Scorecard

```text
Common production execution path    PASS
Field equivalence                   PASS
Iteration-count equivalence         PASS
Residual-history equivalence        PASS
Mass-balance equivalence            PASS
Resolved backend equivalence        PASS
Export equivalence                  PASS
CLI checkpoint -> GUI resume        PASS
GUI checkpoint -> CLI resume        PASS
Negative controls                   DETECTED   6 of 6
Per-control restoration             VERIFIED   6 of 6, by SHA-256
Unexpected RC=99 restore failures   0
SOURCE_FINGERPRINT_V2               MATCH / authorized-delta accounted

G4 -> PASS
```

Regression at closeout: **16 of 16 pass** — 9 CLI/GUI equivalence rows
(was 7) and 7 CLI-flag ctests. `logs/G4_final_regression.log`.

## 2. Structural half

Grep expressions copied verbatim from the frozen `dryrun.sh`; only the output
path changed, so the committed 2026-09-21 baseline is not overwritten.

```text
A1  solver constructions in apps/cli + apps/gui      0    (2026-09-21: 0)    PASS
A2  solver entry points, both -> ProjectRunner        2    (2026-09-21: 2)    PASS
A3  ProjectRunOptions fields CaseSession rewrites     1    cancellationCheck  PASS
P1  GUI references to checkpoint/resume              60    (baseline 0)       PASS
P2  second checkpoint implementation in apps/gui      0                       PASS
V2  validateDraft refs to CaseReader/CaseBuilder     11                       PASS
      GUI calls into the private parsers              0                       PASS
V3  registered ctest entries for the CLI flags        7    (baseline 0)       PASS
```

Two probe corrections of my own are recorded in the preserved 7/11 status: P2
initially counted a comment line, and V3 was first measured with the baseline's
source-line grep rather than by counting registered tests.

## 3. F1 — GUI checkpoint → CLI resume, measured

`CliGuiCheckpointTest.ACheckpointWrittenByTheGuiResumesInTheCli`. The GUI
controller writes the checkpoint; `ProjectRunner::run` is handed that exact file:

```text
resumedFromIteration      8       the GUI's file was genuinely consumed
resumedFromContinuation   true
iterations                20
```

The resumed state is compared against the CLI's own checkpoint resumed the same
way, through the frozen bitwise contract (`expectSimpleResultsAgree`). Not
inferred from P3's byte-identity; that is asserted only so a failure is
attributable to the resume rather than to unequal inputs.

## 4. F2 — the E-thermal coverage row, implemented

`CliGuiEquivalenceTest.AThermalCaseAgreesBitwise` on `cases/heated_cavity`
(20×20, 310 K / 290 K walls, adiabatic floor). No `tests/data` fixture carries a
valid thermal block, so a production case is used rather than inventing one.

Non-vacuity asserted inside the row — `ProjectRunner` runs the thermal solve only
when the SIMPLE result is fully finite, so a case can carry a thermal block and
never solve one: thermal result present on both arms, iterations > 0, and the
temperature field not uniform. Thermal status, iterations, linear iterations,
final residual, max temperature change, the whole temperature field and both
thermal histories are then compared bitwise. Control N5 proves the row is not
vacuous.

## 5. F4 — the harness could not detect a self-comparison: found, then repaired

**This is the finding G4 turned on, and its full history is the evidence.**

**(a) The harness claimed a protection it did not have.** Its header said:

> NON-VACUITY. A harness that compared one arm with itself would pass for free.
> Every case asserts the two results came from *different clients* before
> comparing a single value ... Control N2 exists to show that assertion can fail.

**(b) N2a showed the protection did not exist.** Control N2a — the frozen
definition, never implemented until G4 — substitutes the GUI arm for the CLI arm.
Result, preserved raw in `logs/PRESERVED_preF4_*`:

```text
N2a   PLANTED DEFECT     yes
      CRITERION          PASSED -- the harness did NOT refuse
      DETECTION          UNDETECTED
      RETURN CODE        0 -- NOT expected
```

Every equivalence row passed while comparing one client with itself. The checks
present — `hasResults()` and the `completed()` signal — prove the GUI arm *ran*;
they say nothing about what the second arm is. N2b showed the first property was
genuinely guarded, which is what made the gap precise.

**(c) The harness was repaired** (authorized 2026-09-25, harness-only). A result
no longer travels as a bare `ProjectRunResult`. It travels as an `ArmResult`
tagged with a `ClientArm`, and the tag is written **only** by the factory that
drove that client — `runGuiArm` writes `Gui`, `runCliArm` writes `Cli`. The
default is `Unset`, so an arm no factory ever filled is detectable too.
`expectArmsAgree` asserts provenance **first and fatally**, before any value is
read:

```cpp
ASSERT_NE(gui.arm, cli.arm) << "both arms were produced by the " << armName(gui.arm)
    << " client: this is a self-comparison, it passes for free, and it proves nothing";
ASSERT_EQ(gui.arm, ClientArm::Gui);
ASSERT_EQ(cli.arm, ClientArm::Cli);
```

Provenance is never inferred from equal values, filenames, execution order or
comments. Substituting one arm for the other carries the wrong tag with it.

No production code was touched: no solver, numerics, tolerance, checkpoint
format, CLI behaviour, GUI behaviour or result structure. The change is confined
to `apps/gui/tests/test_cli_gui_equivalence.cpp`.

**(d) N2a now detects the planted self-comparison**, and for the right reason —
the control now requires the failure text to name the provenance assertion, so a
coincidental numeric or filesystem failure would be reported as AMBIGUOUS, not
accepted:

```text
N2a   PLANTED DEFECT     yes
      CRITERION          FAILED as expected (4 tests)
      DETECTION          DETECTED -- the provenance assertion fired by name
      RESTORE            VERIFIED by SHA-256
      RETURN CODE        nonzero (8), expected

test_cli_gui_equivalence.cpp:222: Failure
Expected: (static_cast<int>(gui.arm)) != (static_cast<int>(cli.arm)), actual: 2 vs 2
both arms were produced by the GUI client: this is a self-comparison, it
passes for free, and it proves nothing
```

The failure is at the provenance assertion, before a single number is compared.

## 6. The full control suite

All six, each run serially on an exclusive tree.

```text
N1   one byte differs in the GUI arm's export        DETECTED  4 tests   (2026-09-21: 3)
N2a  the CLI arm is replaced by the GUI arm          DETECTED  4 tests   via provenance
N2b  the GUI produces no results                     DETECTED  4 tests
N3   the GUI stops forwarding checkpoint options     DETECTED  3 tests   (2026-09-21: 2)
N4   the CLI loses its --checkpoint-every validation DETECTED  2 tests
N5   E-thermal pointed at a non-thermal case         DETECTED  1 test

Per-control restoration   VERIFIED by SHA-256, 6 of 6
RC=99 restore failures    0
Return codes              nonzero (8) on all six, as expected
```

N1 and N3 detect more than in 2026-09-21 because the new F1/F2 rows depend on the
same criteria — the rows are wired into the contract, not sitting beside it.

### Two deviations from frozen wording, recorded not silent

* **N4's frozen definition was not implementable literally.** "Point the CLI-flag
  ctests at a binary without the flags" — those ctests bind
  `$<TARGET_FILE:cfdapp>` at configure time and cannot be repointed without
  reconfiguring. The defect planted instead removes the `--checkpoint-every`
  value validation so a bad interval becomes a silent 0, which is the exact
  failure mode V3's own comment names. What N4 tests is unchanged.
* **N1's patch target moved.** The F4 repair replaced the E8 lines, so the frozen
  script's target no longer existed — a control whose patch does not apply
  reports nothing. A G4 copy (`tools/G4_controls_n1_n3.sh`) targets the new line;
  the committed `results/p13/cli-gui/tools/controls.sh` is unmodified.

## 7. Source integrity

```text
V2 initial          6639759722a622db99c03f27cfb5855a66a2b21a9f05ff4f8714924d7ae2024b
V2 after F1+F2      c6f01eeac4daed3cae0d34a13a7d9ab13d0362439d043150fce9dbae6f2d2156
V2 final, after F4  8652dda040ac451e0fab8491fa90f495976513a91a30a45ef71cbf65eb0f292c
```

MATCH with the authorized delta accounted. Attribution proven, not asserted:

```text
V2 tracked component    2f71bb14...  ->  2f71bb14...   IDENTICAL
solver-output sub-hash  3bf622aa...  ->  3bf622aa...   IDENTICAL
untracked engineering   a59e51fe...  ->  750012b4...   changed
untracked file count    122          ->  122           unchanged
tracked files changed   112          ->  112           unchanged

engineering files modified since the V2 baseline:
  apps/gui/tests/test_cli_gui_equivalence.cpp    <- exactly one
```

F1, F2 and F4 all live in that one file. Nothing else in the engineering tree
moved across the whole of G4. `SimulationController.cpp` and `main.cpp` were
patched only inside controls and restored to their exact baseline SHA-256s.

Control hygiene: N4's planted defect makes a rejection ctest actually run and
write `x.ckpt` into the repository root (`tests/CMakeLists.txt:103` names a
repo-root-relative path). It appeared on both N4 runs, was preserved as
`logs/N4_SIDE_EFFECT_x.ckpt` and removed, and the closeout now checks for it
explicitly. The underlying test-hygiene issue is pre-existing and recorded as
debt, not fixed here (Rule 8).

## 8. Limits of this gate, stated

* No human clicked a window. Everything is verified at the controller/service
  layer. `CLAUDE.md` §4: automated GUI tests do not replace human acceptance.
* CLI cancellation remains deliberately absent (frozen criterion P6).
* The harness compares a GUI-driven run against a direct `ProjectRunner` call,
  which is what `apps/cli/main.cpp` delegates to — not a subprocess launch of the
  `cfdapp` binary. The binary's own argument handling is covered separately by
  the 7 CLI-flag ctests.
