# P17.9 — regression

Execution policy **FROZEN**, inherited verbatim from P17.8, which inherited it from P13's G10
closeout. Harness: `tools/p17_9_regress.sh`. Raw log: `logs/50_p17_9_regress.log`.

```text
OMP_NUM_THREADS=1 ctest -j 8 --timeout 7200
5 exclusion patterns, 13 tests, unchanged since P13's G10 closeout
build/qual-final (Release+CUDA+OpenMP) and build/qual-cpuonly (CUDA OFF)
build/qual-debug NOT run -- deferred to P17.11 by standing decision
```

The harness re-checks the frozen gate's own hash at the start and **fails closed** if it has moved.
It had not: `b7185ceac2c0a79c103679218c14c1a17b70b7fc6acca626807dc2ade0910f40`. The three amendments
live in `amendments/` and edit that file not at all.

---

## 1. The prediction, declared before the run

```text
P17.8 close:   release_cuda 2732 registered / 2670 executed
               cpu_only     2526 registered / 2466 executed

P17.9 adds 13 tests and no others:
  tests/integration/compressible/test_shock_validation.cpp   13 TEST macros

  release_cuda 2732 + 13 = 2745 registered    2670 + 13 = 2683 executed
  cpu_only     2526 + 13 = 2539 registered    2466 + 13 = 2479 executed
```

## 2. The result — every predicted number matched exactly

```text
                                    release_cuda        cpu_only
------------------------------------------------------------------------------
build                               exit 0, 52 s        exit 0, 60 s
staleness (FAIL CLOSED)             49 binaries         48 binaries
  stale vs production                 0                   0
  stale vs its own sources            0                   0
  unverifiable                        0                   0
registered                          2745  PRED 2745     2539  PRED 2539
matching the exclusions               13  PRED 13         13  PRED 13
IdealGasEOSTest rows                  30  PRED 30         30  PRED 30
P17.9 shock rows                      13  PRED 13         13  PRED 13
------------------------------------------------------------------------------
ctest exit status                      0                   0
EXECUTED                            2683  PRED 2683     2479  PRED 2479
passed                              100%                100%
FAILED                                 0  PRED 0           0  PRED 0
Not Run                                0                   0
runtime                             2239 s (37.3 min)   1699 s (28.3 min)
COUNT VERDICT                       MATCHES             MATCHES
------------------------------------------------------------------------------
```

```text
NEW FAILURES: none.
KNOWN FAILURES: none. The 13 excluded tests are the frozen exclusion set, unchanged and not
widened by one entry.
```

## 3. Registration is not execution — the named rows that ran

```text
                                 release_cuda   cpu_only
P17.9 shock rows Passed               17*           17*    PREDICTED 13
Hllc rows Passed (P17.7)              39            35     the 4-row difference is the GPU
                                                           equivalence suite, absent with CUDA OFF
IdealGasEOSTest rows Passed (P17.2)   30            30     PREDICTED 30
LowMach rows Passed (P17.6 / P3)      25            25
```

```text
* THE 17 IS A REPORTING-PATTERN IMPRECISION, NOT 4 EXTRA TESTS, and it is recorded rather than
  quietly corrected. The harness's REPORTING grep uses a bare `Contact`, which also matches P17.7's
  HllcContactTest.T1..T4 in test_hllc_flux.cpp. The REGISTRATION grep uses `ContactDiscontinuity`
  and correctly returns 13. Enumerated from the log, the 17 are:
     13  P17.9   SodShockTubeTest x5, ShockDetectorTest x2, NormalShockTest, ContactDiscontinuity-
                 Test, CompressionExpansionTest, SupersonicAdvectionTest, ShockConservationTest,
                 ShockDeterminismTest
      4  P17.7   HllcContactTest.T1_StationaryContactCarriesNoMassOrEnergyBitwise,
                 T2_MovingContactMatchesTheReference, T3_TangentialShearCarriesNoMass...,
                 T4_NonVacuity_DeletingTheContactWaveBreaksTheContact
  The script was NOT edited mid-run to fix this; the count is corrected here in evidence instead.
```

Finding those four rows was useful rather than merely tidy: **P17.7's T4 already proved that
deleting the contact wave breaks the FLUX**, which is exactly why the solver-level gap amendment A1
closes was not obvious. See `amendments/A1_stationary_contact_width.md` §3.1.

## 4. CPU-only has no CUDA dependency

```text
nvcc invocations in the CPU-only build log : 0
libcudart references                       : 0
cuda/libcfdcuda.a present                  : no
```

## 5. Library hashes (CLAUDE.md §8 step 4)

```text
release_cuda  src/libcfdcore.a   188e50397ed13718f449f369ef996768cc4011ac1048955dc312da92716390f6
cpu_only      src/libcfdcore.a   1305a82aa18ba3a5f139a08d4d165c131a7eb3c3881fcb3f454cc99acc68762a
```

Both are IDENTICAL to the values P17.8's regression recorded, which is the sharpest possible
statement that **P17.9 changed no production code**: the core library is byte-for-byte the same
archive P17.8 closed with.

## 6. The code fingerprint did not move across the regression

```text
P17_CODE_FINGERPRINT before : 24c617f33336cca09c83c3065556ead7bdefbfc8bf198b82899989057f20865f
P17_CODE_FINGERPRINT after  : 24c617f33336cca09c83c3065556ead7bdefbfc8bf198b82899989057f20865f
```

```text
SCOPE OF THAT CLAIM: P17_CODE_FINGERPRINT excludes results/ and the top-level docs by
construction, so it says nothing about the tracked results/validation/** reports a full ctest run
rewrites. Those are handled separately in §8.
```

## 7. What this regression does NOT cover

```text
Debug+GUI          NOT RUN -- deferred by decision to P17.11, the same standing decision every P17
                   phase since P17.0 has recorded. It is not skipped for runtime: it is not part of
                   the current P17 qualification baseline.
sanitizers         NOT RUN. CLAUDE.md section 8 names them as part of the full regression; P17.9
                   ran Release+CUDA and CPU-only only, as P17.1 through P17.8 each did.
clang-format       CLEAN on both files P17.9 added. NOT clean tree-wide: 22 pre-existing files
                   fail, none of them P17.9's. Standing debt, first recorded in P17.1.
GPU equivalence    G8 NOT APPLICABLE -- P17.8 implemented no GPU density-based solver, so §32's
                   "where implemented" is not met. P17.7's FLUX-layer equivalence re-ran (4 GPU
                   rows in the release_cuda configuration) and is not re-claimed here.
native Windows     WSL2 Ubuntu-22.04 only.
```

## 8. Runtime-noise audit (CLAUDE.md §7)

```text
tracked results/validation/** files rewritten by this regression : 50
classification : TIMING ONLY, by the same column-by-column check P17.8 used --
    JSON: the only keys that changed are "runtime_seconds" and "seconds"
    MD:   the only column that changed is the runtime column of each table, verified against each
          table's own header (indices 5, 7 and 9 for the three table shapes)
action : restored with `git checkout -- results/validation`
after  : 0 files modified under results/validation
```

## 9. Environment actually used

```text
host        Intel i9-14900HX, 64 GB, NVIDIA RTX 5000 Ada Laptop (CC 8.9), Windows 11 Pro
linux       WSL2, Ubuntu-22.04    nproc 32
build jobs  16      ctest -j 8, OMP_NUM_THREADS=1, --timeout 7200
nvcc        release 12.9, V12.9.86, from /usr/local/cuda-12.9/bin
timings     WSL wall clock, recorded as indicative only
```
