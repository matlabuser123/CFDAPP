# P18.6 — regression (§59)

```text
HARNESS   tools/p18_6_regress.sh -- P18.0's baseline harness, reused with only the output
          directory and the predictions changed. DISABLED tests are COUNTED and the four build
          options are ASSERTED after configure, failing closed on a mismatch.
POLICY    OMP_NUM_THREADS=1 · ctest -j 8 · --timeout 7200 · the same 5 frozen exclusions.
          THE TIMEOUT WAS NOT RAISED.
LOG       logs/P18_6_regress.log, plus per-configuration configure/build/ctest/staleness logs
RUN       2026-10-01T05:48Z to 09:27:30Z
```

---

## 1. Results against the prediction declared before the run

| Configuration | | selected | Passed | FAILED | Skipped | DISABLED | runtime | exit |
|---|---|---:|---:|---:|---:|---:|---:|---:|
| **Release+CUDA** | predicted | 3009 | 2958 | 0 | 2 | 49 | | |
| | **measured** | **3009** | **2958** | **0** | **2** | **49** | 2417 s | 0 |
| **CPU-only** | predicted | 2786 | 2739 | 0 | 0 | 47 | | |
| | **measured** | **2786** | **2739** | **0** | **0** | **47** | 1510 s | 0 |
| **Debug+GUI** | predicted | 3071 | 3016 **or** 3017 | 4 **or** 3 | 2 | 49 | | |
| | **measured** | **3071** | **3017** | **3** | **2** | **49** | 8206 s | 8 |

```text
ALL THREE CONFIGURATIONS MATCH THE PREDICTION, Debug+GUI on its SECOND declared branch.

BASELINE (P18.5 close)
  release_cuda  2967 selected, 2916 Passed, 0 FAILED, 2 Skipped, 49 DISABLED
  cpu_only      2744 selected, 2697 Passed, 0 FAILED, 0 Skipped, 47 DISABLED
  debug_gui     3029 selected, 2974 Passed, 4 FAILED, 2 Skipped, 49 DISABLED

DELTA MEASURED -- +42 selected in EVERY configuration, +42 Passed in Release+CUDA and CPU-only,
+43 Passed and -1 FAILED in Debug+GUI. Skipped and DISABLED did not move anywhere.

THE -1 FAILED IS THE MARGINAL TIMEOUT PASSING THIS TIME, not a P18.6 effect -- section 2.
All 42 new tests passed in every configuration.
```

## 2. The marginal timeout — a sixth data point

```text
DriverSeegmiller.MeshSequence, Debug+GUI, against the unchanged 7200 s cap:

    P18.1    PASSED   7123.03 s     98.9%
    P18.2    TIMEOUT  7201.94 s    >100%
    P18.3    PASSED   6764.75 s     94.0%
    P18.4    PASSED   7034.57 s     97.7%
    P18.5    TIMEOUT  7200.12 s    >100%
    P18.6    PASSED   7168.06 s     99.6%     <-- this run

SIX MEASUREMENTS, FOUR PASSES AND TWO TIMEOUTS, EVERY ONE WITHIN 6% OF THE CAP. This run landed at
99.6% -- 32 seconds of headroom on a two-hour test. The test is not flaky in the sense of a random
defect; it is a Debug-build run sitting on a limit, and which side it lands on depends on machine
load rather than on the code under test.

WHY THIS IS NOT ATTRIBUTED TO P18.6: the three P18.6 files are not reachable from any case file
and are not linked into any Seegmiller code path. Release+CUDA and CPU-only, which run the same
driver tests under the same frozen cap, show 0 FAILED. A P18.6 effect would not confine itself to
the one configuration where the test is already at 99.6% of its budget -- and would not IMPROVE
the count.

THE 7200 s CAP WAS NOT RAISED, and raising it is not proposed. P18.9's G10 now has six data
points and should predict a BRANCH rather than a number.
```

## 3. Every non-passing test, with its measured value

```text
Debug+GUI, 3 FAILED -- the three RELIABLE timeouts, unchanged in character:

  3005  DriverSeegmiller.CorrectedSequenceIsDeterministic      ***Timeout
  3007  DriverSeegmillerInlet.MappedInletPreservesTheMassFlow  ***Timeout
  3008  DriverSeegmillerInlet.MediumGridAB                     ***Timeout

Release+CUDA: 0 FAILED.   CPU-only: 0 FAILED.

NO NEW FAILURE, AND NO KNOWN FAILURE CHANGED ITS CHARACTER. The marginal fourth test passed this
time, which is one of the two outcomes declared before the run.
```

## 4. Regression integrity (CLAUDE.md §8)

| step | Release+CUDA | CPU-only | Debug+GUI |
|---|---|---|---|
| configure exit | 0 | 0 | 0 |
| **build options ASSERTED** | `Release/OFF/ON/ON` ✓ | `Release/OFF/OFF/ON` ✓ | `Debug/ON/ON/OFF` ✓ |
| build exit (time) | 0 | 0 (110 s) | 0 (595 s) |
| **staleness, fail-closed** | 0 stale | 0 stale | 0 stale |
| ctest exit | 0 | 0 | 8 (the 3 timeouts) |

### 4.1 A staleness concern that was raised, and resolved by measurement

```text
include/cfd/species/HeatRelease.hpp WAS EDITED AFTER release_cuda HAD BUILT AND PASSED ITS
STALENESS CHECK. The edit added a comment block naming the P15 assembler that consumes S_E -- made
because the integrity probe correctly reported itself BLIND, having grepped for a symbol the header
did not mention.

CLAUDE.md section 8 says a regression result is authoritative ONLY if the binary was demonstrably
built against the exact source being tested. A comment block does not reach codegen, but the source
HASH changed, and a hash change is what the staleness check exists to catch -- so the claim needed
PROVING rather than asserting. Recorded as OPEN at logs/08_staleness_concern_OPEN.log while the
regression was still running.

RESOLVED BY REBUILD AND HASH COMPARISON (logs/08_staleness_concern_RESOLVED.log):

    recorded by the regression at release_cuda build time
      libcfdcore.a  a82a7c3c913fafbc1d6f5f90d656fe3e08ee1fb29fa08edd14d16e1319c1aba5
    after rebuilding build/qual-final against the CURRENT source (2 translation units recompiled)
      libcfdcore.a  a82a7c3c913fafbc1d6f5f90d656fe3e08ee1fb29fa08edd14d16e1319c1aba5

IDENTICAL. The host toolchain is bit-reproducible, so this proves the comment-only edit changed no
object code and the release_cuda counts stand for the tree's final source. NO RE-RUN REQUIRED.

cpu_only and debug_gui were unaffected -- both configured and built after the edit.
```

Library hashes, recorded per configuration in `logs/P18_6_regress.log`:

```text
release_cuda  libcfdcore.a  a82a7c3c913fafbc1d6f5f90d656fe3e08ee1fb29fa08edd14d16e1319c1aba5
              libcfdcuda.a  1e2e0f914afd40ef1f624f03cdb2bbee8aa141d46d780ad1bbcc3c29c404290a

libcfdcuda.a's hash is NOT comparable across rebuilds -- nvcc is not bit-reproducible in this
toolchain. The host library is, which is what made section 4.1's test possible.
```

## 5. The CPU-only configuration, and why it is not optional

```text
src/species/HeatRelease.cpp IS IN THE MAIN SOURCE LIST, not inside the if(NOT CFDAPP_ENABLE_CUDA)
block. A file added inside that block is compiled by no routine gate -- a blind spot this project
has hit before.

MEASURED, NOT ASSUMED: the CPU-only configuration built with 0 errors and ran 2786/2739 with
0 FAILED, including all 42 P18.6 tests. The new file compiles and runs with CUDA OFF.

AND P18.6 ADDS NO GPU CODE AT ALL (section 47): CPU numerics remain authoritative, and P18.8 owns
CPU/GPU equivalence. An integrity grep confirms no cudaMalloc, __global__ or cudaMemcpy in the new
sources, with a positive control that the grep reads the file.
```

## 6. Formatting, whitespace and sanitizers

```text
clang-format-18 --dry-run --Werror, the three P18.6 files            CLEAN
clang-format-18 over include/ src/ apps/ tests/                      28 violating files, ALL
                                                                     PRE-EXISTING, 0 in any P18 file

git diff --check                                                     259 warnings in the tree, all
                                                                     in three CMakeLists no P18
                                                                     phase authored:
    cuda/CMakeLists.txt                     152
    tests/unit/gpu/CMakeLists.txt            75
    tests/unit/compressible/CMakeLists.txt   32
  in a P18.6 file                           0
  in TODO.md or ROADMAP.md                  0
  A PUSH BLOCKER recorded in TODO.md, not a P18.6 regression.

sanitizers     NOT RUN. The P18.6 criteria do not require them and CLAUDE.md section 8 places the
               sanitizer configuration in the full regression G11 settles at P18.9. Recorded as
               NOT RUN with the reason, exactly as P18.1 through P18.5 recorded it -- not as a pass.
```

## 7. results/validation

```text
50 tracked reports were rewritten by the regression. CLASSIFIED before restoring
(logs/09_validation_noise.log):
    37 distinct changed keys, EVERY ONE in the timing / iteratively-converged allow-list
    10 physics fields checked (wall_shear, u_tau, y_plus, nusselt, reattachment, cf_,
       pressure_coefficient, mass_flow, max_divergence, mass_imbalance) -- each 0 changed lines
    1074 changed lines inspected -- the positive control against a blind verdict
    restored: 50 modified before, 0 after

THE MAGNITUDE MEASUREMENT PRODUCED AN ALARMING-LOOKING NUMBER that turned out to be a denominator
artifact, and the instrument was fixed rather than the number explained away. See
logs/10_validation_noise_MAGNITUDE.log: the worst non-timing relative change, 5.609e-02, was on a
CONTINUITY RESIDUAL whose tracked value is 5.741112e-10 -- six orders of magnitude below the
solution error norms in the same file. The absolute move is ~3.2e-11. L36 is NOT contradicted.
```

## 8. What this regression does and does not establish

```text
ESTABLISHES
  - P18.6 added 42 tests, all passing in all three configurations, and moved no other count.
  - Release+CUDA and CPU-only are 0 FAILED.
  - The environment is WSL2/Ubuntu-22.04 on the recorded hardware.
  - The release_cuda counts apply to the tree's FINAL source, proven by library hash.

DOES NOT ESTABLISH
  - native Windows CUDA. WSL2 + CUDA evidence does not verify it, and none is claimed.
  - sanitizer cleanliness. NOT RUN, deferred to G11.
  - a single predictable Debug+GUI failure count. Six measurements of the marginal test now give
    4 PASS / 2 TIMEOUT, all within 6% of the cap. P18.9 should predict the branch.
  - production reachability. No case file reaches any P18.6 code.
  - anything about the compressible total-energy path, which still refuses a variable cp.
```
