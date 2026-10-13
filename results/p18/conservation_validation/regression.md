# P18.7 — regression (§57)

```text
HARNESS   tools/p18_7_regress.sh -- P18.0's baseline harness, reused with only the output
          directory and the predictions changed.
POLICY    OMP_NUM_THREADS=1 . ctest -j 8 . --timeout 7200 . the same 5 frozen exclusions.
          THE TIMEOUT WAS NOT RAISED.
LOG       logs/P18_7_regress.log, plus per-configuration logs
RUN       2026-10-01T10:13Z to 13:36:08Z
```

---

## 1. Results against the prediction declared before the run

| Configuration | | selected | Passed | FAILED | Skipped | DISABLED | runtime | exit |
|---|---|---:|---:|---:|---:|---:|---:|---:|
| **Release+CUDA** | predicted | 3028 | 2977 | 0 | 2 | 49 | | |
| | **measured** | **3028** | **2977** | **0** | **2** | **49** | 2084 s | 0 |
| **CPU-only** | predicted | 2805 | 2758 | 0 | 0 | 47 | | |
| | **measured** | **2805** | **2758** | **0** | **0** | **47** | 1441 s | 0 |
| **Debug+GUI** | predicted | 3090 | 3036 **or** 3035 | 3 **or** 4 | 2 | 49 | | |
| | **measured** | **3090** | **3036** | **3** | **2** | **49** | 8156 s | 8 |

```text
ALL THREE CONFIGURATIONS MATCH THE PREDICTION, Debug+GUI on its FIRST declared branch.

BASELINE (P18.6 close)
  release_cuda  3009 selected, 2958 Passed, 0 FAILED, 2 Skipped, 49 DISABLED
  cpu_only      2786 selected, 2739 Passed, 0 FAILED, 0 Skipped, 47 DISABLED
  debug_gui     3071 selected, 3017 Passed, 3 FAILED, 2 Skipped, 49 DISABLED

DELTA MEASURED -- +19 selected and +19 Passed in EVERY configuration. FAILED, Skipped and DISABLED
did not move anywhere. All 19 new tests passed in all three.

P18.7 IS VALIDATION-ONLY: no production file was added or modified, so the only possible source of
a count change was the new test file -- and that is exactly what the measurement shows.
```

## 2. The marginal timeout -- a seventh data point

```text
DriverSeegmiller.MeshSequence, Debug+GUI, against the unchanged 7200 s cap:

    P18.1    PASSED   7123.03 s     98.9%
    P18.2    TIMEOUT  7201.94 s    >100%
    P18.3    PASSED   6764.75 s     94.0%
    P18.4    PASSED   7034.57 s     97.7%
    P18.5    TIMEOUT  7200.12 s    >100%
    P18.6    PASSED   7168.06 s     99.6%
    P18.7    PASSED   6963.18 s     96.7%     <-- this run

SEVEN MEASUREMENTS, FIVE PASSES AND TWO TIMEOUTS, EVERY ONE WITHIN 6% OF THE CAP. P18.9's G10 now
has seven data points and should predict a BRANCH rather than a number.

NOT ATTRIBUTABLE TO P18.7, which changed no production source at all and whose tests are not
linked into any Seegmiller path. Release+CUDA and CPU-only show 0 FAILED.
```

## 3. The three failures

```text
  3024  DriverSeegmiller.CorrectedSequenceIsDeterministic      ***Timeout
  3026  DriverSeegmillerInlet.MappedInletPreservesTheMassFlow  ***Timeout
  3027  DriverSeegmillerInlet.MediumGridAB                     ***Timeout

The three RELIABLE timeouts, unchanged in character. NO NEW FAILURE.
```

## 4. Integrity (CLAUDE.md section 8)

| step | Release+CUDA | CPU-only | Debug+GUI |
|---|---|---|---|
| configure exit | 0 | 0 | 0 |
| build options ASSERTED | `Release/OFF/ON/ON` | `Release/OFF/OFF/ON` | `Debug/ON/ON/OFF` |
| build exit | 0 | 0 | 0 |
| staleness, fail-closed | 0 stale | 0 stale | 0 stale |
| ctest exit | 0 | 0 | 8 (the 3 timeouts) |

```text
NO STALENESS CONCERN AROSE THIS PHASE. P18.6 had one -- a header edited after a configuration had
built -- and it was resolved by hash comparison. P18.7 edited no production file at all, so the
question does not arise.
```

## 5. Formatting, whitespace, sanitizers

```text
clang-format-18, the P18.7 test file                         CLEAN
git diff --check                                             259 warnings, ALL pre-existing in
                                                             three CMakeLists no P18 phase authored
  in a P18.7 file                                            0
  in TODO.md or ROADMAP.md                                   0

sanitizers     NOT RUN. The P18.7 criteria do not require them and CLAUDE.md section 8 places the
               sanitizer configuration in the full regression G11 settles at P18.9. Recorded as
               NOT RUN with the reason, as P18.1 through P18.6 recorded it -- not as a pass.
```

## 6. results/validation

```text
50 tracked reports rewritten and CLASSIFIED before restoring (logs/04_validation_noise.log):
    37 distinct changed keys, every one in the timing / iteratively-converged allow-list
    10 physics fields checked, each 0 changed lines
    1074 changed lines inspected -- the positive control
    restored: 50 modified before, 0 after

AND THE MAGNITUDE QUESTION P18.6 LEFT OPEN IS NOW SETTLED. The scale-aware comparator reports:

    max relative change, NON-TIMING : 3.513e-06
      at distorted_mesh_mms.json :: orders[1].triplets[1].extrapolated_error
    keys at the convergence floor  : 810, max ABSOLUTE change 1.328e-08

The worst mover is a RICHARDSON-EXTRAPOLATED quantity, and KNOWN_LIMITATIONS L36 records that
category as moving <= 1.1e-05. 3.513e-06 is INSIDE it. L36 HOLDS; P18.6's alarm was entirely
instrumental, and P18.7 closes that finding.
```
