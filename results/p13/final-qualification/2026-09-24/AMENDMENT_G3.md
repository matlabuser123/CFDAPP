# AMENDMENT — P13 gate G3 (full CTest): the invocation is stale

The failure is PRESERVED at `logs/G3_FAILURE_PRESERVED_j16_default_timeout.log`
(724 KB). Nothing about the code was changed in response to it.

---

## What happened

```text
ctest --test-dir build/qual-final -j16 --output-on-failure
  90% tests passed, 250 tests failed out of 2501
  Total Test time (real) = 27328.96 sec     (7 h 35 m)
  gate wall time 12:46:16 -> 20:32:35        (7 h 46 m)
```

## Classification: INVALID GATE / ENVIRONMENTAL — not a regression

Three pieces of evidence, in order of strength.

**1. Every failure is a Timeout. Not one is an assertion failure.**

```text
250  ***Timeout
 49  ***Not Run
  0  assertion failures
```

**2. The tests that timed out are ones that finish in milliseconds.**

```text
CFDAppCliValidCase                                       (Timeout)
ProjectRunnerTest.ValidCaseConvergesAndExportsResults     (Timeout)
CaseSessionTest.RunConvergingCaseMovesToCompleted         (Timeout)
CaseBuilderTest.SolvedResultMatchesManualSetupBitForBit   (Timeout)
```

A genuine regression does not make a CLI smoke test take 25 minutes.

**3. THE DECISIVE CHECK — the same tests pass on the same tree.**

Forty of the timed-out tests were looked up in today's `regress_both.sh p16`
run over `build/cuda`, same source, same machine, a few hours earlier:

```text
sampled                 40
PASSED in the -j8 run   40
not found / not passed   0
```

Same source tree, same hardware, opposite verdict. The variable is the
invocation, not the code.

## Cause

```text
P13 gate      -j16   no --timeout (default 1500 s)   no exclusions
regress_both  -j 8   --timeout 7200                  documented exclusions
```

Sixteen concurrent tests, a large share of them CUDA, contend for one GPU.
CUDA work serializes, so tests block rather than run; the fast CPU-side tests
queue behind them and hit the 1500 s default cap. That is a cascade, which is
why even a CLI smoke test timed out.

The invocation was written before P14, P15 and P16 added their GPU test load,
and before the four P20 automatic-wall suites became known-expected failures.
It is stale in exactly the way `build/final` being CUDA=OFF was stale: it
described the repository accurately when it was written.

## The amendment

G3 adopts the repository's own regression settings, which exist for precisely
this reason and are already the instrument P16 Step 5 was qualified with:

```text
ctest --test-dir build/qual-final -j 8 --timeout 7200 \
      -E 'SSTAutomaticWallChannel|SSTCompositeWallChannel|SSTMomentumFluxChannel|WallArchitectureDiagnostic|DriverSeegmiller.CorrectedMeshSequence' \
      --output-on-failure
```

**Nothing is weakened.** No tolerance moves, no criterion is dropped, and the
exclusions are the ones `regress_both.sh` already documents — its own header
states that leaving them in "makes it permanently red and MASKS a real
regression". The excluded suites keep their normal coverage and remain
observable through `run_p16_deferred.sh`.

`--timeout 7200` is not a relaxation to obtain a pass either: the 1500 s
default is below the runtime of `DriverSeegmiller.CorrectedMeshSequence`
(~1100 s alone, longer under load), so the default could report a timeout for a
test that was working correctly.

## What this cost, and the lesson

Seven hours and forty-six minutes, to produce a verdict about the harness
rather than the code. The signal was available early: I predicted G3 would fail
on five specific known-expected suites, and the actual result — 250 timeouts —
did not match that prediction at all. A prediction that misses by two orders of
magnitude is itself information, and checking the failure MODE (all Timeout,
zero assertions) would have classified this within a minute of the first
failures appearing rather than after the full run completed.

---

# CORRECTION — the first amendment treated a SYMPTOM

The amendment above blamed `-j16` and the 1500 s default timeout, and raised the
timeout to 7200 s. That was wrong, and the re-run proved it: with `-j8` and
`--timeout 7200` the gate completed **43 tests in 5.5 hours** — worse in wall
time than the run it replaced, because a starved test now blocks for two hours
instead of twenty-five minutes.

## The actual root cause, measured

```text
build/qual-final   CFDAPP_ENABLE_OPENMP = ON
build/cuda         CFDAPP_ENABLE_OPENMP = OFF    <- the tree that ran in 28 min
OMP_NUM_THREADS    unset, so it defaults to nproc = 32
```

`ctest -j8` x 32 OpenMP threads per process = **256 threads on 32 cores**. At
`-j16` it was 512. Every test is starved, so fast tests hit whatever cap is set
and slow ones crawl.

Measured directly on the same 21 tests, same tree, same `-j8`:

```text
OMP_NUM_THREADS unset   304.95 s wall   TESTS FAILED (hit the 300 s cap)
OMP_NUM_THREADS=1         1.72 s wall   21 tests, all passed
```

A factor of 178, and the difference between a red gate and a green one.

## Why this was invisible until now

`regress_both.sh` runs `build/cuda`, which is `OPENMP=OFF`, so it never
oversubscribes and has always completed in ~28 minutes. The P13 gate originally
ran `build/final`, which is also `OPENMP=OFF` today. **The oversubscription was
introduced by MY choice of `build/qual-final`** for G2 — made to keep the whole
gate on one clean tree, which was right in principle, but that tree is the only
OpenMP-enabled one in the qualification.

## The corrected amendment

```text
OMP_NUM_THREADS=1 ctest --test-dir build/qual-final -j 8 --timeout 7200 \
      -E '<the documented P20 exclusions>' --output-on-failure
```

`OMP_NUM_THREADS=1` is an execution-environment setting, not a test criterion.
It restores the thread behaviour the original gate had when it ran against an
`OPENMP=OFF` tree, so nothing is weakened and no threshold moves.

`--timeout 7200` is KEPT, for an independent reason: the longest legitimate
validation test runs ~31 min (1860 s), which already exceeds ctest's 1500 s
default. That part of the first amendment was correct even though its stated
cause was not.

## Cost

7 h 46 m on the first failure, then 5 h 31 m on a "fix" that addressed the wrong
variable. The tell was available immediately: the second run was SLOWER than the
first despite halving the parallelism, which is the signature of contention, not
of a timeout that needed raising. One 2-minute A/B measurement — the one finally
run above — would have settled it before either long run.
