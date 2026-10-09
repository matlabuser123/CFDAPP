# P18.1 — regression

```text
HARNESS       tools/p18_1_regress.sh -- P18.0's baseline harness, reused with only the output
              directory and the predictions changed. Its two inherited fixes are retained:
              DISABLED tests are COUNTED, and the four build options are ASSERTED after configure,
              failing closed on a mismatch.
POLICY        OMP_NUM_THREADS=1 · ctest -j 8 · --timeout 7200 · the same 5 frozen exclusions.
              Inherited verbatim from P17.10 / P13 G10. The timeout was NOT raised.
LOG           logs/P18_1_regress.log  (+ per-configuration configure / build / staleness / ctest)
```

---

## 1. The prediction, declared before the run

P18.1 is additive: two new production translation units, one new test file, two CMakeLists lines.
`gtest_discover_tests` registers **one ctest row per gtest test**, so the 60 new gtest tests
(12 `SpeciesRegistryTest` + 48 `MassFractionStateTest`, counted from `--gtest_list_tests` at
`logs/07_test_counts.log`) add 60 rows to every configuration. None matches the 5 frozen exclusion
patterns, so *selected* rises by 60 too. **Skipped and DISABLED must not move**, because the new
tests are neither.

```text
ANY of these would have been a P18.1 finding: a fifth Debug+GUI failure, a changed
Skipped/DISABLED count, or a delta other than exactly +60.
```

## 2. Results

| Configuration | | selected | Passed | FAILED | Skipped | DISABLED | runtime | ctest exit |
|---|---|---:|---:|---:|---:|---:|---:|---:|
| **Release+CUDA** | predicted | 2823 | 2772 | 0 | 2 | 49 | | |
| | **measured** | **2823** | **2772** | **0** | **2** | **49** | 2060 s | 0 |
| **CPU-only** | predicted | 2600 | 2553 | 0 | 0 | 47 | | |
| | **measured** | **2600** | **2553** | **0** | **0** | **47** | 1442 s | 0 |
| **Debug+GUI** | predicted | 2885 | 2830 | 4 | 2 | 49 | | |
| | **measured** | **2885** | **2831** | **3** | **2** | **49** | 8172 s | 8 |

```text
BASELINE FOR COMPARISON (results/p18/baseline/summary.md, itself an exact reproduction of P17.11)
  release_cuda  2763 selected, 2712 Passed, 0 FAILED, 2 Skipped, 49 DISABLED, 2226 s
  cpu_only      2540 selected, 2493 Passed, 0 FAILED, 0 Skipped, 47 DISABLED, 1675 s
  debug_gui     2825 selected, 2770 Passed, 4 FAILED, 2 Skipped, 49 DISABLED, 8514 s

DELTA MEASURED
  release_cuda  +60 selected, +60 Passed, 0 elsewhere       EXACTLY AS PREDICTED
  cpu_only      +60 selected, +60 Passed, 0 elsewhere       EXACTLY AS PREDICTED
  debug_gui     +60 selected, +61 Passed, -1 FAILED         DEVIATION -- see section 2a
```

## 2a. THE ONE DEVIATION, AND WHY IT IS NOT P18.1'S

```text
PREDICTED  4 FAILED (the inherited DriverSeegmiller timeout set)
MEASURED   3 FAILED
```

One fewer failure is still a deviation from a declared prediction, and CLAUDE.md §8 is explicit
that "a known failure stays known only while it reproduces its recorded values exactly. Any new or
changed failure is a regression." It was therefore investigated, not accepted.
Full attribution: `logs/11_debug_gui_deviation.log`.

```text
WHICH TEST MOVED
  47/2885 #2814 DriverSeegmiller.MeshSequence                      Passed   7123.03 s
  51/2885 #2819 DriverSeegmiller.CorrectedSequenceIsDeterministic  Timeout  7200.64 s
  52/2885 #2821 DriverSeegmillerInlet.MappedInletPreservesTheMassFlow  Timeout  7200.65 s
  53/2885 #2822 DriverSeegmillerInlet.MediumGridAB                 Timeout  7200.67 s

MeshSequence PASSED, with 76.97 s of margin against the 7200 s cap -- 98.93% of the limit.
```

Five independent lines of attribution:

```text
1. THE CAP WAS NOT RAISED. The harness ran --timeout 7200, and the other three fired at
   7200.6x s, so the limit was demonstrably in force.

2. P18.1 CANNOT REACH THIS TEST. Exactly SIX files in the repository name either new type:
   the two headers, the two sources, their test, and src/CMakeLists.txt. References inside
   tests/integration/backward_facing_step/ : 0. The test cannot construct, call or link
   against MassFractionState or SpeciesRegistry.

3. THE SAME TEST PASSES IN RELEASE, in this same run: release_cuda #2814 Passed in 1213.43 s,
   5.9x under the cap. The Debug+GUI problem is a TIME BUDGET, not a correctness defect --
   precisely what P17.11 recorded when it left G10 PARTIAL.

4. THE RUN WAS 4.0% FASTER OVERALL (8172 s against the baseline's 8514 s). A test sitting at
   ~100% of the cap crosses it on a run 4% faster. MeshSequence landed at 98.93%.

5. THE ARITHMETIC CLOSES EXACTLY. baseline 2770 P + 4 F = 2774 non-disabled outcomes.
   measured 2831 P + 3 F = 2834 = 2774 + 60 new tests. Exactly one test moved FAILED -> Passed
   and the 60 new rows all passed. NO NEW FAILURE APPEARED, in any configuration.
```

**A refinement of the known limitation, which is the useful output of this.** The P17.11 / P18.0
characterisation of "4 inherited Debug+GUI timeouts" is imprecise. The set is really
**3 reliable timeouts plus 1 marginal test sitting within ~1 % of the cap**, whose result depends
on machine load. That matters for G10: a marginal test does not reproduce, so "the identical set,
test id for test id" is not a property P18.9 can rely on. Reported for `TODO.md`.

```text
VERDICT   NOT a P18.1 regression. No new failure, in any configuration. The one movement is in a
          pre-existing test that P18.1 provably cannot reach, in the favourable direction, and is
          fully explained by a 4% faster run against a test at 98.9% of its timeout.
```

Runtimes are −7.5 % and −13.9 % against the baseline — ordinary machine variance on
multi-thousand-second runs, and in the faster direction, so not a symptom of work not done. The
row counts, not the runtimes, are the evidence.

## 3. Configuration was asserted, not assumed

Each configuration's four build options were read from `CMakeCache.txt` **after** configure and
checked against expected values, failing closed on any mismatch:

```text
release_cuda   build=Release  gui=OFF  cuda=ON   openmp=ON     asserted OK
cpu_only       build=Release  gui=OFF  cuda=OFF  openmp=ON     asserted OK
debug_gui      build=Debug    gui=ON   cuda=ON   openmp=OFF    asserted OK
```

**The CPU-only result matters beyond its counts.** Files compiled only when CUDA is OFF are not
exercised by a CUDA build, and this project has a recorded blind spot there. `cpu_only` builds and
passes with `CFDAPP_ENABLE_CUDA=OFF`, so the new translation units are proved to compile and pass
in both configurations, not just the CUDA one.

## 4. Staleness — the result is not from stale binaries

```text
release_cuda   50 test binaries, 0 stale vs the newest production source, 0 unverifiable
               VERDICT: all 50 built test binaries postdate the newest production source AND
                        every source they actually compile, per CMake's own object lists
```

`staleness.py` (P17.7's, unchanged) fails closed: a binary it cannot verify counts as stale. This
is what makes the counts authoritative rather than merely recent.

```text
library hashes
  release_cuda  libcfdcore.a  a2d4ea6f6c633bdf57938c46f571f51443455b34bce3fe164c7d35be4d909aac
                libcfdcuda.a  1e2e0f914afd40ef1f624f03cdb2bbee8aa141d46d780ad1bbcc3c29c404290a
  cpu_only      libcfdcore.a  763b6ef4c0f096f66ca4b7be190df3a34f949896d9edc578d06f1c1f0a5baa48
```

## 5. A run was abandoned and restarted — recorded, not hidden

```text
logs/10_ABANDONED_regress_superseded.log

The first launch was stopped partway through release_cuda. Three compatibility tests against the
real shipped species case (section 31) were added AFTER it started, which would have left
release_cuda measured at +57 rows and the two later configurations at +60 -- three configurations
disagreeing with each other and with the declared prediction.

Stopping and restarting on the final tree was the correct call: CLAUDE.md section 8 requires the
test binary to be built against the exact source being tested. The abandoned log is preserved
rather than deleted, and the prediction was re-derived (+57 -> +60) from a fresh --gtest_list_tests
count BEFORE the second launch, not adjusted afterwards to fit.
```

## 6. Focused suite

```text
build/final (Release, CUDA OFF, OpenMP OFF), binary deleted before build and required back:
  SpeciesRegistryTest      12 / 12
  MassFractionStateTest    48 / 48
  pre-existing species     37 / 37
  total                    97 / 97 passed, 0 failed        logs/02_focused_tests.log
```

## 7. clang-format

```text
the five P18.1 files    --dry-run --Werror   exit 0
the tree as CI runs it  28 pre-existing offenders, none a P18.1 file -- inherited from the
                        uncommitted P14-P17 working tree and NOT fixed here (CLAUDE.md section 2).
                        Reported for TODO.md technical debt.     logs/08_format.log
```

## 8. Sanitizers

```text
NOT RUN. P18.1's frozen gate (G2) does not require them, and CLAUDE.md section 8 places the
sanitizer configuration in the FULL regression that G11 settles at P18.9. Recorded as NOT RUN
with the reason rather than silently omitted.

Noted for that phase: MassFractionState owns std::vector<ScalarField> and performs no manual
memory management, no pointer arithmetic and no unchecked indexing on any public path -- every
accessor bounds-checks and throws. There is correspondingly little for ASan/UBSan to find here,
but that is an argument for expecting a clean run, NOT a substitute for one.
```
