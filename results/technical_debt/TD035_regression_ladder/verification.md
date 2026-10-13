# TD-035 — the sanitizer and Debug+GUI rungs are RUN. Sanitizers are CLEAN, the cost is MEASURED, and 0 of 32 failures are unexplained.

```text
STATUS:      **TWO OF THE THREE MISSING RUNGS ARE DONE, and the push blocker's own premise is
             discharged.** TD-035 said the blocker stood because "the full 2860-test asan
             preset's cost is UNMEASURED". It is now measured: **25 673.72 s = 7.13 h**, and
             the sanitizers report **ZERO diagnostics**.
               Release                  2844 tests,  6 failed, 6908.34 s -- ALL KNOWN
               Debug + GUI              2906 tests, 18 failed, 15526.38 s
               sanitizers (ASan+UBSan)  2832 tests, 14 failed, 25673.72 s, 0 sanitizer findings
               clang-format             742 files, 0 offending (recorded with TD-046)
             **THE SECTION 8 LADDER IS 4 OF 4 ON THIS TREE.**
             **All 32 failures across both runs are accounted for. None is a sanitizer finding
             and none is unexplained.**
             AND A CORRECTION TO TD-035's OWN TEXT, which I wrote: there are not "five
             sanitizer configurations". There is ONE, sharded five ways -- section 4.
SCOPE:       the two §8 rungs that had never been run. No production change, no test change.
             Release is re-running because MY OWN TD-046/TD-047 invalidated its recorded counts.
ENVIRONMENT: recorded per the hardware policy -- nproc 32, 31 GiB (WSL's share of 64),
             g++ 11.4.0, cmake 3.22.1, Ninja 1.10.1, clang-format-18 18.1.8, Ubuntu 22.04.5,
             HEAD b4b206f96b8d8c403be81725fddfcbd03b57e5fe.
EVIDENCE:    logs/01_asan.log · logs/02_debug.log
COMMIT:      carried as evidence. logs/03_release.log is DELIBERATELY NOT in this commit -- it
             was being written when the commit was made, and a half-written log would read as a
             truncated run.
```

---

## 1. SANITIZERS — the result that matters is a zero

```text
cmake --preset asan     CMAKE_BUILD_TYPE Debug · CFDAPP_ENABLE_SANITIZERS ON
                        CFDAPP_ENABLE_CUDA OFF · CFDAPP_BUILD_GUI OFF · OPENMP OFF
                        "AddressSanitizer + UndefinedBehaviorSanitizer enabled (GNU)"
build, every target     rc=0, 11 min 11 s
ASAN_OPTIONS            detect_leaks=1:halt_on_error=0
UBSAN_OPTIONS           print_stacktrace=1:halt_on_error=0
ctest --preset asan --timeout 7200 -j 8
                        99% tests passed, 14 tests failed out of 2832
                        Total Test time 25673.72 s = 7.13 h      ctest rc=8 (unmasked)

SANITIZER DIAGNOSTIC LINES: 0
  no "ERROR: AddressSanitizer", no "runtime error:", no "ERROR: LeakSanitizer",
  no "SUMMARY: AddressSanitizer", no "SUMMARY: UndefinedBehaviorSanitizer"
```

`-j 8` rather than 16, because the hardware policy says to go lower for sanitizer and
memory-heavy suites. `G8PremixedFlameTest` is excluded by the preset itself; its ASan cost is
~46 h on record.

### 1.1 The 14, classified

```text
4   SST channel tests (Failed)   the SAME FOUR that fail in Release, by name. TD-032's
                                 long-standing set.
10  TIMEOUTS                     and NINE OF THE TEN PASS IN RELEASE.
```

The timeouts are a **cost artifact**, and the Release durations prove it rather than assert it:

```text
DriverSeegmiller.IsDeterministic                 10:13   passes in Release
WallArchitectureDiagnostic.LowReSST...           20:16   passes in Release
DriverSeegmiller.MeshSequence                    39:17   passes in Release
DriverSeegmillerInlet.MappedInlet...             48:46   passes in Release
DriverSeegmiller.CorrectedSequenceIsDet...     1:01:51   passes in Release
DriverSeegmillerInlet.MediumGridAB             1:02:20   passes in Release
DriverSeegmiller.CorrectedMeshSequence         1:05:41   FAILS in Release (known, TD-032)
```

A test at 3941 s in Release cannot fit a 7200 s per-test cap once ASan slows execution. **The cap
is not raised**: §7 forbids inflating a limit to obtain a pass, so the honest outcome is that this
configuration's effective coverage is ~2822 of 2832 tests plus the G8 exclusion, recorded as a
limit the same way the G8 exclusion already is.

## 2. DEBUG + GUI

```text
cmake --preset debug    CMAKE_BUILD_TYPE Debug · CFDAPP_BUILD_GUI ON
                        CFDAPP_ENABLE_SANITIZERS OFF · CUDA OFF · OPENMP OFF
build, every target     rc=0
ctest --preset debug --timeout 7200 -j 16
                        99% tests passed, 18 tests failed out of 2906
                        Total Test time 15526.38 s = 4.31 h      ctest rc=8 (unmasked)

18 = 4 known SST (Failed)
   + 5 Seegmiller-family TIMEOUTS
   + 9 G8PremixedFlameTest TIMEOUTS
unexplained: 0
```

### 2.1 A PREDICTION THAT WAS RECORDED BEFORE THE RUN, AND HELD

`step9_debug.sh` carried this before it executed:

> *"TD-046 made the lambda(T) test actually execute its two solves; it took 2866 s in RELEASE.
> Debug is typically 5-15x slower on numerical code... So I expect NEW Debug timeouts from the G8
> flame cases."*

**It held, and more broadly than predicted: ALL NINE G8 flame cases time out, not the two named.**
In Release they take 40-70 minutes each, so Debug's slowdown puts every one past the cap. A cost
artifact of the configuration, created by TD-046 making a test that previously died at 6 ms
actually run.

### 2.2 THE RECORDED BASELINE IS NOT A VALID COMPARATOR, and I had intended to use it

`results/p18/final_qualification/logs/regression/debug_gui_ctest.log` records 3078 tests, 4
failed, 8402.72 s. **It cannot be compared to this run:**

```text
that baseline had CUDA ON     it ran CudaSpmvTest; 410 GPU-ish test lines; "434/3130" discovered
this run has CUDA OFF
G8PremixedFlameTest at that baseline:  0 mentions
G8PremixedFlameTest in this run:      76 mentions
```

So the 9 G8 timeouts cannot be a regression against a baseline whose test set did not contain
them, and the 3078 -> 2906 count change is the CUDA difference, not disappearing tests.

**What IS comparable, and it matters:** that baseline's four `DriverSeegmiller` timeouts are **all
four still present** here. `P18_GATES.md` G11 REQUIRES them, so their continued presence is a
pass, not a failure.

## 3. WHY CUDA OFF IS THE RIGHT CONFIGURATION

```text
.github/workflows/ci.yml:103   cmake --preset ${{ matrix.build_type }}   -- no CUDA flag
.github/workflows/ci.yml:290   cmake --preset asan                       -- no CUDA flag
runs-on: ubuntu-latest         GitHub-hosted; no GPU
```

CI configures with no CUDA flag on runners with no GPU, so **CUDA OFF is CI's configuration**, and
§7 makes CI's settings the reference for sanitizer runs. Both runs here match it. The P18 baseline
was a local full-GPU run — a third configuration, matching neither CI nor this.

## 4. A CORRECTION TO TD-035's OWN TEXT

TD-035 and TODO.md both say *"the five sanitizer configurations"*. **I wrote that, and it is a
misreading.**

```text
ci.yml:266   name: sanitizers (shard ${{ matrix.shard }}/5)
ci.yml:272   matrix: shard: [1, 2, 3, 4, 5]
ci.yml:290   run: cmake --preset asan      <- the SAME preset in every shard
```

There is **ONE** sanitizer configuration, sharded five ways for wall-clock. A single full-preset
run covers the union of all five shards and is therefore stricter than any individual shard.

### 4.1 And the measured cost corroborates CI's own model

```text
ci.yml's comment:  "Modelled worst shard: 93 min of tests (+ ~6 min configure/build)"
5 x 93 min = 465 min        measured here: 25673.72 s = 428 min
```

Within 8 % of CI's own estimate, from an independent measurement. That is a cross-check on the
number TD-035 called unmeasurable.

## 5. RELEASE, RE-RUN -- and two of three predictions were exact

```text
cmake --preset release   Release · CUDA OFF · GUI OFF · OpenMP OFF
library                  48b3dfb02b05836330477012ad80f9887218ca6146cc9016dff7ebf2477fdf5f
ctest --preset release --timeout 7200 -j 16
  99% tests passed, 6 tests failed out of 2844
  Total Test time 6908.34 s      ctest rc=8 (unmasked)

  1862 G8PremixedFlameTest.TheConsumptionIntegralAgreesWithTheEigenvalue  (Failed)  known, TD-003
  2755 SSTAutomaticWallChannel.DoesNotRegressTheAdoptedHighReGate         (Failed)  known, TD-032
  2759 SSTAutomaticWallChannel.StaysWellBehavedOnMeshes...                (Failed)  known, TD-032
  2762 SSTCompositeWallChannel.DiscriminatingMeshesAtReTau590             (Failed)  known, TD-032
  2764 SSTMomentumFluxChannel.FourWayComparisonAtReTau590                 (Failed)  known, TD-032
  2891 DriverSeegmiller.CorrectedMeshSequence                             (Failed)  known, TD-032
```

**`AVariableConductivityChangesTheAnswerAndIsNotFrozenAtTheInletValue` is GONE from the failure
list** -- TD-046 confirmed in the full suite rather than only in its own focused run.

### 5.1 The predictions, scored

Recorded before the run: *"Expect roughly 2844 tests, 6 failures, and ~2860 s longer."*

```text
tests      predicted 2844   measured 2844     EXACT
failures   predicted 6      measured 6        EXACT
wall time  predicted +2860 s   measured -236 s   WRONG
```

**The timing prediction was wrong because the model was wrong.** I summed the lambda(T) test's
2866 s onto the total, but at `-j 16` the wall clock follows the CRITICAL PATH, not the sum: a test
that runs in parallel with others adds nothing to it. The run came in 236 s SHORTER than the
2026-10-11 run's 7144 s, within run-to-run contention variance. The counts were right for the
right reason; the time was wrong for a reason worth recording.

## 6. THE RUNTIME NOISE, CLASSIFIED AND RESTORED -- and waiting mattered

The three runs rewrote **53 tracked files**. Section 7 licenses restoring *timing-only* changes,
so each was classified rather than assumed, by a **fail-closed** probe
(`scratchpad/classify_noise.py`): anything it cannot PROVE is timing-only is reported as SUSPECT.

```text
49  TIMING-ONLY   restored with `git checkout --`, rc=0
 4  BUILD STAMP   KEPT, not restored: "458457542a2e-modified" -> "b4b206f96b8d-modified"
                  cases/heated_cavity/results/metadata.json
                  tests/data/cases/{does_not_converge,mesh_quality_warning_cli,
                                    valid_cavity_cli_smoke}/results/metadata.json
                  each contains EXACTLY 2 changed lines, both build_identity, nothing else
```

`build_identity` is the commit SHA embedded at configure time via `Version.hpp`; it moved because
HEAD moved. **That is not timing noise, so section 7's licence does not cover it** -- restoring
would re-assert a stale SHA. Section 9 requires committed case outputs to stay byte-identical or
every change to be explained: this one is explained, and whether these four should track HEAD or be
pinned is a decision left to the user, not taken here.

### 6.1 CLASSIFYING MID-RUN WOULD HAVE PRODUCED A FALSE REGRESSION REPORT

A provisional classification run *while the Release suite was still executing* flagged three files
with ~294 lines of apparent floating-point regressions in MMS error norms:

```text
distorted_mesh_mms.json           l1 0.001908553421221324 -> 0.0019085534139583096   (75 lines)
distorted_mesh_momentum_mms.json  l1 0.002401926286882332 -> 0.0024019262869527298  (146 lines)
curved_channel_multiblock_...     velocity_l2_error 0.004837588904357929 -> ...9150  (73 lines)
```

With `OpenMP OFF` and `CUDA OFF` the solver should be bit-deterministic, so these looked like a
real regression worth a two-library attribution. **On the COMPLETED run all three are
timing-only.** They were an artifact of reading files while tests were writing them. Restoring or
investigating on the provisional read would have chased a phantom and risked reporting a numerical
regression that does not exist.

### 6.2 TWO BUGS IN MY OWN CLASSIFIER, both found and fixed

```text
BUG 1  the timing-key regex required a closing quote immediately after "runtime", so
       "runtime_seconds" fell through as unclassified -- ~20 FALSE POSITIVES.
BUG 2  the markdown column lookup picks the wrong header index, labelling runtime cells as
       "status" or "(col 7)" -- 4 FALSE POSITIVES, confirmed by hand against each table header:
       every one has `runtime (s)` as its last column and only that column changed, with grid,
       cells, h, Converged, iterations and mass imbalance all identical.
```

Both inflated the SUSPECT list. Neither could cause a real change to be restored away, because the
probe fails closed in that direction only.

## 7. WHAT IS NOT ESTABLISHED

```text
NOT established  that the 4 build-stamp files should be committed. They are left MODIFIED and
                 unstaged; the decision is the user's (section 6).
NOT established  that TD-035 can close. Its premise -- unmeasured cost -- is discharged, and the
                 sanitizers are clean, but closing it needs the full ladder including Release,
                 and a decision about whether 10 cost-driven ASan timeouts and 14 in Debug are
                 acceptable as recorded limits. That is a judgement for the user, not a
                 measurement.
NOT established  that CI would show the same timeouts. CI shards, so each shard runs fewer tests
                 per job, but --timeout 7200 is PER TEST and sharding cannot help a single slow
                 test. Whether GitHub's runners are faster or slower than this machine for these
                 cases is unmeasured.
NOT run          native Windows, and CUDA under either Debug or ASan. §4 of CLAUDE.md: WSL2
                 evidence does not establish native Windows. Both runs here are WSL2, CUDA OFF.
NOT investigated why the 4 SST tests fail. They are carried as TD-032's long-standing set and
                 were not re-derived here; this run confirms they still fail, by name, in two
                 more configurations.
```
