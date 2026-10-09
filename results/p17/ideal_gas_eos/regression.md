# P17.2 — regression

**PASS. Every frozen prediction matched exactly, in both configurations.**

Raw: `logs/30_p17_2_regress_mandated.log`, `logs/30_release_cuda_ctest.log`,
`logs/30_cpu_only_ctest.log`. Harness: `tools/p17_2_regress.sh`.

---

## 1. Result against the frozen prediction

Predictions were the P17.1 baseline plus exactly fourteen, derived before the run: 9 new
`IdealGasEOSTest` rows and 5 new rows in the thermodynamics file, all plain unit tests with
no `DISABLED_` prefix and no name matching an exclusion.

```text
                          P17.1 baseline   PREDICTED   MEASURED   verdict
Release+CUDA  build/qual-final
  registered                    2567         2581        2581     MATCH
  matching the exclusions         13           13          13     MATCH
  disabled (unique)               49           49          49     MATCH
  executed                      2505         2519        2519     MATCH
  failed                           0            0           0     MATCH
  runtime                     43.7 min          -      38.6 min

CPU-only      build/qual-cpuonly
  registered                    2365         2379        2379     MATCH
  matching the exclusions         13           13          13     MATCH
  disabled (unique)               47           47          47     MATCH
  executed                      2305         2319        2319     MATCH
  failed                           0            0           0     MATCH
  runtime                     29.3 min          -      26.6 min
```

Both arithmetics close with nothing left over:

```text
2581 - 13 - 49 = 2519     exact
2379 - 13 - 47 = 2319     exact
```

```text
ctest exit status   0 and 0
summary lines       100% tests passed, 0 tests failed out of 2519
                    100% tests passed, 0 tests failed out of 2319
Not Run             0 and 0
```

Both configurations ran **faster** than the P17.1 baseline (38.6 vs 43.7 min; 26.6 vs 29.3
min), because P17.1's Release+CUDA run was starved for 13 minutes by a probe I had launched
without `OMP_NUM_THREADS=1`. Load here held at 8.27, exactly `-j 8`. Runtimes are WSL wall
clock and are indicative only, never performance evidence (`CLAUDE.md` §7).

Debug+GUI was not run, per the standing decision in
`results/p17/baseline/DECISION_debug_gui_scope.md`. It is `NOT RUN`, not `PASS`.

## 2. Integrity — CLAUDE.md section 8

```text
build                     exit 0 (7 s) and exit 0 (98 s)
built test binaries       46 (Release+CUDA), 45 (CPU-only)
                          the one-binary difference is the GPU test target, absent in a
                          CUDA-off build
stale vs production       0 and 0
stale vs own test source  0 and 0        <- the clause added during P17.1
external tools            1, listed: /usr/bin/cmake (2024-02-03)
cuda/libcfdcuda.a         0a64478a5d1929248ebdbc6026381346aa8697f2df26df41ce9ab5bde6b77a60
```

## 3. Fingerprints

```text
P17_CODE_FINGERPRINT
  P17.1 close         eba3d7023f8c2516884e260ca946f4440b82e2557e5dbbaa3cce32f7c6432935
  P17.2, before run   4d08c2367c1d24fca640f2c4d3ec47084cfdc6ba52788972edaf2a19d3704491
  P17.2, after run    4d08c2367c1d24fca640f2c4d3ec47084cfdc6ba52788972edaf2a19d3704491
                      UNMOVED across the regression -- running tests changed no code
```

Moved from the P17.1 close by the **authorized delta and nothing else**:

```text
tracked modified                     170 -> 171   (+1: only test_thermodynamic_properties.cpp
                                                   is newly modified; the other four P17.2
                                                   files were already modified vs HEAD by
                                                   the earlier P17.2 session)
untracked                            107 -> 107   (+0)
tracked engineering files changed     119 -> 120   (+1, same reason)
```

## 4. G6 — the P13-001 SIMPLE fingerprint

```text
probe        results/p13/restart/tools/solve_fingerprint.cpp   (unmodified)
invocation   OMP_NUM_THREADS=1 /tmp/p17_2_fp
cases        10        lines  130
diff         EMPTY -- 0 bytes, preserved as logs/10_fp_diff.txt
verdict      BITWISE IDENTICAL -- P17.2 moved no bit of the incompressible solver
```

An empty diff file is kept deliberately: the absence of a difference **is** the evidence.

`OMP_NUM_THREADS=1` is set, as it was for P17.1 and unlike the earlier P17.2's own G7 — for
a bitwise-identity claim a thread-count-dependent run is the wrong instrument, since OpenMP
reduction order can change bit patterns. It also completed in 17 s rather than stalling.

## 5. B6 — CPU-only has no CUDA dependency

```text
nvcc invocations in the CPU-only build log    0
libcudart references in that log              0
cuda/libcfdcuda.a present                     no
ldd cudart refs in the CPU-only CLI           0
```

All four agree. **And unlike the P17.1 run, the build-log evidence is strong here**: the
CPU-only build took 98 s and produced a 30,448-byte log, i.e. a genuine full compile with
zero nvcc invocations — not an incremental "no work to do" that would make the count
vacuously zero. This is the configuration that compiles files guarded by
`if(NOT CFDAPP_ENABLE_CUDA)`.

The harness printed blank for the two log-derived counts: `grep -c` exits 1 when the count
is zero, which suppressed the value inside a command substitution. Known reporting defect,
carried over from P17.1; values re-measured directly above.

## 6. Watched for, and clean

```text
incompressible SIMPLE     P13-001 fingerprint BITWISE IDENTICAL
CompressibleSIMPLE        149/149 compressible suite, incl. the MMS and both
                          production-case suites
thermal / P15             161/161 (frozen narrow regex); plus the 13/13 constant-property
                          group cited by gate group K
P14 transport             68/68
P16 turbulence            315/315
P17.1 variable density    20/20 CompressibleMassFluxTest, unchanged
EOS + thermodynamics      30/30 and 20/20
```

No failure appeared anywhere, so nothing had to be classified as related or unrelated.

## 7. What this regression does NOT cover

```text
Debug+GUI          NOT RUN -- deferred by decision to P17.11
sanitizers         NOT RUN. CLAUDE.md section 8 names them as part of the full regression;
                   P17.2 ran Release+CUDA and CPU-only only.
clang-format clean CLEAN on the five files P17.2 changed (gate G9). NOT clean tree-wide:
                   22 pre-existing files fail, none of them P17.2's, and
                   results/p13/final-qualification/ contains ZERO mentions of
                   clang-format -- so P13's own "full regression PASS" never executed that
                   component. Standing debt, first recorded in P17.1.
native Windows     WSL2 only, per CLAUDE.md section 4.
```
