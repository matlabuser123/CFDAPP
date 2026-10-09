# P17.1 — regression

**PASS. Every frozen prediction matched exactly, in both configurations.**

Raw: `logs/20_p17_1_regress_mandated.log`, `logs/20_release_cuda_ctest.log`,
`logs/20_cpu_only_ctest.log`. Harness: `tools/p17_1_regress.sh`.

---

## 1. Result against the frozen prediction

Predictions were the P17.0 baseline plus exactly eight, derived before the run: the
eight new tests are plain unit tests with no `DISABLED_` prefix and no name matching an
exclusion, so every count had to rise by exactly eight and nothing else could move.

```text
                          P17.0 baseline   PREDICTED   MEASURED   verdict
Release+CUDA  build/qual-final
  registered                    2559         2567        2567     MATCH
  matching the exclusions         13           13          13     MATCH
  disabled (unique)               49           49          49     MATCH
  executed                      2497         2505        2505     MATCH
  failed                           0            0           0     MATCH
  IdealGasEOSTest rows Passed     21           21          21     MATCH
  runtime                     41.6 min          -      43.7 min    (see section 4)

CPU-only      build/qual-cpuonly
  registered                    2357         2365        2365     MATCH
  matching the exclusions         13           13          13     MATCH
  disabled (unique)               47           47          47     MATCH
  executed                      2297         2305        2305     MATCH
  failed                           0            0           0     MATCH
  IdealGasEOSTest rows Passed     21           21          21     MATCH
  runtime                     28.7 min          -      29.3 min
```

Both arithmetics close with nothing left over:

```text
2567 - 13 - 49 = 2505     exact
2365 - 13 - 47 = 2305     exact
```

```text
ctest exit status   0 and 0
summary lines       100% tests passed, 0 tests failed out of 2505
                    100% tests passed, 0 tests failed out of 2305
Not Run             0 and 0
```

Debug+GUI was not run, per the standing decision recorded in
`results/p17/baseline/DECISION_debug_gui_scope.md`. It is `NOT RUN`, not `PASS`.

## 2. Integrity — CLAUDE.md section 8

```text
build                    exit 0 (2 s) and exit 0 (5 s) -- incremental, both current
newest PRODUCTION source src/compressible/CompressibleMassFlux.cpp
built test binaries      46 (Release+CUDA), 45 (CPU-only)
                         the one-binary difference is the GPU test target, absent in a
                         CUDA-off build -- a signal the two registries are genuinely
                         different trees
stale vs production      0 and 0
stale vs own test source 0 and 0        <- a NEW clause, see section 5
external tools           1, listed: /usr/bin/cmake (2024-02-03), invoked by script-driven
                         CLI tests; not a product of this build, so not staleness-checked,
                         and recorded rather than silently excluded
cuda/libcfdcuda.a        0a64478a5d1929248ebdbc6026381346aa8697f2df26df41ce9ab5bde6b77a60
```

## 3. Fingerprints

```text
P17_CODE_FINGERPRINT
  P17.0 baseline      1e307fcd7c70104d02da8adfb226dfd5641a6c8e880fc8169ce43b6ca070c5d2
  P17.1, before run   eba3d7023f8c2516884e260ca946f4440b82e2557e5dbbaa3cce32f7c6432935
  P17.1, after run    eba3d7023f8c2516884e260ca946f4440b82e2557e5dbbaa3cce32f7c6432935
                      UNMOVED across the regression -- running tests changed no code (B8)

SOURCE_FINGERPRINT_V2
  P17.0 freeze        fbf786656eac2284956a4e29fe2745157a3e59ab339833a9f49ff6e048bd9fd6
  P17.1               8a179e5bbb15e1f4a6b83ea725440af7d18da25d61dd4b48384dbe8086935621

Both moved from the P17.0 baseline by the AUTHORIZED DELTA and nothing else:
  tracked engineering files changed  116 -> 119   (+3, exactly the P17.1 files)
  untracked engineering+output       122 -> 122   (+0)
  untracked component sha256         069c034c… -> 069c034c…   BYTE-IDENTICAL
```

## 4. Runtime, and an honest note on it

```text
Release+CUDA  43.7 min against a 41.6 min baseline
```

The 2.1-minute overrun is **my fault, not the tree's**: I launched the SIMPLE fingerprint
probe concurrently without `OMP_NUM_THREADS=1`, and it held roughly 25 cores for the
first ~13 minutes of this run (load peaked at 39.8 on a 32-core machine). It was not
doing 25 cores of work — oversubscribed OpenMP threads spin rather than progress; the
same probe later completed in **12 seconds** single-threaded and uncontended.

Runtimes here are WSL wall clock and are recorded as indicative only, never as
performance evidence (`CLAUDE.md` §7). No test result can be affected by contention:
`OMP_NUM_THREADS=1` was set for both ctest runs, and no test came near its 7200 s ceiling.

## 5. Two harness corrections, both preserved

```text
logs/PRESERVED_invalid_crossdir_staleness/    attempt 1 -- WHY_INVALID.md
```

Attempt 1 failed closed with "45 stale binaries" in Release+CUDA. Diagnosis: the check
required every binary to postdate the newest source **anywhere** in the tree, and
`clang-format -i` had just made a test file the newest source — so 45 binaries that do
not compile that file were flagged. Measured mtimes showed the property §8 actually
requires held for every binary.

The correction is better targeted **and adds a clause the original lacked**:

```text
before   every binary newer than the newest file under include src cuda apps tests
         benchmarks                              -- defeated by touching any test file
after    (1) every binary newer than the newest PRODUCTION source (include src cuda),
             the real hazard since libcfdcore.a links into all of them
         (2) AND each binary newer than the test sources in ITS OWN directory  <- NEW
```

Clause (2) catches a case (1) cannot: a test file edited without its own binary rebuilt.
`FAIL CLOSED` is retained verbatim. This is a mis-specified proxy corrected, not a
criterion weakened — and it is the third defect in this family found by the check failing
closed rather than by a wrong number passing through.

## 6. A reporting defect, and what it means for B6

The harness printed **blank** for two of B6's four sub-checks. Cause: `grep -c` exits 1
when the count is zero, which suppressed the value inside a command substitution. Values
re-measured directly:

```text
nvcc invocations in the CPU-only build log    0
libcudart references in that log             0
cuda/libcfdcuda.a present                    no
ldd cudart refs in the CPU-only CLI          0
```

All four agree that this configuration neither compiles nor links anything CUDA.

**But two of them are weak evidence in this run, and that is worth saying.** The CPU-only
build was incremental and its log is 22 bytes — "no work to do". So "0 nvcc invocations"
is 0 because nothing was compiled, not because a compile avoided nvcc. The
artifact-derived checks (`libcfdcuda.a` absent, `ldd cudart` = 0) are properties of the
built binaries and are unaffected by incrementality, so they carry the claim. The strong
build-log version of this evidence is in the P17.0 baseline, whose CPU-only build log is
~30 KB from a full compile with 0 nvcc invocations.

## 7. Watched for, and clean

```text
incompressible SIMPLE     P13-001 fingerprint BITWISE IDENTICAL, 10 cases / 130 lines,
                          empty diff preserved      logs/08_fingerprint.log
CompressibleSIMPLE        137/137 compressible suite, including the MMS and both
                          production-case suites
thermal                   P15 161/161 (frozen narrow regex)
P14 transport             68/68
P16 turbulence            315/315
mass-flux calculations    20/20, including the 8 new
EOS tests                 21 + 7, all Passed in both configurations
```

No failure appeared anywhere, so nothing had to be classified as related or unrelated.

## 8. What this regression does NOT cover

```text
Debug+GUI          NOT RUN -- deferred by decision to P17.11
sanitizers         NOT RUN. CLAUDE.md section 8 names them as part of the full
                   regression; P17.1 ran Release+CUDA and CPU-only only.
clang-format clean NOT CLEAN tree-wide -- 22 pre-existing files fail, none of them
                   P17.1's, and results/p13/final-qualification/ contains ZERO mentions
                   of clang-format, so P13's own "full regression PASS" never executed
                   this component. Recorded as technical debt in implementation.md
                   section 6; deliberately not fixed inside P17.1.
native Windows     WSL2 only, per CLAUDE.md section 4.
```
