# P18.5 — regression (§47)

```text
HARNESS   tools/p18_5_regress.sh -- P18.0's baseline harness, reused with only the output directory
          and the predictions changed. DISABLED tests are COUNTED and the four build options are
          ASSERTED after configure, failing closed on a mismatch.
POLICY    OMP_NUM_THREADS=1 · ctest -j 8 · --timeout 7200 · the same 5 frozen exclusions.
          THE TIMEOUT WAS NOT RAISED.
LOG       logs/P18_5_regress.log, plus per-configuration configure/build/ctest/staleness logs
RUN       2026-10-01T01:13Z to 04:48:59Z
```

---

## 1. Results against the prediction declared before the run

| Configuration | | selected | Passed | FAILED | Skipped | DISABLED | runtime | exit |
|---|---|---:|---:|---:|---:|---:|---:|---:|
| **Release+CUDA** | predicted | 2967 | 2916 | 0 | 2 | 49 | | |
| | **measured** | **2967** | **2916** | **0** | **2** | **49** | 2293 s | 0 |
| **CPU-only** | predicted | 2744 | 2697 | 0 | 0 | 47 | | |
| | **measured** | **2744** | **2697** | **0** | **0** | **47** | 1525 s | 0 |
| **Debug+GUI** | predicted | 3029 | 2975 **or** 2974 | 3 **or** 4 | 2 | 49 | | |
| | **measured** | **3029** | **2974** | **4** | **2** | **49** | 8617 s | 8 |

```text
ALL THREE CONFIGURATIONS MATCH THE PREDICTION, Debug+GUI on its SECOND declared branch.

BASELINE (P18.4 close)
  release_cuda  2929 selected, 2878 Passed, 0 FAILED, 2 Skipped, 49 DISABLED
  cpu_only      2706 selected, 2659 Passed, 0 FAILED, 0 Skipped, 47 DISABLED
  debug_gui     2991 selected, 2937 Passed, 3 FAILED, 2 Skipped, 49 DISABLED

DELTA MEASURED -- +38 selected in EVERY configuration, +38 Passed in Release+CUDA and CPU-only,
+37 Passed in Debug+GUI with +1 FAILED. Skipped and DISABLED did not move anywhere.

THE +37/+1 IS THE MARGINAL TIMEOUT, NOT A P18.5 EFFECT -- see section 2. All 38 new tests passed in
every configuration; logs/03_preservation.log and the focused run both show 38/38.
```

## 2. The marginal timeout — a fifth data point

```text
DriverSeegmiller.MeshSequence, Debug+GUI, against the unchanged 7200 s cap:

    P18.1    PASSED   7123.03 s     98.9%
    P18.2    TIMEOUT  7201.94 s    >100%
    P18.3    PASSED   6764.75 s     94.0%
    P18.4    PASSED   7034.57 s     97.7%
    P18.5    TIMEOUT  7200.12 s    >100%     <-- this run

FIVE MEASUREMENTS, THREE PASSES AND TWO TIMEOUTS, ALL WITHIN 6% OF THE CAP. The test is not
flaky in the sense of a random defect; it is a 2-hour Debug run sitting on a 2-hour limit, and
which side it lands on depends on machine load, not on the code under test.

WHY THIS IS NOT ATTRIBUTED TO P18.5. The three P18.5 files are not reachable from any case file
(reaction_integration.md section 4) and are not linked into any Seegmiller code path. The CPU-only
and Release+CUDA configurations, which run the same driver tests under the same frozen cap, show
0 FAILED. A P18.5 effect would not confine itself to the one configuration where the test is
already at 100.0% of its budget.

THE 7200 s CAP WAS NOT RAISED, and raising it is not proposed here. P18.9's G10 is where the
Debug+GUI failure count is settled, and it now has five data points instead of four.
```

## 3. Every non-passing test, with its measured value

```text
Debug+GUI, 4 FAILED -- the 3 reliable timeouts plus the marginal one:

  2958  DriverSeegmiller.MeshSequence                      ***Timeout  7200.12 sec   MARGINAL
  2963  DriverSeegmiller.CorrectedSequenceIsDeterministic  ***Timeout  7200.14 sec   known
  2965  DriverSeegmillerInlet.MappedInletPreservesTheMassFlow  ***Timeout  7200.16 sec   known
  2966  DriverSeegmillerInlet.MediumGridAB                 ***Timeout  7200.18 sec   known

AND THE FOUR THAT DID FINISH, for context on how close the budget is:
  2962  DriverSeegmiller.IsDeterministic                        Passed  1750.86 sec
  2959  DriverSeegmiller.WallDistributionsHaveSeparatedFlowStructure  Passed  2874.83 sec
  2960  DriverSeegmiller.MechanismControlsStillDiscriminate     Passed  3793.18 sec
  2964  DriverSeegmillerInlet.PrecursorReproducesTheReferenceInflowState  Passed  5788.52 sec

NO NEW FAILURE, AND NO KNOWN FAILURE CHANGED ITS CHARACTER. A "known" failure stays known only
while it reproduces its recorded behaviour; all four are the same tests timing out at the same cap.
Release+CUDA: 0 FAILED. CPU-only: 0 FAILED.
```

## 4. Regression integrity (CLAUDE.md §8)

| step | Release+CUDA | CPU-only | Debug+GUI |
|---|---|---|---|
| configure exit | 0 | 0 | 0 |
| **build options ASSERTED** | `Release/OFF/ON/ON` ✓ | `Release/OFF/OFF/ON` ✓ | `Debug/ON/ON/OFF` ✓ |
| build exit (time) | 0 (60 s) | 0 | 0 (273 s) |
| **staleness, fail-closed** | 0 stale of 50 binaries | 0 stale | 0 stale |
| ctest exit | 0 | 0 | 8 (the 4 timeouts) |

```text
THE STALENESS CHECK IS THE ONE THAT MAKES THE COUNTS AUTHORITATIVE. It uses CMake's own per-target
object lists, so it verifies every test binary postdates the newest production source AND every
source it actually compiles -- not a directory-glob heuristic. All three configurations reported
"stale vs production 0, stale vs its own sources 0, unverifiable (fail closed) 0".

THE CONFIG ASSERT MATTERS FOR A DIFFERENT REASON: build/qual-* carry their configuration in the
CMake cache only. A fresh tree silently configures Debug with no OpenMP, so a regression run
against a recreated directory would measure the wrong thing while reporting three configurations.
The harness asserts all four options after configure and fails closed.
```

Library hashes, recorded per configuration in `logs/P18_5_regress.log`:

```text
release_cuda  libcfdcore.a  ff81bc68061e3b879a07eb0e9ce45a9846f25fc4a0176b90b56a786355625735
              libcfdcuda.a  1e2e0f914afd40ef1f624f03cdb2bbee8aa141d46d780ad1bbcc3c29c404290a

libcfdcuda.a's hash is NOT comparable across rebuilds -- nvcc is not bit-reproducible in this
toolchain. The host library is. Recorded for the record, not used as an equivalence claim.
```

## 5. The CPU-only configuration, and why it is not optional

```text
src/species/ArrheniusKinetics.cpp IS IN THE MAIN SOURCE LIST, not inside the
if(NOT CFDAPP_ENABLE_CUDA) block. A file added inside that block is compiled by no routine gate,
which is a blind spot this project has hit before.

MEASURED, NOT ASSUMED: the CPU-only configuration built 0 errors and ran 2744/2697 with 0 FAILED,
including all 38 P18.5 tests. The new file therefore compiles and runs with CUDA OFF.
```

## 6. Formatting and sanitizers

```text
clang-format-18 --dry-run --Werror over include/ src/ apps/ tests/   (logs/10_format.log)
    721 files checked
    28 violating files -- ALL PRE-EXISTING, none in any P18 file
    P18.5 files among them        : 0
    P18.1-P18.4 files among them  : 0
The three P18.5 files are individually CLEAN (logs/08_final_integrity.log).

git diff --check                                                     (logs/08_final_integrity.log)
    259 warnings in the tree, all in three CMakeLists no P18 phase authored:
        cuda/CMakeLists.txt                     152
        tests/unit/gpu/CMakeLists.txt            75
        tests/unit/compressible/CMakeLists.txt   32
    A PUSH BLOCKER recorded in TODO.md, not a P18.5 regression.
    P18.5 FIXED one warning it found in TODO.md (trailing whitespace, left by an earlier phase),
    taking the tree from 260 back to 259.
    In a P18.5 file: 0.  In TODO.md or ROADMAP.md: 0.

sanitizers     NOT RUN. The P18.5 criteria do not require them and CLAUDE.md section 8 places the
               sanitizer configuration in the full regression G11 settles at P18.9. Recorded as
               NOT RUN with the reason, exactly as P18.1 through P18.4 recorded it -- not as a pass.
```

## 7. What this regression does and does not establish

```text
ESTABLISHES   P18.5 added 38 tests, all passing in all three configurations, and moved no other
              count anywhere. Release+CUDA and CPU-only are 0 FAILED. The environment is
              WSL2/Ubuntu-22.04 on the recorded hardware.

DOES NOT ESTABLISH
  - native Windows CUDA. WSL2 + CUDA evidence does not verify it, and none is claimed.
  - sanitizer cleanliness. NOT RUN, deferred to G11.
  - a single predictable Debug+GUI failure count. The inherited set is 3 reliable timeouts plus
    one marginal test now measured five times, 3 PASS / 2 TIMEOUT. P18.9 cannot predict a single
    number from this, and should predict the branch.
  - production reachability. No case file reaches any P18.5 code; see reaction_integration.md.
```
