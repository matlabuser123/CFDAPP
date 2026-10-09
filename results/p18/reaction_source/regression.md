# P18.4 — regression (§49)

```text
HARNESS   tools/p18_4_regress.sh -- P18.0's baseline harness, reused with only the output directory
          and the predictions changed. DISABLED tests are COUNTED and the four build options are
          ASSERTED after configure, failing closed on a mismatch.
POLICY    OMP_NUM_THREADS=1 · ctest -j 8 · --timeout 7200 · the same 5 frozen exclusions.
          The timeout was NOT raised.
LOG       logs/P18_4_regress.log
```

---

## 1. Results against the prediction declared before the run

| Configuration | | selected | Passed | FAILED | Skipped | DISABLED | runtime | exit |
|---|---|---:|---:|---:|---:|---:|---:|---:|
| **Release+CUDA** | predicted | 2929 | 2878 | 0 | 2 | 49 | | |
| | **measured** | **2929** | **2878** | **0** | **2** | **49** | 2057 s | 0 |
| **CPU-only** | predicted | 2706 | 2659 | 0 | 0 | 47 | | |
| | **measured** | **2706** | **2659** | **0** | **0** | **47** | 1500 s | 0 |
| **Debug+GUI** | predicted | 2991 | 2937 **or** 2936 | 3 **or** 4 | 2 | 49 | | |
| | **measured** | **2991** | **2937** | **3** | **2** | **49** | 8131 s | 8 |

```text
ALL THREE CONFIGURATIONS MATCH THE PREDICTION EXACTLY, including the Debug+GUI branch.

BASELINE (P18.3 close)
  release_cuda  2891 selected, 2840 Passed, 0 FAILED, 2 Skipped, 49 DISABLED
  cpu_only      2668 selected, 2621 Passed, 0 FAILED, 0 Skipped, 47 DISABLED
  debug_gui     2953 selected, 2899 Passed, 3 FAILED, 2 Skipped, 49 DISABLED

DELTA MEASURED -- +38 selected and +38 Passed in EVERY configuration, nothing else moved.
```

## 2. The marginal timeout — a fourth data point

```text
DriverSeegmiller.MeshSequence, Debug+GUI, against the unchanged 7200 s cap:

    P18.1    PASSED   7123.03 s     98.9%
    P18.2    TIMEOUT  7201.94 s    >100%
    P18.3    PASSED   6764.75 s     94.0%
    P18.4    PASSED   7034.57 s     97.7%     <-- this run

Four measurements, spread ~437 s (6% of the cap), on trees differing only by additive changes the
test cannot reach. One of the four crossed; three did not.
```

```text
THE THREE FAILURES THIS RUN ARE THE RELIABLE SET, and only that set:
  50/2991 #2925 DriverSeegmiller.CorrectedSequenceIsDeterministic      Timeout 7200.60 s
  51/2991 #2927 DriverSeegmillerInlet.MappedInletPreservesTheMassFlow  Timeout 7200.59 s
  52/2991 #2928 DriverSeegmillerInlet.MediumGridAB                     Timeout 7200.59 s

NOT A P18.4 EFFECT: no fifth test, no new identity, and P18.4 modifies no existing production
source at all -- it adds two files and fills a source hook P18.2 already had.
```

## 3. Configuration asserted, not assumed

```text
release_cuda   build=Release  gui=OFF  cuda=ON   openmp=ON     asserted OK
cpu_only       build=Release  gui=OFF  cuda=OFF  openmp=ON     asserted OK
debug_gui      build=Debug    gui=ON   cuda=ON   openmp=OFF    asserted OK

Staleness (P17.7's checker, fail-closed): all 50 built test binaries postdate the newest production
source AND every source they actually compile.
```

`src/species/ReactionMechanism.cpp` builds and passes with `CFDAPP_ENABLE_CUDA=OFF` as well as ON,
closing the CPU-only blind spot for the new file.

## 4. Preservation

P18.4 **modifies no existing production source**. Preservation is therefore structural — measured
anyway (`logs/01_preservation.log`):

```text
P15  thermal unit suite                 163 / 163   (Enthalpy 8/8, TemperatureProperty 5/5)
P17  compressible unit suite            215 / 215   (ThermodynamicProperties 10/10, IdealGas 30/30,
                                                     TotalEnergy 34/34)
P14  transport unit suite                22 / 22
P16  turbulence unit suite              246 / 246
P18.1 + P18.2 + P18.3 + P18.4 species   203 / 203
```

```text
P18.2 SPECIFICALLY (section 47): with no reaction progress, the result is BITWISE what P18.2
produces with no reaction at all -- asserted for Su AND Sp, every species, every cell, by
ZeroProgressRateReproducesP18_2Exactly. Its own 34 tests pass unchanged, and
src/species/SpeciesTransport.cpp has 0 changes with its sha256 recorded.

P18.3 SPECIFICALLY (section 48): reactions READ molecular weights from P18.3 and store none --
0 stored, 4 read. A mismatch is impossible by design rather than by validation, and N3 confirms
that a wrong weight in the mixture is caught.
```

## 5. Focused suite

```text
CFDSpeciesTests    203 registered, 203 passed, 0 failed      logs/03_test_counts.log
  P18.4 new          38   ReactionTest 12 · ReactionMechanismTest 3 ·
                          ReactionSourceTest 19 · ReactionTransportTest 4
  P18.3              34
  P18.2              34
  P18.1              60
  pre-P18            37
```

## 6. Formatting and sanitizers

```text
clang-format   the 3 P18.4 files clean under --dry-run --Werror. The 28 PRE-EXISTING offenders are
               unchanged and none is a P18.4 file.
sanitizers     NOT RUN. The P18.4 criteria do not require them and CLAUDE.md section 8 places the
               sanitizer configuration in the full regression G11 settles at P18.9. Recorded with
               the reason. ReactionMechanism performs no manual memory management and every
               accessor bounds-checks and throws, which argues for expecting a clean run -- not for
               skipping one.
```

## 7. Runtime

```text
release_cuda  2057 s  against the baseline's 2141 s   -3.9%
cpu_only      1500 s  against 1402 s                  +7.0%
debug_gui     8131 s  against 7973 s                  +2.0%
```

Ordinary machine variance. Row counts, not runtimes, are the evidence.
