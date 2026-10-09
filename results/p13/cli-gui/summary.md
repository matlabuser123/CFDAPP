# P13-003 — CLI / GUI consistency: summary

**One execution path already existed. What did not exist was equal access to it, or any real
measurement that the two clients agree.**

```text
gate        results/p13/cli-gui/acceptance_gate.md   (frozen before any change)
dry-run     logs/00_dryrun.log                       (every baseline measured, not inferred)
baseline    the tree that closed P13-002
environment WSL2 Ubuntu 22.04, gcc 11.4, CUDA 12.9, RTX 5000 Ada (sm_89)
```

---

## 1. The premise held: there is one solver path

```text
apps/cli/main.cpp:251          ProjectRunner::run(caseDirectory, options)
apps/gui/…Controller.cpp:234   session_.run(options)
src/app/CaseSession.cpp:197      -> ProjectRunner::run(*dir, options)

solver constructions in apps/cli + apps/gui:  0
```

`ProjectRunner.hpp`'s "ONE SOLVER BACKEND" rule has held since P5, and this item did not need to
merge anything. It is about what sits on top.

## 2. Three findings, all closed

### 2.1 The clients exposed disjoint subsets of the same backend

`ProjectRunOptions` has five fields; neither client used more than three:

| option | CLI before | GUI before | after |
| --- | --- | --- | --- |
| `progressCallback` | no | yes | unchanged |
| `cancellationCheck` | no (deliberate, §4) | yes | unchanged |
| `checkpointPath` | yes | **no** | **both** |
| `checkpointInterval` | yes | **no** | **both** |
| `resumeFrom` | yes | **no** | **both** |

The GUI could not checkpoint or resume, and the measured baseline was stark: **0** references to
checkpoint or resume anywhere in `apps/gui`, against 34 in the CLI.

The fix needed **no new plumbing**. `CaseSession::run` already forwarded every option untouched
(it wraps only `cancellationCheck`, to OR in its own Stop flag). Only the surface was missing:
`setCheckpoint`, `setResumeFrom`, `clearCheckpoint`, plus three reported properties. That is why
criterion P2 — no second implementation — is true by construction rather than by inspection.

Two things deliberately *not* done while adding it:

* **`CaseSession` was not given a `setLastError`.** The controller needed somewhere to put an
  argument error that never reaches the session; widening a production API for a GUI convenience is
  how two clients drift apart. The controller keeps its own slot and `lastError()` prefers it.
* **The GUI does not check whether a checkpoint file exists.** `ProjectRunner` already refuses a
  missing or invalid one, naming the file. A second place deciding "is this checkpoint usable" is
  precisely the divergence this item exists to remove.

### 2.2 The existing equivalence check measured almost nothing

`apps/gui/tests/test_case_editing.cpp` called itself the "CLI/GUI Round-Trip Gate" and asserted:

```cpp
EXPECT_EQ(cliEquivalentRun.status, ProjectRunStatus::Converged);
```

Two runs converging to **different answers** would pass it. Replaced with the brief's actual list,
compared bitwise: final velocity, pressure and face mass flux; status and iteration count; the
u/v/w/pressure/continuity residual histories; global mass imbalance; the resolved backend and
face-flux scheme; and the three exported files.

Coverage: 2D, 3D, and a case **edited in the GUI** then run both ways — the one row where the GUI
did something the CLI cannot, and both must still agree.

### 2.3 CI never touched the CLI binary's argument handling

Deferred here from P13-001 with that in mind. Measured baseline: **0** ctest entries. Now six —
`--help` documents the flags, and five bad invocations must exit non-zero, including
`--checkpoint-every ten`, which must not silently mean "never checkpoint".

## 3. The vacuous criterion I caught in my own harness

E8 compared the two clients' exported files **by path**. `ResultExporter` always writes to
`<case>/results`, and the CLI arm runs second — so it read the same file twice and would have
passed no matter what either client produced.

It was green when I found it. It was found by asking "could this pass for the wrong reason?", not
by a failing test.

Fixed by snapshotting the GUI's exported bytes **before** the CLI arm overwrites them, and widened
from `metadata.json` alone to all three files. Control **N1** is what proves the fix is real: with
one byte added to the GUI's snapshot, **3 equivalence tests now fail**. In the vacuous version that
perturbation would have changed nothing.

A second, smaller instance in the same stretch: the first draft of N1 declared an unused perturbed
copy instead of perturbing the assertion, so it would have reported UNDETECTED for the wrong
reason. Corrected before it ran.

## 4. The asymmetry that stays, and why it is a criterion

**The CLI cannot cancel a running solve, and this item did not add that.** The GUI has a Stop
button because it runs the solve on a worker thread with an event loop; the CLI is a single thread
that calls the solver and prints the result. Adding cancellation means a signal handler and a
second thread — a new feature, not a consistency fix. `cfdapp` is stopped the way any command-line
program is.

Criterion P6 makes this a recorded decision rather than an apparent oversight, and
`docs/user_guide/cli.md` now says so where a user will find it.

## 5. Results

```text
A1  solver constructions in the clients            0            PRESERVATION
A2  entry points, both -> ProjectRunner             2            PRESERVATION
A3  options fields CaseSession rewrites             1 documented PRESERVATION

E1..E8  CLI vs GUI, bitwise                         3 tests      PASS
        2D, 3D, and a GUI-edited case
P1..P5  GUI checkpoint surface, cross-client        4 tests      PASS
        a GUI checkpoint and a CLI checkpoint of the same case are
        BYTE-IDENTICAL; each client resumes the other's
P6      CLI cancellation asymmetry documented                    PASS
V3      CLI flag coverage in CI                     6 tests      PASS  (baseline 0)

N1  one byte differs in the GUI's export       -> 3 tests fail   DETECTED
N3  the GUI stops forwarding its options       -> 2 tests fail   DETECTED

G4  clang-format, 603 files                         0 violations
G1  Release + CUDA CTest                            2036 / 2036, 0 failed
G2  Debug + GUI CTest                               2030 / 2030, 0 failed
G5  P13-001 and P13-002 tests inside that run       63 tests, 0 non-passing
      SIMPLECheckpoint 14  SIMPLEContinuation 3  CheckpointProductionCase 6
      CaseFingerprint 2    CaseDiagnostic 7      RestartIO 14  RestartSnapshot 17
```

## 6. Vacuity, checked before the fact this time

P13-002's amendment A3 exists because a criterion whose baseline already equalled its target got
reported as a pass. Every criterion here was checked against that in the dry-run, before any code
changed:

* **P1 (0 → >0)** and **V3 (0 → 5+)** — non-vacuous, baselines measured.
* **A1, A3, the private-parser count** — already at target, which is correct *only* because they
  are PRESERVATION criteria. They are labelled as such and are **not** counted as achievements of
  this item.
* **E1–E8** — no baseline, because no harness existed. Recorded as "not measurable" rather than
  written down as 0 and later quoted as an improvement to 0.

## 7. What this item did not do

* **No GUI redesign**, and no QML surface for checkpointing. The controller exposes it; wiring a
  button is a UI decision, not a consistency fix, and the brief says not to refactor unrelated GUI
  code.
* **No claim of visual GUI acceptance.** `CLAUDE.md` §4: automated GUI tests do not replace human
  acceptance. Everything here is verified at the controller/service layer, and that limit is
  stated rather than glossed. No one clicked a window.
* **No CLI cancellation** — see §4.
