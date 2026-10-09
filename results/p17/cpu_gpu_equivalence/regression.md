# P17.10 — regression

Execution policy **FROZEN**, inherited verbatim from P17.9, which inherited it from P17.8 and
ultimately from P13's G10 closeout. Harness: `tools/p17_10_regress.sh`. Raw log:
`logs/50_p17_10_regress.log`.

```text
OMP_NUM_THREADS=1 ctest -j 8 --timeout 7200
5 exclusion patterns, 13 tests, unchanged since P13's G10 closeout -- NOT widened by one entry
build/qual-final (Release+CUDA+OpenMP) and build/qual-cpuonly (CUDA OFF, Release+OpenMP)
build/qual-debug NOT run -- deferred to P17.11 by standing decision
```

The harness re-checks the frozen gate's own hash at the start and **fails closed** if it moved. It
had not: `c5b86211dce2128152f5fe8d5a93b8ba739fc596da562f485af18d3ce6efbb51`. It also re-checks both
inherited gates and lists the three P17.9 amendments.

**This is the third run.** The first two are preserved and are described in §7 — neither was a code
failure, and neither is quietly dropped.

---

## 1. The prediction, declared before the run

```text
P17.9 close:   release_cuda 2745 registered / 2683 executed
               cpu_only     2539 registered / 2479 executed

P17.10 adds THREE files and no others:
  tests/integration/compressible/test_p17_10_dryrun.cpp                 8 TESTs  (CPU, both configs)
  tests/unit/compressible/test_device_thermodynamics_equivalence.cpp    6 TESTs  (CPU, both configs)
  tests/unit/gpu/test_density_based_gpu_equivalence.cpp                17 TESTs  (CUDA ONLY)

The deltas DIFFER by configuration, because tests/unit/gpu is added only when CUDA is ON. That
asymmetry IS the prediction, not an excuse for one:

  release_cuda 2745 + 8 + 6 + 17 = 2776 registered   2683 + 31 = 2714 executed
  cpu_only     2539 + 8 + 6      = 2553 registered   2479 + 14 = 2493 executed
```

## 2. The result — every predicted count matched exactly

```text
                                    release_cuda        cpu_only
------------------------------------------------------------------------------
build                               exit 0,   2 s      exit 0,  42 s
staleness (FAIL CLOSED)             50 binaries        49 binaries
  stale vs production                 0                  0
  stale vs its own sources            0                  0
  unverifiable                        0                  0
registered                          2776  PRED 2776    2553  PRED 2553
matching the exclusions               13  PRED 13        13  PRED 13
IdealGasEOSTest rows                  30  PRED 30        30  PRED 30
P17.10 rows                           31  PRED 31        14  PRED 14
------------------------------------------------------------------------------
ctest exit status                      0                  0
EXECUTED                            2714  PRED 2714    2493  PRED 2493
passed                              100%               100%
FAILED                                 0  PRED 0          0  PRED 0
runtime                             2099 s             1449 s
COUNT VERDICT                       MATCHES            MATCHES
------------------------------------------------------------------------------
```

```text
NEW FAILURES: none.
KNOWN FAILURES: none. The 13 excluded tests are the frozen exclusion set, unchanged.
```

## 3. Registration is not execution — the named rows that ran

```text
                                 release_cuda   cpu_only
P17.10 rows Passed                    29           14      PREDICTED 31 / 14
P17.9 shock rows Passed               13           13
P17.8 DensityBased rows Passed        36           36
Hllc rows Passed (P17.7)              39           35      the 4-row difference is P17.7's GPU
                                                           equivalence suite, absent with CUDA OFF
IdealGasEOSTest rows (P17.2)          30           30      PREDICTED 30
LowMach rows (P17.6 / P3)             25           25
```

```text
THE 29 AGAINST A PREDICTED 31 IS A PREDICTION ERROR OF MINE, NOT A TEST RESULT. The two missing rows
are P17.10's own sanitizer controls:

    DensityBasedGpuSanitizerControl.N1_DeliberateOutOfBoundsRead    ***Skipped
    DensityBasedGpuSanitizerControl.N2_DeliberateSharedMemoryRace   ***Skipped

They SKIP unless CFD_P17_10_SANITIZER_CONTROL=1, which only tools/sanitizers.sh sets -- a deliberate
guard, because an out-of-bounds DEVICE read can raise a sticky error that poisons the CUDA context
for a process running ~200 GPU tests. I predicted ctest would count a skipped gtest as "Passed"; it
reports it as "Skipped" and counts it in the executed total but not in the Passed rows. 29 + 2 = 31,
and the executed count (2714) was right. The prediction was imprecise; the tests did exactly what
they were built to do.
```

## 4. CPU-only has no CUDA dependency

```text
nvcc invocations in the CPU-only build log : 0
libcudart references                       : 0
cuda/libcfdcuda.a present                  : no
```

## 5. Library hashes — the sharpest statement that no CPU production code changed

```text
release_cuda  src/libcfdcore.a   188e50397ed13718f449f369ef996768cc4011ac1048955dc312da92716390f6
cpu_only      src/libcfdcore.a   1305a82aa18ba3a5f139a08d4d165c131a7eb3c3881fcb3f454cc99acc68762a
```

**Both are IDENTICAL to the values P17.8 and P17.9 recorded.** P17.10 is an implementation phase —
it added a CUDA kernel, two headers and three test files — and the core library still comes out
byte-for-byte the archive P17.9 closed with, in both configurations.

```text
AND THE CPU-ONLY ONE IS STRONGER THAN IT LOOKS. That tree was DELETED and rebuilt FROM SCRATCH
during this phase (§7), so 1305a82a... is not an incremental build that merely was not disturbed --
it is a full rebuild from source that reproduced P17.9's archive bit for bit. Which also establishes
something this project had not recorded: the HOST toolchain here is bit-reproducible, unlike nvcc.

libcfdcuda.a is recorded per configuration but never used as an identity check: nvcc is not
bit-reproducible and its hash moves across rebuilds of identical source.
```

## 6. The code fingerprint did not move across the regression

```text
P17_CODE_FINGERPRINT before : 60cf951bde2a4eb2fc32d21470377090fa3453c6b5b435c130b2247164f89318
P17_CODE_FINGERPRINT after  : 60cf951bde2a4eb2fc32d21470377090fa3453c6b5b435c130b2247164f89318
```

```text
SCOPE OF THAT CLAIM: P17_CODE_FINGERPRINT excludes results/ and the top-level docs by construction,
so it says nothing about the tracked results/validation/** reports a full ctest run rewrites. Those
are handled in §8.

IT ALSO DIFFERS FROM P17.9's VALUE (24c617f3...), and that is expected, not a finding: P17.10 adds
production source. A fingerprint that had NOT moved would be the surprise.
```

## 7. The two earlier runs — preserved, not discarded

```text
RUN 1  logs/50_p17_10_regress_FAILED_STALENESS.log
       release_cuda PASSED 2714/2714. cpu_only FAILED CLOSED on staleness before running any test:
       47 of 49 binaries "stale vs production", while "stale vs its own sources" was 0 over 286
       objects. The newest production sources were cuda/CMakeLists.txt, DeviceDensityBasedSolver.hpp
       and DeviceDensityBasedKernel.cu -- ALL CUDA-ONLY, compiled by no CPU-only target. A false
       positive of a conservative global rule. Diagnosed in STALENESS_FAILURE.md. Resolved by
       deleting and rebuilding the tree, NOT by touching the check.

RUN 2  logs/50_p17_10_regress_INVALID_DEBUG_CPUONLY.log
       release_cuda PASSED 2714/2714. cpu_only reported 3 FAILURES -- and they were not failures.
       Deleting the tree destroyed its configuration, which lived only as CMake cache state; the
       harness configures with a bare `cmake -S . -B <dir>`, which on a fresh tree takes the PROJECT
       DEFAULTS: CMAKE_BUILD_TYPE=Debug and CFDAPP_ENABLE_OPENMP=OFF. Three DriverSeegmiller tests
       that take 1390-1690 s in Release hit the frozen 7200 s timeout in Debug.

       Settled by hash, not by argument: reconfigured to Release+OpenMP and rebuilt from scratch,
       libcfdcore.a came out 1305a82a... -- identical to P17.9's -- and the two trees' option sets
       now differ in exactly CFDAPP_ENABLE_CUDA. Written up in HARNESS_FINDINGS.md §2.

RELEASE_CUDA PASSED 2714/2714 IN ALL THREE RUNS, at 2293 s, 2271 s and 2099 s.
```

## 8. Runtime-noise audit (CLAUDE.md §7)

```text
tracked results/validation/** files rewritten by this regression : 50
classification : TIMING ONLY, by tools/md_column_audit.py, which FAILS CLOSED on anything else

    changed markdown table cells : 51, every one under a 'runtime (s)' header
    changed JSON lines           : 168 -- 94 "seconds", 74 "runtime_seconds"
    controls: 'wall_shear_stress', 'status', 'iterations' and 'accepted' are each confirmed to be
              REJECTED as non-timing, so the verdict is not a classifier that accepts everything

action : restored with `git checkout -- results/validation`
after  : 0 files modified under results/validation
```

```text
THE CLASSIFIER WAS WRONG THREE TIMES BEFORE IT WAS RIGHT, and each wrong version gave a confident
answer. Recorded in the tool's own docstring rather than smoothed over:

  v1  matched the CHANGED COLUMN INDICES (5, 7, 9) against the indices P17.9 recorded and called it
      agreement. It is not: index 5 is 'iterations' in ten files and 'runtime (s)' in two.
  v2  never flushed pending rows at a FILE boundary, so markdown rows from a .md file were
      attributed to the next .json -- it reported 47 "non-timing" changes in files whose diffs
      contain no pipe characters at all.
  v3  used each file's FIRST table header. results/validation/production/poiseuille.md holds TEN
      tables in SIX widths; index 5 is 'runtime (s)' in the 9-cell ones and 'status' in the 12-cell
      first one. It reported 6 runtime changes as changes to 'status'.
  v4  resolves the NEAREST PRECEDING header of matching width, located by the hunk's line numbers.

P17.9's audit used the index method (v1). Its conclusion was almost certainly right, but its
INSTRUMENT could not have distinguished a runtime change from an iterations change, and that is
worth knowing.
```

## 9. What this regression does NOT cover

```text
Debug+GUI          NOT RUN -- deferred by standing decision to P17.11, as every P17 phase since
                   P17.0 has recorded. Not skipped for runtime.
sanitizers (host)  NOT RUN. CUDA sanitizers WERE run and are in equivalence.md §8.
clang-format       CLEAN on the files P17.10 added. NOT clean tree-wide: pre-existing debt first
                   recorded in P17.1.
49 DISABLED tests  NOT RUN, deliberately, and the harness's "Not Run : 0" line does not say so --
                   see HARNESS_FINDINGS.md §1. None of the 49 is a P17 test.
native Windows     WSL2 Ubuntu-22.04 only.
```

## 10. Environment actually used

```text
host        Intel i9-14900HX, 64 GB, NVIDIA RTX 5000 Ada Laptop (CC 8.9), Windows 11 Pro
linux       WSL2, Ubuntu-22.04    nproc 32
build jobs  16      ctest -j 8, OMP_NUM_THREADS=1, --timeout 7200
nvcc        release 12.9, V12.9.86, from /usr/local/cuda-12.9/bin
sanitizer   compute-sanitizer 2025.2.1.0
timings     WSL wall clock, recorded as indicative only
```
