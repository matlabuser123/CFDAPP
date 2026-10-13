# P13 Final Qualification — G6, Diagnostics / Logging: final report

**Result: all criteria PASS. G6 → PASS.**

Date 2026-09-24 (UTC) / 2026-09-25 local. Tree `3bd32f918656-modified`.

**No production, test or harness source was changed.** `SOURCE_FINGERPRINT_V2`
is byte-identical before and after — no delta to account for. Everything added
here is evidence under `results/`.

---

## 1. Scorecard

```text
stdout machine-readable            PASS       C2 0/7, C2b 0/35
logging on stderr                  PASS       C1, no log line on stdout at any level
trace level                        PASS
debug level                        PASS
info level                         PASS
warning level                      PASS
error level                        PASS
invalid level rejection            PASS       V2, exit 1, names all five
default behaviour                  PASS       V4 0/7 stderr differing; default = warning
no hot-loop logging                PASS       P3, 5 and 50 iterations -> 3 lines each
log/result consistency             PASS       stdout, stderr and metadata.json agree
negative controls                  DETECTED   6 of 6 active criteria (see §6)
per-control restoration            VERIFIED   4 of 4 mutated files, twice over
filesystem hygiene                 PASS       no strays; committed evidence untouched
SOURCE_FINGERPRINT_V2              MATCH      before == after, no delta

G6 -> PASS
```

## 2. A correction made before measuring anything

Every frozen G6 instrument hardcodes `build/final/apps/cli/cfdapp`. That binary
dated **2026-09-25 05:26**, which falls inside the window when another session's
G3 controls had `src/io/CaseReader.cpp` and `src/io/case/JsonUtil.cpp` patched.
Measuring the gate against it would have qualified a possibly
control-contaminated executable.

Rebuilt from the restored tree first (`logs/G6_measure.log`), so every number
below comes from a binary built from the qualified source.

## 3. Channel separation — C1, C2, C2b

```text
C2   stdout vs the frozen baseline corpus, default level      0 / 7 differ
V4   stderr vs the frozen baseline corpus, default level      0 / 7 differ
C2b  stdout vs the default baseline at EVERY level            0 / 35 differ
       error 0   warning 0   info 0   debug 0   trace 0
C1   a log line reaching stdout at any of the five levels     none
```

The architecture holds: stdout carries the report, stderr carries diagnostics,
and raising verbosity to `trace` does not move one byte of stdout.

## 4. Verbosity — V1, V2, V3

```text
V1   all five levels accepted; the run still completes
       error   exit=3  stdout=803 B  stderr=0 B
       warning exit=3  stdout=803 B  stderr=0 B
       info    exit=3  stdout=803 B  stderr=178 B
       debug   exit=3  stdout=803 B  stderr=314 B
       trace   exit=3  stdout=803 B  stderr=314 B

V2   cfdapp --log-level shout
       exit 1
       Error: --log-level must be one of trace, debug, info, warning, error, got "shout"
     Rejected, naming all five valid choices. Not silently defaulted.

V3   stderr lines:  error=0  warning=0  info=1  debug=3      strictly increasing
```

Note `error` and `warning` both produce 0 stderr bytes on a successful run. That
is amendment A1's deliberate design, not a gap: `ProjectRunner` returns every
failure to its caller, which prints it unconditionally, so the log level controls
the diagnostic *trail* and never the outcome. An operator cannot hide a failure
by choosing a quiet level.

## 5. The log points, default behaviour, and the hot loop

All eight observable points fire at `--log-level debug`, `FAILURES: 0`:

```text
L1a  loading case               L4a  checkpoint written at iteration 2
L1b  case rejected              L4b  resumed from iteration 6
L2   linear solver backend      L7   export failed
L3   solver start               L8   solve finished (status, iterations, residuals,
                                     mass imbalance, runtime, backend)
```

L5 and L6 (linear-solve failure, non-finite state) are not reachable from any
shipped case and are exercised by control N4 instead — the frozen gate says so,
and that limit is carried forward unchanged rather than papered over.

**Default behaviour (V4):** default level is `warning`; stderr is byte-identical
to the baseline for all 7 cases; trace/debug/info are not emitted by default.

**P3 — no hot-loop logging**, the criterion that matters most for performance:

```text
static    Logger call sites in SIMPLE.cpp          1
          inside the outer-iteration loop          0
dynamic   max_iterations=5,  --log-level trace     3 stderr lines
          max_iterations=50, --log-level trace     3 stderr lines
```

Equal, not merely bounded. A tenfold increase in outer iterations produces
identical diagnostic output, so diagnostics sit on lifecycle transitions rather
than inside the solve.

## 6. Step 7 — the log agrees with the production result

One run, three channels, same numbers:

```text
stdout (the client's report)   Converged: no (max iterations reached)
                               Iterations: 9
                               U residual: 0.00162955
stderr (the diagnostic trail)  [INFO] solve finished: MaxIterations, 9 outer
                               iterations, residuals u=0.001630 ... discretization CPU
metadata.json (exported state) iterations=9 status=MaxIterations backend=CPU
                               gpu_disc=False
```

The log reports state the solver already computed; it does not recompute or
approximate it. The only difference is display precision — `0.00162955` against
`0.001630` — which is two renderings of one value, not two answers.

## 7. Non-vacuity controls

All six frozen controls run unmodified, serially, on an exclusive tree.

```text
N1-C1   route Info back to stdout
        planted defect  YES   criterion fails  YES (3 log lines on stdout)
        DETECTION  DETECTED        RESTORE  VERIFIED by sha256

N1-C2   the same plant, measured against C2
        planted defect  YES   criterion fails  NO  (0 stdout files differing)
        DETECTION  UNDETECTED -- EXPECTED, superseded by N1b. See below.

N1b     route Info/Debug/Trace back to stdout, measured against C2b
        planted defect  YES   criterion fails  YES
        DETECTION  DETECTED        RESTORE  VERIFIED by sha256
          intact  0/35    planted  18/35    restored  0/35
          (info 4, debug 7, trace 7)

N2-V4   drop the CLI's explicit default level
        planted defect  YES   criterion fails  YES (4 stderr files differing)
        DETECTION  DETECTED        RESTORE  VERIFIED by sha256

N3      one byte appended to one of the 7 baseline files
        the comparison instrument reports  1 differing
        DETECTION  DETECTED -- the instrument itself can fail

N4      delete the L3 emission
        planted defect  YES   criterion fails  YES (L3 absent from the debug run)
        DETECTION  DETECTED        RESTORE  VERIFIED by sha256

N5      make the CLI wrapper echo stderr onto the matched stream
        CFDAppCliLogLinesNeverReachStdout  (Failed)
        DETECTION  DETECTED        RESTORE  VERIFIED by sha256, test passes again
```

### N1-C2 UNDETECTED is the documented outcome of an adopted amendment

Per the closure rules an UNDETECTED control stops the gate until classified.
Classification: **the control is wrong for that criterion, and this was already
diagnosed and formally amended at P13-005, before this qualification.**

`results/p13/diagnostics/acceptance_gate_A2.md` records it: amendment A1 made the
CLI's default level `Warning`, so at the default level nothing below Warning is
emitted at all, and routing Info to stdout cannot put anything on stdout in a
default run. N1 reaches C1 (measured at every level) and cannot reach C2
(measured at the default level only). C2 is therefore a **preservation**
criterion — the frozen gate's own dry-run record already classified it that way —
and the inconsistency was in N1's "must break" column.

A2 added **C2b** as the detection counterpart and **N1b** as its control. This
run reproduces A2's recorded table exactly:

```text
                          A2, recorded 2026-09-21      this run
--log-level info                            4               4
--log-level debug                           7               7
--log-level trace                           7               7
                  total                    18              18
```

Same instrument, same numbers, two weeks apart. The amendment is sound and still
holds. **No criterion was weakened here and none was reinterpreted after seeing
the result** — the reinterpretation happened at P13-005 and is what this gate
verified.

## 8. Restoration and hygiene

Restoration was verified **twice, independently**: each frozen script checks its
own restore by sha256, and this gate's wrapper recorded its own pre-control
hashes and re-checked all four watched files after every script. A script that
restored and verified with the same broken assumption would report success either
way; two independent records will not.

```text
src/core/Logger.cpp                  OK
apps/cli/main.cpp                    OK
src/app/ProjectRunner.cpp            OK
tests/cmake/RunCliStdoutOnly.cmake   OK
```

```text
repo-root strays                                 none
untracked engineering file count                 122 (unchanged)
control markers in src/ apps/ tests/             none
committed P13-005 evidence, 10 files             all UNCHANGED
the 14 baseline corpus files                     all UNCHANGED
```

Every control worked in `/tmp` directories it created itself. After restore, the
tree re-measures at C2 0/7, V4 0/7, C2b 0/35.

## 9. Source fingerprint

```text
V2 before G6   8652dda040ac451e0fab8491fa90f495976513a91a30a45ef71cbf65eb0f292c
V2 after  G6   8652dda040ac451e0fab8491fa90f495976513a91a30a45ef71cbf65eb0f292c
```

Equal. Components unchanged too: tracked diff `2f71bb14…`, untracked engineering
`750012b4…`, solver-output sub-hash `3bf622aa…`, 112 tracked and 122 untracked
files. G6 required no source or harness edit at all.

## 10. What was deliberately not run here

`results/p13/diagnostics/tools/final.sh` bundles the three-configuration full
regression (Release+CUDA, Debug+GUI, CPU-only) with its format sweep. That is
**G10's** gate and takes hours; running it here would duplicate it and invite the
contention failure that invalidated G3's first invocation. Its diagnostics-specific
sections — P3 and P2 — are reproduced in `tools/G6_measure.sh` and both pass, with
P2 at 102/102.

## 11. Limits of this gate, stated

* L5 and L6 are not reachable from any shipped case; they are covered by control
  N4's mechanism rather than by a case. The frozen gate's own limitation, carried
  forward.
* `--log-level error` and `--log-level warning` emit nothing from `ProjectRunner`
  on a successful run. Deliberate (A1): the outcome is always returned and
  printed by the client, so the level cannot hide a failure.
* No human read the terminal output. This is a byte-comparison gate against a
  frozen corpus, not a judgement that the messages are well worded.
