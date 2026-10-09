# P17.0 — baseline CUDA regression: Release+CUDA

**PASS — 2497 / 2497, 0 failed. Every frozen prediction matched exactly.**

Run 2026-09-26. Gate: `baseline_gate.md`, frozen before this ran,
sha256 `a147f79c4dc2aac21bae6a39f5afa135cf15921545c32c4537938c3f08c7dfbd`.

---

## Configuration and invocation

```text
tree          build/qual-final
              CMAKE_BUILD_TYPE=Release  CFDAPP_ENABLE_CUDA=ON
              CFDAPP_BUILD_GUI=OFF      CFDAPP_ENABLE_OPENMP=ON
nvcc          /usr/local/cuda-12.9/bin/nvcc  12.9 V12.9.86   (NOT /usr/bin/nvcc 11.5)
GPU           RTX 5000 Ada Generation Laptop, compute 8.9, driver 580.97
invocation    OMP_NUM_THREADS=1 ctest -j 8 --timeout 7200 -E <5 frozen exclusions>
exclusions    the same 5 patterns / 13 tests as P13 G10, verbatim, not widened
```

## Result against the frozen prediction

```text
quantity                     PREDICTED   MEASURED   verdict
------------------------------------------------------------
registered                     2559        2559     MATCH
matching the exclusions          13          13     MATCH
disabled (unique)                49          49     MATCH
executed                       2497        2497     MATCH
failed                            0           0     MATCH
IdealGasEOSTest rows             21          21     MATCH
IdealGasEOSTest rows Passed      21          21     MATCH
```

```text
ctest exit status   0
summary line        100% tests passed, 0 tests failed out of 2497
Not Run             0
runtime             2491.77 s of test time; 2494 s wall (41.6 min)
                    [WSL wall clock -- indicative, not performance evidence]
```

The arithmetic closes with nothing left over:

```text
2559 registered  -  13 excluded  -  49 disabled  =  2497 executed     exact
```

That is the point of having frozen the numbers first. A run that merely reported
"PASS" would have been consistent with a stale binary silently omitting P17.2's nine
tests; landing on 2497 with 21 EOS rows executed by name is not.

## Why the prediction was falsifiable

P13 G10 measured 2488 executed on this configuration. P17.2 added 9 `TEST` cases to
`tests/unit/compressible/test_ideal_gas_eos.cpp` — plain unit tests, no CUDA, no GUI,
no `DISABLED_` prefix, no name matching an exclusion. So `2488 + 9 = 2497` was
predicted before the run, and a result of 2488 would have proven the binary was stale.

The 21 rows decompose exactly as predicted, 12 pre-existing plus 9 new:

```text
P17.2's nine, each Passed:
  DensityToPressureRoundTripRecoversPressure      (gate R2)
  PressureToDensityRoundTripRecoversDensity       (gate R1)
  PressureMatchesRhoRT                            (gate R4)
  PressureScalesLinearlyInDensityAndTemperature
  PressureIsReachableThroughTheAbstraction         on EquationOfState&, not the
                                                   concrete class
  PressureRejectsNonPositiveDensityOrTemperature  (gate V1/V2)
  PressureRejectsNonFiniteDensityOrTemperature    (gate V1/V2)
  PressureRejectsAnOverflowingProduct             (gate V3)
  PressureIsDeterministic                         (gate V4)

pre-existing twelve, each Passed:
  DDensityDPressureIsIndependentOfPressure    DDensityDPressureMatchesAnalyticalFormula
  DDensityDTemperatureIsNegative              DDensityDTemperatureMatchesAnalyticalFormula
  DoublingPressureAtFixedTemperatureDoublesDensity
  DoublingTemperatureAtFixedPressureHalvesDensity
  MatchesKnownStandardAtmosphereState         PressureCorrectionGivesExpectedDensityCorrection
  RejectsNonFiniteGasConstant                 RejectsNonFinitePressureOrTemperature
  RejectsNonPositiveGasConstant               RejectsNonPositivePressureOrTemperature
```

## Integrity — CLAUDE.md §8

```text
build                    exit 0 (6 s); build/qual-final was already current from P17.2
newest production source 2026-09-26T03:44:26Z  src/compressible/IdealGasEOS.cpp
built test binaries      46, discovered from ctest's OWN registry (command[0] of every
                         registered test), not from find(1)
oldest built binary      2026-09-26T03:44:59Z  tests/unit/core/CFDCoreTests
stale built binaries     0        -> all 46 are newer than the newest source
external tools           1, listed explicitly: /usr/bin/cmake (2024-02-03), which some
                         CLI rejection tests invoke via cmake -P. Not staleness-checked
                         because it is not a product of this build; recorded rather
                         than silently excluded.
cuda/libcfdcuda.a        0a64478a5d1929248ebdbc6026381346aa8697f2df26df41ce9ab5bde6b77a60
```

Binary discovery was the subject of two preserved harness failures before this run;
see `logs/PRESERVED_invalid_binary_discovery/` and
`logs/PRESERVED_invalid_system_binary/`. Both failed closed without executing a test.
The fix made the check stricter, not looser.

## Fingerprints across the run — B8

```text
P17_CODE_FINGERPRINT before  1e307fcd7c70104d02da8adfb226dfd5641a6c8e880fc8169ce43b6ca070c5d2
P17_CODE_FINGERPRINT after   1e307fcd7c70104d02da8adfb226dfd5641a6c8e880fc8169ce43b6ca070c5d2
                             UNMOVED -- running tests changed no code
SOURCE_FINGERPRINT_V2        fbf786656eac2284956a4e29fe2745157a3e59ab339833a9f49ff6e048bd9fd6
                             also unmoved: the concurrent ROADMAP.md work did not
                             continue during the run
```

## Logs

```text
driver   logs/10_baseline_regress_mandated.log
build    logs/10_release_cuda_build.log
ctest    logs/10_release_cuda_ctest.log
harness  tools/p17_baseline_regress.sh
```
