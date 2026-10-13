# P13 Final Qualification — G4, CLI / GUI consistency

Status: **HISTORICAL — the 7-of-11 partial result, preserved per authorization §7.**
Superseded by G4_REPORT.md. Only this status line was changed; the findings and
measurements below are exactly as recorded at the time.
Date: 2026-09-24 (UTC) / 2026-09-25 local.

---

## 1. What was verified, and with which instrument

### Structural half — `tools/G4_structural.sh`, no build, no source touched

The grep expressions are copied verbatim from the frozen
`results/p13/cli-gui/tools/dryrun.sh`. Only the output path changed, because
`dryrun.sh` writes into the committed P13-003 log and re-running it in place
would overwrite the 2026-09-21 baseline this gate is judged against.

```text
A1  solver constructions in apps/cli + apps/gui      0    (must be 0; 2026-09-21: 0)    PASS
A2  solver entry points, both -> ProjectRunner        2    (2026-09-21: 2)              PASS
      apps/cli/main.cpp:256                  ProjectRunner::run(caseDirectory, options)
      apps/gui/SimulationController.cpp:249  session_.run(options)
A3  ProjectRunOptions fields CaseSession rewrites     1    cancellationCheck, documented PASS
P1  GUI references to checkpoint/resume              60    (baseline 0, target >0)      PASS
P2  second checkpoint implementation in apps/gui      0    (must be 0)                  PASS
V2  validateDraft refs to CaseReader/CaseBuilder     11    (>0 = shared core)           PASS
      GUI calls into the private parsers              0    (must be 0)                  PASS
V3  registered ctest entries for the CLI flags        7    (baseline 0, target 5+)      PASS
```

The one execution path still holds: `CLI -> ProjectRunner::run` and
`GUI -> CaseSession::run -> ProjectRunner::run`, no client-side solver, no
second checkpoint implementation.

### Numerical half — `logs/G4_numerical.log`

```text
ctest --test-dir build/qual-debug -R CliGui --output-on-failure
100% tests passed, 0 tests failed out of 7        Total 3.20 sec
```

`build/qual-debug` is the Debug+GUI tree built from the frozen source at G1
(2026-09-24 12:08). **`ninja` was deliberately NOT run before this**: another
session was patching `src/io/CaseReader.cpp` and `src/io/case/JsonUtil.cpp` for
its own non-vacuity controls at the time, and a rebuild would have silently
captured a control-patched source state. The binaries therefore represent the
tree G1 qualified, which is what this gate is about.

Run under load average 18 with another session building. It passed in 3.2 s, so
the harness's 120 s `QSignalSpy` deadlines were never approached — a pass under
adverse load is still a pass. Only a FAILURE would have been ambiguous and
needed a quiet re-run.

Criteria covered by those 7 tests, all bitwise per the frozen gate:

```text
E1  velocity x/y/z per cell                       bitwise          PASS
E2  pressure                                      bitwise          PASS
E3  face mass flux                                bitwise          PASS
E4  status and iterations                         equal            PASS
E5  u/v/w/p/continuity histories                  bitwise          PASS
E6  globalMassImbalance, finalContinuityResidual  bitwise          PASS
E7  resolved backend + face-flux scheme           equal            PASS
E8  fields.csv, residuals.csv, metadata.json      byte-identical   PASS
P3  GUI checkpoint file == CLI checkpoint file    byte-identical   PASS
P4  CLI checkpoint -> GUI resume     resumedFromIteration 8, 20 iters  PASS
P5  GUI reports checkpointsWritten / resumedFromIteration as data     PASS
```

Coverage rows exercised: E-2d (`valid_cavity`), E-3d
(`valid_duct3d_cli_smoke`), E-gui-edited (viscosity changed in the GUI, saved,
then both arms), E-export (inside every row).

---

## 2. Findings — two coverage gaps in the frozen harness

Neither is a production regression. Both are places where the frozen gate's own
text asks for something the harness never implemented. Classification:
**harness defect**, per the failure-handling rules.

### F1 — `GUI checkpoint -> CLI resume` is never measured

`acceptance_gate.md` P4 reads: *"A checkpoint written by the CLI resumes in the
GUI, **and one written by the GUI resumes in the CLI**."* The harness implements
only the first direction — `ACheckpointCrossesBetweenTheClients` writes with
`ProjectRunner` and resumes in the controller, and its own comment says so:
`// P4 -- written by the CLI, resumed in the GUI.`

No test feeds a GUI-written checkpoint to `ProjectRunner::run` with
`resumeFrom`. `summary.md` §5 nevertheless states "each client resumes the
other's". That claim rests on P3: the two checkpoint files are byte-identical,
and the CLI can resume its own, so it must be able to resume the GUI's. The
argument is sound, but it is an inference, not a measurement — and `TODO.md`
lists the two directions as separate line items.

### F2 — the `E-thermal` coverage row was never implemented

`acceptance_gate.md` line 56 lists a fifth coverage row:

```text
E-thermal     a case with a thermal block (a second solver after SIMPLE)
```

`E-thermal` appears exactly once in the whole of `results/p13/cli-gui/` — in
that list. No test uses a thermal case; `summary.md` §5 records only "3 tests /
2D, 3D, and a GUI-edited case". The row is the one that would exercise a second
solver running after SIMPLE, which is precisely the shape P15 added.

### F3 — two of the four frozen N controls were never implemented or run

`acceptance_gate.md` group N specifies four controls, and closure requires
"every control in N fails as specified". Only two exist:

```text
N1  perturb one exported value in the GUI arm      -> E8       IMPLEMENTED, ran 2026-09-21
N3  remove the GUI checkpoint plumbing             -> P1/P3/P4 IMPLEMENTED, ran 2026-09-21
N2  compare the GUI arm against itself             -> harness must refuse   NEVER IMPLEMENTED
N4  point the CLI-flag ctests at a flagless binary -> V3                    NEVER IMPLEMENTED
```

`tools/controls.sh` names all four in its header comment but its body patches
only N1 and N3. `logs/N_controls.log` records only N1 and N3. `summary.md` §5
lists only N1 and N3. Searching the whole of `results/p13/cli-gui/`, the strings
`N2` and `N4` occur **only** in `acceptance_gate.md` itself.

This matters most for N2. The harness's own header says *"Control N2 exists to
show that assertion can fail"* — referring to the non-vacuity assertions
(`hasResults()`, the `completed` signal) that are supposed to prove the two arms
came from different clients. Those assertions are the guard against the exact
failure mode that P13-002 and GPU-PIPE-001 both hit, and nothing has ever
demonstrated that they can fail. An unexercised non-vacuity guard is the thing
this project's own rules single out as untrustworthy.

Closing F1 or F2 means adding a test, which changes source and moves the
fingerprint. **Not done without authorization.** F3 needs two control scripts,
which are evidence under `results/` and do not touch the fingerprint — but they
patch source at run time, so they still need a quiet tree.

---

## 3. Two corrections to my own probes, recorded not silent

Both are preserved: `logs/G4_structural_PROBE_v1_PRESERVED.log` is the first run.

* **P2 initially read 1 and would have been reported FAIL.** The single hit was
  a *comment* at `apps/gui/SimulationController.hpp:181`. `dryrun.sh`'s own A1
  grep excludes comments with `grep -v '//'`; my P2 probe did not. The probe was
  wrong, not the code. Filter added to match the frozen convention.
* **V3 initially read 4 against a target of 5+.** `dryrun.sh` measures V3 as
  `grep -c 'CFDAppCliCheckpoint' tests/CMakeLists.txt`. That is a sound
  *baseline* instrument — 0 occurrences did mean 0 tests — but the wrong
  instrument for the post-implementation value, because the registrations go
  through a CMake `function()`, so source lines are not registered tests.
  `ctest -N` counts 7. The target was not moved.

---

## 4. Why G4 cannot be closed right now

### 4.1 The N controls need an exclusive tree — another session holds it

`results/p13/cli-gui/tools/controls.sh` patches `apps/gui/...` and runs
`cmake --build build/debug -j20` on a restore trap. Throughout this work
another session (`9ccc20b4…`) was running the G3 case-validation controls:
`control_n1b.sh`, then `controls_n2_n3.sh`, each patching and restoring
`src/io/CaseReader.cpp` and `src/io/case/JsonUtil.cpp` on *its* restore trap,
and building `build/debug` and `build/final` at `-j20`.

Two concurrent patch/restore traps on one tree means neither script's
"restore VERIFIED" line proves the tree is clean, and if either is interrupted
the tree is left patched with no record of which control did it. Running G4's
controls into the same `build/debug` would put two `ninja -j20` instances in one
build directory.

Observed directly: at 19:34Z `git diff` showed **no** change in either control
file against HEAD — the N3 control ("the manifest site reverts to its old
message") was applied at that moment.

### 4.2 The fingerprint criterion cannot be satisfied as written

`TODO.md` is inside `tree_fingerprint.sh`'s SOURCE set, by its own design note:
*"src/, include/, cuda/, tests/, apps/, benchmarks/, scripts/, CMake files,
CLAUDE.md, TODO.md"*. But `TODO.md` is the qualification's execution control
file — every gate closeout ticks boxes in it. Measured:

```text
G1 baseline      2026-09-24T01:56Z   TODO.md 667 changed lines   86346f15...
G3 case-val      2026-09-24T18:59Z   TODO.md 667 changed lines   86346f15...
G4 (this tree)   2026-09-24T19:31Z   TODO.md 496 changed lines   differs
```

The fingerprint held exactly as long as `TODO.md` held and moved when G1–G3
were ticked. So "the fingerprint must return to `86346f15…`" is unsatisfiable
for any gate that closes: closing it is what breaks it.

Attribution of the move, by elimination:

```text
tracked source files differing from the frozen G3 record    TODO.md ONLY
      (all other 162 files: identical changed-line counts)
untracked source file LIST                                  identical
untracked solver outputs (cli_log_level_.../results, etc.)  mtimes predate the
      frozen fingerprint (2026-09-24 18:31Z vs 18:59Z), so unchanged
```

Residual uncertainty, stated rather than hidden: the 162-file comparison is on
changed-line counts, not content hashes, because no copy of the 18:59Z
`TODO.md` survives (`results/todo-archive/` holds only a 2026-09-17 version), so
the historical source-diff hash cannot be recomputed with `TODO.md` removed.

`tools/G4_fingerprint.sh` therefore records both values — the original
whole-tree one, for continuity with G1, and a `TODO.md`-excluded one, which is
the value that can actually be held constant across a closeout. Nothing is
weakened: the excluded file is neither compiled nor tested, and every source,
test, CMake and `CLAUDE.md` path stays inside the hash.

`fingerprint_G4_before.txt`, at 2026-09-24T19:34Z:

```text
SOURCE FINGERPRINT (tree_fingerprint.sh definition, incl TODO.md)
  46fc7f40cfb85c8b69212b5bfd8a79fed492c00c5cca151cd92f63f972636fe6
SOURCE FINGERPRINT (G4 definition, excl TODO.md)
  49bb50ba098a5acb0b990c98896d3c50a6fb975bc3647b9759fd689a6bcd7765
```

Both were taken while the other session's controls were mid-cycle, so **neither
is a valid G4 baseline.** Two probes three minutes apart disagreed
(`c03f7942…` vs `837fe2e5…` on the tracked-diff component) for exactly that
reason. A valid baseline requires a quiet tree.

---

## 5. G4 scorecard

```text
Common production execution path    PASS             A1/A2/A3/P2, structural
Field equivalence                   PASS             E1, E2, E3 bitwise
Iteration-count equivalence         PASS             E4
Residual-history equivalence        PASS             E5 bitwise
Mass-balance equivalence            PASS             E6 bitwise
Resolved backend equivalence        PASS             E7
Export equivalence                  PASS             E8 byte-identical
CLI checkpoint -> GUI resume        PASS             P4, one direction
GUI checkpoint -> CLI resume        NOT MEASURED     finding F1
Negative controls detected          NOT RUN          tree not exclusive (4.1);
                                                     and 2 of 4 never existed (F3)
Source fingerprint unchanged        CANNOT EVALUATE  criterion defect (4.2)

G4 -> NOT CLOSED
```

## 6. What closing G4 requires

1. A quiet tree — the other session's G3 controls finished, nothing building.
2. Re-take the G4 fingerprint baseline on that quiet tree.
3. Run `results/p13/cli-gui/tools/controls.sh` (N1, N3) and report
   criterion / detection / restoration separately.
4. A decision on F3: whether G4 writes the missing N2 and N4 controls, or
   records that the frozen gate has only ever had two of its four. Writing them
   is evidence-only work under `results/` and does not move the fingerprint.
5. A decision on the fingerprint criterion (4.2).
6. A decision on findings F1 and F2 — both need a new test, i.e. a source
   change, i.e. authorization.
