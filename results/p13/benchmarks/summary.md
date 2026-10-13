# P13-007 — Production benchmark suite: summary

**There were seven benchmark programs and no suite.** Nothing ran them together, nothing compared
them to anything, and not one of them could report a failure. They also wrote into the
repository's committed evidence.

```text
gate        results/p13/benchmarks/acceptance_gate.md   (see its own note on when it was frozen)
amendment   acceptance_gate_A1.md                       (the threshold derivation was wrong)
audit       results/p13/benchmarks/audit.md
baseline    the tree that closed P13-006
environment WSL2 Ubuntu 22.04, i9-14900HX (32 cores), gcc 11.4, CUDA 12.9, RTX 5000 Ada
```

---

## 1. What was wrong, measured

```text
B1  benchmarks accepting an output path                       0 of 5
B2  tracked files a run can overwrite                        34 across 5 directories
B3  benchmarks registered with add_test() / referenced by CI  0 / 0
B4  sites returning nonzero across all 7 benchmarks           0
B5  committed baseline                    0.1.0-dev, OpenMP off, CUDA off
```

**B2 is the concrete harm, and it happened during this work.**
`results/validation/grid_convergence/natural_convection_ra1e3.json` changed underneath the session
— `runtime_seconds` only (14.557→5.856, 60.775→21.999, 99.555→48.292), every physical quantity and
solver status identical. A run writing to a path relative to its working directory. Reverted.

**B4 is why "suite" was a misnomer.** Zero `return 1`, zero `EXIT_FAILURE` across all seven
executables. "No severe performance regression" was detectable only by a person opening a CSV and
remembering last month's numbers.

**B5 is why the committed baselines could not be used.** They describe a different program —
0.1.0-dev, serial, no CUDA. Diffing today's build against them and calling it a regression would
be a confident wrong answer.

## 2. What was built

```text
benchmarks/BenchmarkOutput.hpp           --output-dir, added to all 5 file-writing benchmarks
scripts/benchmarks/run_suite.sh          one driver: runs, captures the environment, verdicts
scripts/benchmarks/compare_benchmarks.py regression detection that can actually fail
tests/cmake/RunBenchmarkSmoke.cmake      a ctest entry, so the suite cannot rot unnoticed
```

Two benchmarks were **left alone**: `spmv_scaling` and `gpu_spmv` write no files at all. Giving
them an output directory would have been a change with nothing behind it.

The **default is unchanged**: a benchmark invoked with no arguments writes exactly where it always
did. Changing that would invalidate every documented recipe and make recorded results wrong about
their own provenance. `--output-dir` is the opt-in; the driver always passes it. A missing argument
exits 2 rather than falling back — a run told to write elsewhere that quietly wrote into the
repository is the failure this closes.

## 3. The threshold, and the amendment that corrected it

The gate quoted a measured 7.9% worst-case spread and set the threshold at 1.25x, "roughly 3x that
worst case". **That was the wrong measurement**, and A1 records it rather than adjusting it
quietly:

```text
same binary, four separate recordings     20x20     40x40     80x80
                               min       1.5030    6.8588   30.5893
                               max       2.0648    8.8989   37.4993
                           max/min        1.37x     1.30x     1.23x
```

7.9% was **within-session** spread — five runs back to back on a quiet machine. Against a *stored*
baseline the machine's state at record time and check time are unrelated, and the same binary
spanned 1.37x. A 1.25x threshold would have fired with no code change at all.

**The threshold was not raised.** A tolerance loose enough to accept 1.37x could not detect a 30%
regression — it would report PASS over a real slowdown, which is worse than no check. The variable
was controlled instead:

```text
1-minute load > 25% of core count   ->  exit 2, nothing recorded
--allow-loaded                      ->  records, stamped "loaded: yes -- NOT COMPARABLE"
```

With load controlled the within-session figure applies, and 1.25x is ~3x it.

**Stated in the tool itself:** a regression smaller than 25% is invisible to this check.

## 4. Four ways the comparison refuses to pass vacuously

```text
no metric comparable          -> FAIL ("this check proved nothing")
a baseline case missing       -> FAIL, not a skip
environments differ           -> refuses to compare at all
a run recorded under load     -> marked NOT COMPARABLE, and refused
```

Admission is a **dimensional floor**, not a tolerance: a metric is compared only when its baseline
value clears 1.0 ms / 0.05 s. The noisy quantities are exactly those near the clock's resolution
(`mass_flux_seconds`: 362% spread between identical runs), so the floor removes them structurally
rather than by stretching a tolerance wide enough to swallow a 0.0001-second number.

## 5. Results

```text
O1  --output-dir on all 5 file-writing benchmarks     PASS
O2  a suite run leaves tracked evidence untouched     PASS  git status unchanged before/after
O3  --output-dir with no argument                     PASS  exit 2
O4  an uncreatable output directory                   PASS  exit 2
O5  no arguments -> writes where it always did        PASS  (preservation)
O6  refuses to record on a loaded machine             PASS  exit 2 at loadavg 39.14 / limit 8.00
R1  a comparison returns a verdict                    PASS
R2  aggregate metrics only, dimensional floor         PASS
R3  identical-binary run                              PASS  0.96x / 0.95x / 0.98x, 3 compared
R4  an injected 1.60x slowdown                        PASS  FAIL at 1.57x, metric named
R5  comparing zero metrics                            PASS  reported as a failure
R6  a missing baseline case                           PASS  reported, not skipped
R7  threshold quoted against measured noise           PASS  see A1
S1  one driver, refuses without --output-dir          PASS
S2  environment captured beside the results           PASS  cpu/cores/compiler/version/GPU/loadavg
S3  reachable from ctest by a smoke entry             PASS  CFDAppBenchmarkSuiteSmoke
S4  the smoke entry runs no long benchmark            PASS  quick CPU benchmark only
E1  a baseline recorded on this machine               PASS  i9-14900HX, 0.2.0-dev, loaded: no
E2  cross-environment comparison refused              PASS  names the mismatching field
```

### Non-vacuity controls — and the round that proved nothing

```text
NV1  ignore --output-dir            0 files reached the requested directory     DETECTED
NV2  inject a 1.60x slowdown        FAIL at 1.57x, naming the metric            DETECTED
NV3  a baseline with no metric      "this check proved nothing"                 DETECTED
NV4  delete a case from the run     "missing 1 baseline case(s)"                DETECTED
NV5  mismatched CPU                 refused, naming the cpu                     DETECTED
NV6  threshold raised to 2.0x       PASSES -- the threshold is what detects     DETECTED
NV7  load guard removed             unguarded exit 0, guarded exit 2 @ 39.14    DETECTED
```

**The first run of NV2, NV4 and NV5 reported DETECTED and proved nothing.** The baseline had been
recorded before A1 added `loaded` to the environment file, so the environment check refused every
comparison and the comparator never reached the regression logic. All three "failed" for a reason
unrelated to what they test.

**NV6 is the only reason that was caught.** It requires the injected slowdown to *stop* failing at
a raised threshold — and it kept failing, because the failure was never about the threshold. A
control that must flip its verdict catches what a control that must merely fail cannot.

The fix was the cause, not the symptom: re-record the baseline with the current driver, and make
every control assert the **mechanism** rather than the exit status —
NV2 requires a `REGRESSION` line, NV4 the word `missing`, NV5 a `cpu:` mismatch. The re-run also
stops outright unless a sanity comparison reports "metrics compared" first, so a repeat of the same
mistake cannot manufacture four more false DETECTEDs.

## 6. Regression (criterion P2) — and the run that did not count

```text
                      ctest exit   tests passed   the smoke entry
Release + CUDA                 0           2049   #2094  Passed   34.73 s
Debug + GUI                    0           2043   #2088  Passed  179.63 s
CPU-only                       0           1983   #2028  Passed   28.09 s
clang-format-18, CI scope                     0 violations
benchmarks/ (outside the CI scope, checked anyway)   0 violations
```

**The first regression was green and did not count.** It reported 2048 / 2042 / 1982 — numbers
*identical* to P13-006's, when one more test should have appeared. `CFDAppBenchmarkSuiteSmoke` had
been registered in `tests/CMakeLists.txt` behind `if(TARGET cfd_benchmark_cpu)`, and the root
CMakeLists adds `tests/` (line ~114) **before** `benchmarks/` (line ~137) — so the target did not
exist yet and the guard silently dropped the entry. The test never ran; the suite reported success
without it.

The root CMakeLists already carries a comment about this exact class of bug from P5:
`gtest_discover_tests()` in a subdirectory added before `enable_testing()`, found because
`ctest -N` did not list tests whose binary passed standalone. This is its second appearance.

Registration moved to `benchmarks/CMakeLists.txt`, where the target exists, and the **full**
regression was re-run — not just `build/final` — because a run that did not contain the test cannot
support the criterion. The superseded log is kept at
`logs/final_SUPERSEDED_no_smoke_test.log`, annotated: a green run that did not cover what it
claimed is worth keeping visible.

### Two failures of one shape, minutes apart

```text
NV2 / NV4 / NV5   reported DETECTED   the comparator never reached the regression logic
the smoke test    reported PASS       the test was not in the suite at all
```

Both green. Both proved nothing. Neither was caught by the mechanism meant to catch it — NV6's
flip-the-verdict requirement found the first, an unchanged test count found the second.

**A passing check must be shown capable of failing, and "it passed" is not evidence that it ran.**

## 6. What this item did NOT do

* **No new benchmark programs.** Seven exist and measure real things.
* **No change to what any benchmark measures.** Changing the measurement and then comparing to a
  baseline would make the comparison meaningless.
* **No timing assertion inside ctest.** The smoke entry checks that the suite runs and writes what
  the comparator needs. A timing assertion running alongside 2000 other tests would measure the
  machine — which is precisely the effect that forced A1.
* **No portable baseline.** A baseline recorded on this i9-14900HX is valid for this machine only.
  The load guard makes recordings comparable, not portable; E2 is what stops a cross-machine
  comparison happening silently.
* **No CI benchmark job.** CI runs on shared hosted runners whose load is neither known nor
  controllable, which is the one condition this item established that measurement requires.
