# P13-005 — Diagnostics / logging: summary

**The framework already existed and nothing used it. Wiring it up naively would have corrupted the
CLI's machine-readable report — so the first change was the channel, not the messages.**

```text
gate        results/p13/diagnostics/acceptance_gate.md   (frozen before any change)
amendments  acceptance_gate_A1.md, acceptance_gate_A2.md (both recorded below)
baseline    the tree that closed P13-004
environment WSL2 Ubuntu 22.04, gcc 11.4, CUDA 12.9, RTX 5000 Ada (sm_89)
```

---

## 1. What the audit found

```text
Logger call sites in src/ + apps/ + cuda/      2          both warning(), both about GPU fallback
levels routed to stdout                        3 of 5     Trace, Debug, Info
stderr bytes on a successful CLI run           0
log points already carried in a struct         8 of 9
--log-level (or any equivalent)                did not exist
```

`cfd::core::Logger` is thread-safe, dependency-free and has five levels. It was used twice. Nothing
needed inventing; what needed care was that **Info went to stdout**, which is where the CLI writes
its report — the same stream `CFDAppCliValidCase` matches `"Converged: yes"` on. A dozen log points
at Info would have interleaved diagnostics with results on one stream.

So the item's first change is one line:

```cpp
bool targetsStderr(LogLevel) noexcept { return true; }   // was: Warning || Error
```

Results go to stdout. Everything the program says *about* a run goes to stderr. That is what makes
`cfdapp --case x > report.txt 2> diagnostics.log` mean something, and it is what allowed the log
points to be added at all.

## 2. The log points

Eight points across the run, all reading state that was already computed and returned — nothing
here derives a new number, so no log line can disagree with the report or the export.

```text
L1  loading case: <dir>                                   Debug
L1  case rejected: <the full diagnostic>                   Debug
L2  linear solver backend, in the solver-start line        Debug
L2  GPU discretization declined, with the reason           Warning   (pre-existing, in SIMPLE.cpp)
L3  solver start: <case> (<solver>, <cells> cells, ...)    Debug
L4  checkpoint written at iteration N: <file>              Info
L4  resumed from iteration N: <file>                       Info
L5  linear solve failed: <status> -- <detail>              Debug
L6  solver state went non-finite at iteration N            Debug
L7  export failed: <reason>                                Debug
L8  solve finished: status, iterations, u/v/p/continuity
    residuals, mass imbalance, seconds, CPU or GPU         Info
```

All eight observed in a real process at `--log-level debug` (`tools/verify_V_L.sh`,
`logs/06_V_L.log`). L2's fallback half is **not** re-emitted: `SIMPLE.cpp` already warns with the
reason at the point it declines, for every caller, and a second copy would be exactly the
duplication amendment A1 removed elsewhere.

## 3. `--log-level`, and the default that keeps a run quiet

```text
cfdapp --case <dir> --log-level <trace|debug|info|warning|error>
```

The default is **warning**, so a run's stderr is byte-for-byte what it was before this item
existed. Measured, 7 cases: `stderr differing: 0`.

An unknown level is refused, not ignored, and the refusal names the valid ones from the logger
itself rather than a second hardcoded copy:

```text
$ cfdapp --case cavity --log-level shout
Error: --log-level must be one of trace, debug, info, warning, error, got "shout"
```

## 4. Two gate amendments, both recorded before the result

**A1 — the gate contradicted itself, and the measurement proved it.** Group L put L1/L5/L6/L7 at
Error; V4 required the default run's stderr to be unchanged. With the default level at Info, both
could not hold — measured, 7 of 7 stderr files differed. The three rejection cases were the
informative ones: their baseline stderr was *not* empty, because the CLI already prints the
rejection. The `[ERROR]` line was a verbatim second copy of a message the user was already shown.

V4 was **not** weakened. The level assignments changed, under one rule stated once:
*ProjectRunner returns every failure to its caller and the caller reports it, so it does not also
log failures at Error.* And the CLI sets its own default to Warning, making V4's own stated
rationale — *"diagnostics are available on request, not imposed"* — literal. No existing behaviour
changed, and that is measured rather than assumed: the audit found 2 call sites, both `warning()`,
and Warning is still emitted at the new default.

**A2 — a control came back UNDETECTED, and that was reported, not buried.** N1 (route Info back to
stdout) was supposed to break C1 *and* C2. It broke C1 and could not reach C2: after A1 nothing is
emitted at the default level, so nothing can leak there. C2 is therefore PRESERVATION, which the
gate's own dry-run record had already said while Group C's heading called it DETECTION.

Rather than accept a criterion no control can exercise, the detection counterpart was added and
measured — **C2b: stdout is byte-identical at every `--log-level`**, 7 cases × 5 levels:

```text
                    intact    N1b (Info/Debug/Trace -> stdout)    restored
   total differing        0                                 18           0   of 35
```

## 5. Results

```text
C1   every level writes to stderr                         PASS   (unit + process level)
C2   default stdout byte-identical, 7 cases               PASS   0 differing
C2b  stdout byte-identical at all 5 levels, 7 cases       PASS   0 of 35 differing
C3   the two pre-existing warning() sites unchanged       PASS   still emitted at the new default
L1-L8 every point emitted and observed                    PASS   8 of 8
V1   --log-level accepted at all five levels              PASS
V2   an unknown level is rejected, naming the valid ones  PASS   exit 1
V3   strictly increasing verbosity                        PASS   error 0 < info 1 < debug 3 lines
V4   default stderr unchanged, 7 cases                    PASS   0 differing
P3   no logging in a hot loop                             PASS   0 sites inside the loop;
                                                                 3 stderr lines at 5 AND at 50 iterations
```

Non-vacuity controls — each must fail, and each did:

```text
N1   Info routed back to stdout            -> C1   3 log lines on stdout      DETECTED
N1b  Info/Debug/Trace back to stdout       -> C2b  18 of 35 files differ      DETECTED
N2   the CLI's explicit default removed    -> V4   4 of 7 stderr files differ DETECTED
N3   the comparison instrument itself      -> 1 byte appended, 1 reported     DETECTED
N4   one L point's emission deleted        -> V3   3 debug lines become 2     DETECTED
N5   the stdout-purity ctest fed stderr    -> the test fails                  DETECTED
```

Every control restored its sources from a pre-control copy with the restore verified by **sha256**,
and the restored tree rebuilt.

## 6. Permanent tests

```text
tests/unit/core/test_logger.cpp
  LoggerTest.EveryLevelGoesToStderrAndNeverToStdout    all 5 levels, both streams captured
  LogLevelNames.EveryLevelRoundTripsThroughItsName
  LogLevelNames.UnknownNameIsRejectedRatherThanDefaulted
  LogLevelNames.TheAdvertisedNamesAreExactlyTheOnesThatParse
tests/CMakeLists.txt
  CFDAppCliLogLevelIsDocumented
  CFDAppCliLogLevelUnknownIsRejected
  CFDAppCliCheckpoint_log_level_missing_arg
  CFDAppCliLogLinesNeverReachStdout                    a real process, at --log-level trace
```

`CFDAppCliLogLinesNeverReachStdout` needs a wrapper (`tests/cmake/RunCliStdoutOnly.cmake`) because
ctest matches its regexes against stdout and stderr **combined** — which is precisely the
distinction this test exists to make. The wrapper re-emits stdout only, and gives the run its own
copy of the case so it cannot race `CFDAppCliValidCase` over one `results/` directory.

Two existing assertions in `test_logger.cpp` asserted the old routing (`[INFO]` on stdout) and were
updated to the new contract. That is a contract change, stated here rather than hidden in a diff.

## 7. A note on the format gate

`final.sh`'s first format pass scanned the whole tree with the default `clang-format`, which in
this image is version **14**. Both the scope and the version were wrong: `ci.yml` runs
`find include src apps tests` and pins **clang-format-18**, with its own comment recording that 14
and 18 give genuinely different output. Re-measured with the instrument CI uses:

```text
clang-format-18 violations in include/ src/ apps/ tests/:   0
```

clang-format-14 disagrees on 4 pre-existing test files that P13-005 does not touch. Recorded in
`logs/05_format.log` rather than silently narrowed away.

## 8. Regression (criteria P1, P2)

```text
                          tests passed   failed   LastTest.log
Release + CUDA                    2083        0   2026-09-21T14:04:23Z
Debug + GUI                       2077        0   2026-09-21T14:48:16Z
CPU-only                          2017        0   2026-09-21T14:57:35Z
P13-001..004 gate subset            79        0
clang-format-18, CI scope            0 violations
```

**The instrument had to be corrected before these numbers meant anything.**
`final.sh` reported each suite as `RC=0`, which is worthless: in
`ctest ... | tail -4; echo "RC=$?"` the status is **tail's**, and it is 0 whether or not a test
failed. The counts above come from ctest's own per-test record
(`Testing/Temporary/LastTest.log`) instead — stronger evidence than the summary line it lost,
because it states how many tests ran rather than only that none failed. All three log timestamps
fall inside this run. `logs/07_ctest_verification.log`.

Two stale `LastTestsFailed.log` files are present and are **not** from this run — both older than
their own `LastTest.log`: `build/final` 13:54 is control N5 (which deliberately broke
`CFDAppCliLogLinesNeverReachStdout`, then restored it) and `build/debug` 11:44 is a P13-003 run.
The verification prints both timestamps side by side so this cannot be misread.

## 8. What this item did not do

* **No logging framework.** One existed, and replacing a sound dependency-free logger would have
  been the kind of unrelated rework the brief rules out.
* **No per-iteration logging.** P3 is guaranteed structurally — there is no log call inside the
  outer loop — rather than by a benchmark that would have to be re-run forever.
* **No GUI `--log-level` equivalent.** It is a command-line control; the GUI surfaces the same
  information in its own UI. Recorded in `docs/user_guide/cli.md` alongside the cancellation
  asymmetry, not left as an undocumented gap.
* **No log file, rotation or structured (JSON) output.** Not asked for, and `2> file` already gives
  a user the file. Adding a second output format would mean a second thing to keep deterministic.
