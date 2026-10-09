# P18.3 — regression (§34)

```text
HARNESS   tools/p18_3_regress.sh -- P18.0's baseline harness, reused with only the output directory
          and the predictions changed. DISABLED tests are COUNTED and the four build options are
          ASSERTED after configure, failing closed on a mismatch.
POLICY    OMP_NUM_THREADS=1 · ctest -j 8 · --timeout 7200 · the same 5 frozen exclusions.
          The timeout was NOT raised.
LOG       logs/P18_3_regress.log
```

---

## 1. Results against the prediction declared before the run

| Configuration | | selected | Passed | FAILED | Skipped | DISABLED | runtime | exit |
|---|---|---:|---:|---:|---:|---:|---:|---:|
| **Release+CUDA** | predicted | 2891 | 2840 | 0 | 2 | 49 | | |
| | **measured** | **2891** | **2840** | **0** | **2** | **49** | 2141 s | 0 |
| **CPU-only** | predicted | 2668 | 2621 | 0 | 0 | 47 | | |
| | **measured** | **2668** | **2621** | **0** | **0** | **47** | 1402 s | 0 |
| **Debug+GUI** | predicted | 2953 | 2898 **or** 2899 | 4 **or** 3 | 2 | 49 | | |
| | **measured** | **2953** | **2899** | **3** | **2** | **49** | 7973 s | 8 |

```text
BASELINE (P18.2 close)
  release_cuda  2857 selected, 2806 Passed, 0 FAILED, 2 Skipped, 49 DISABLED
  cpu_only      2634 selected, 2587 Passed, 0 FAILED, 0 Skipped, 47 DISABLED
  debug_gui     2919 selected, 2864 Passed, 4 FAILED, 2 Skipped, 49 DISABLED

DELTA MEASURED
  release_cuda  +34 selected, +34 Passed, 0 elsewhere        EXACTLY AS PREDICTED
  cpu_only      +34 selected, +34 Passed, 0 elsewhere        EXACTLY AS PREDICTED
  debug_gui     +34 selected, +35 Passed, -1 FAILED          the marginal timeout, see section 2
```

Both Release configurations reproduce the baseline exactly. Skipped and DISABLED did not move in any
configuration. **No new failure appeared anywhere.**

## 2. The marginal timeout — now measured three times

The prediction named **both** branches, for the second phase running, because P18.1 and P18.2 had
already measured the same test on opposite sides of the cap. P18.3 is the third data point.

```text
DriverSeegmiller.MeshSequence, Debug+GUI, frozen 7200 s cap, on trees differing only by ADDITIVE
changes the test cannot reach:

    P18.1    PASSED   7123.03 s     98.9% of the cap
    P18.2    TIMEOUT  7201.94 s    >100%
    P18.3    PASSED   6764.75 s     94.0% of the cap     <-- this run

A spread of ~440 s, i.e. ~6% of the cap, across three runs of the same test.
```

```text
THE THREE FAILURES THIS RUN ARE THE RELIABLE SET, and only that set:
  59/2953 #2887 DriverSeegmiller.CorrectedSequenceIsDeterministic      Timeout 7200.61 s
  60/2953 #2889 DriverSeegmillerInlet.MappedInletPreservesTheMassFlow  Timeout 7200.63 s
  61/2953 #2890 DriverSeegmillerInlet.MediumGridAB                     Timeout 7200.65 s

NOT A P18.3 EFFECT:
  * no fifth test and no new identity, in any configuration;
  * MeshSequence passes in BOTH Release configurations of this same run;
  * P18.3 modifies no existing production source at all, so it cannot reach a turbulence test.

THE CHARACTERISATION IS NOW SETTLED: the inherited set is THREE RELIABLE TIMEOUTS PLUS ONE MARGINAL
TEST. P18.9 cannot rely on "the identical set, test id for test id" reproducing, and any phase that
predicts a single Debug+GUI failure count will be wrong roughly half the time.
```

## 3. Configuration asserted, not assumed

```text
release_cuda   build=Release  gui=OFF  cuda=ON   openmp=ON     asserted OK
cpu_only       build=Release  gui=OFF  cuda=OFF  openmp=ON     asserted OK
debug_gui      build=Debug    gui=ON   cuda=ON   openmp=OFF    asserted OK
```

`src/species/MixtureThermodynamics.cpp` builds and passes with `CFDAPP_ENABLE_CUDA=OFF` as well as
ON, which closes the CPU-only blind spot for the new file.

## 4. Preservation — measured, though it should be structural

P18.3 **modifies no existing production source**. It adds two files and one `target_sources` block.
Preservation is therefore structural — but that is a claim about code, so it was measured
(`logs/01_preservation.log`):

```text
P15  thermal unit suite                 163 / 163
     Enthalpy tests                       8 / 8
     TemperatureProperty tests            5 / 5
P17  compressible unit suite            215 / 215
     ThermodynamicProperties tests       10 / 10
     IdealGas tests                      30 / 30
     TotalEnergy tests                   34 / 34      <-- incl. the cp(T) refusal, unchanged
P14  transport unit suite                22 / 22
P16  turbulence unit suite              246 / 246
P18.1 + P18.2 + P18.3 species           165 / 165
```

```text
ONE FILTER MISS RECORDED RATHER THAN QUIETLY FIXED: the first preservation run filtered
'*TemperatureDependent*' and matched 0 tests -- the suite is named TemperatureProperty. A 0-test
filter reported as a pass is a vacuous claim, so the filter was corrected and the real count (5)
recorded.
```

## 5. Focused suite

```text
CFDSpeciesTests    165 registered, 165 passed, 0 failed       logs/02_test_counts.log
  P18.3 new          34   MixtureThermodynamicsTest 30 · SpeciesThermodynamicsTest 4
  P18.2              34
  P18.1              60
  pre-P18            37
```

## 6. Formatting and sanitizers

```text
clang-format   the 3 P18.3 files clean under --dry-run --Werror. The 28 PRE-EXISTING offenders are
               unchanged and none is a P18.3 file.
sanitizers     NOT RUN. G5 does not require them and CLAUDE.md section 8 places the sanitizer
               configuration in the full regression G11 settles at P18.9. Recorded with the reason.
               Noted for that phase: MixtureThermodynamics performs no manual memory management, no
               pointer arithmetic and no unchecked indexing -- every accessor bounds-checks and
               throws. That argues for expecting a clean run, NOT for skipping one.
```

## 7. Runtime

```text
release_cuda  2141 s  against the baseline's 2141... measured 2141 vs 2121   +0.9%
cpu_only      1402 s  against 1455 s                                         -3.6%
debug_gui     7973 s  against 8670 s                                         -8.0%
```

The Debug+GUI run was 8% faster than P18.2's, which is consistent with the marginal test coming in
435 s under the cap rather than 2 s over it. Row counts, not runtimes, are the evidence.
