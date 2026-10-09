# P18.2 — regression

```text
HARNESS   tools/p18_2_regress.sh -- P18.0's baseline harness, reused with only the output directory
          and the predictions changed. Its inherited fixes are retained: DISABLED tests are COUNTED,
          and the four build options are ASSERTED after configure, failing closed on a mismatch.
POLICY    OMP_NUM_THREADS=1 · ctest -j 8 · --timeout 7200 · the same 5 frozen exclusions.
          The timeout was NOT raised.
LOG       logs/P18_2_regress.log
```

---

## 1. Results against the prediction declared before the run

| Configuration | | selected | Passed | FAILED | Skipped | DISABLED | runtime | exit |
|---|---|---:|---:|---:|---:|---:|---:|---:|
| **Release+CUDA** | predicted | 2857 | 2806 | 0 | 2 | 49 | | |
| | **measured** | **2857** | **2806** | **0** | **2** | **49** | 2121 s | 0 |
| **CPU-only** | predicted | 2634 | 2587 | 0 | 0 | 47 | | |
| | **measured** | **2634** | **2587** | **0** | **0** | **47** | 1455 s | 0 |
| **Debug+GUI** | predicted | 2919 | 2865 **or** 2864 | 3 **or** 4 | 2 | 49 | | |
| | **measured** | **2919** | **2864** | **4** | **2** | **49** | 8670 s | 8 |

```text
BASELINE (P18.1 close)
  release_cuda  2823 selected, 2772 Passed, 0 FAILED, 2 Skipped, 49 DISABLED
  cpu_only      2600 selected, 2553 Passed, 0 FAILED, 0 Skipped, 47 DISABLED
  debug_gui     2885 selected, 2831 Passed, 3 FAILED, 2 Skipped, 49 DISABLED

DELTA MEASURED
  release_cuda  +34 selected, +34 Passed, 0 elsewhere        EXACTLY AS PREDICTED
  cpu_only      +34 selected, +34 Passed, 0 elsewhere        EXACTLY AS PREDICTED
  debug_gui     +34 selected, +33 Passed, +1 FAILED          the marginal timeout, see section 2
```

Both Release configurations reproduce the baseline exactly, with the delta being precisely the 34
new tests and nothing else. Skipped and DISABLED did not move in any configuration.

## 2. The Debug+GUI failure count, predicted as a branch and why that is not hedging

```text
PREDICTED, before the run:  "3 OR 4 FAILED ... it is what P18.1 MEASURED.
                             DriverSeegmiller.MeshSequence completed in 7123.03 s against the frozen
                             7200 s cap, i.e. at 98.9% of the limit, so whether it times out depends
                             on machine load. The inherited set is THREE RELIABLE TIMEOUTS plus ONE
                             MARGINAL TEST, not four reliable ones."
MEASURED:                    4 FAILED -- the marginal test timed out this time.

  32/2919 #2853 DriverSeegmiller.CorrectedSequenceIsDeterministic    Timeout 7201.93 s
  33/2919 #2855 DriverSeegmillerInlet.MappedInletPreservesTheMassFlow Timeout 7201.92 s
  34/2919 #2856 DriverSeegmillerInlet.MediumGridAB                    Timeout 7201.94 s
  35/2919 #2848 DriverSeegmiller.MeshSequence                         Timeout 7201.94 s   <-- MARGINAL
```

```text
THIS IS THE P18.1 FINDING CONFIRMED, not a new one. The same test, on a tree differing only by
P18.2's additive changes, passed at 7123 s in the P18.1 run and timed out here. That is the
definition of marginal, and it is why the prediction named both branches instead of one.

NOT A P18.2 REGRESSION, on three grounds:
  1. it is one of the FOUR KNOWN inherited pre-P17 tests -- no fifth test, no new identity;
  2. it PASSES IN RELEASE IN THIS SAME RUN, at 1253.44 s, 5.7x under the cap -- so it is a time
     budget, not a correctness defect, exactly as P17.11 recorded when it left G10 PARTIAL;
  3. P18.2 cannot reach it: DriverSeegmiller does not reference the species transport, and P14's
     behaviour for every existing caller is bitwise unchanged (section 4).

THE TIMEOUT WAS NOT RAISED to clear it.
```

## 3. Configuration asserted, not assumed

```text
release_cuda   build=Release  gui=OFF  cuda=ON   openmp=ON     asserted OK
cpu_only       build=Release  gui=OFF  cuda=OFF  openmp=ON     asserted OK
debug_gui      build=Debug    gui=ON   cuda=ON   openmp=OFF    asserted OK
```

**CPU-only matters beyond its counts.** Files compiled only when CUDA is OFF are exercised by no
CUDA build, and this project has a recorded blind spot there. `src/species/SpeciesTransport.cpp` and
the amended `src/transport/ScalarTransport.cpp` both build and pass with `CFDAPP_ENABLE_CUDA=OFF`.

## 4. P14 preservation — the precondition for the whole phase

The boundary fix lives in P14, the **only** scalar transport assembly in the codebase, carrying all
six production turbulence equations. It is an opt-in selector defaulting to the pre-P18.2 behaviour.

```text
logs/10_preservation_final.log, after every P18.2 change:
  transport characterization (frozen matrix, sparsity, RHS)          3 / 3
  transport unit suite                                              22 / 22
  P14 migration equivalence (frozen solved field, 3 FixedGradient)   3 / 3
  turbulence unit suite                                            246 / 246
  thermal unit suite                                               163 / 163
  species unit suite                                               131 / 131
Both frozen oracles carry their own CanDetectAnAssemblyChange non-vacuity control.
```

## 5. Focused suite

```text
CFDSpeciesTests    131 registered, 131 passed, 0 failed     logs/11_focused_status_final.log
  P18.2 new         34   SpeciesClosureTest 8 · FaceConservation 2 · Source 2 · Boundary 4 ·
                         Transient 3 · Transport 2 · Canonical 4 · Conservation 7 · Control 2
  P18.1             60
  pre-P18           37
```

## 6. Formatting and sanitizers

```text
clang-format   the 6 P18.2 files clean under --dry-run --Werror. The 28 PRE-EXISTING offenders are
               unchanged and none is a P18.2 file -- inherited from the uncommitted P14-P17 tree.
sanitizers     NOT RUN. G4 does not require them and CLAUDE.md section 8 places the sanitizer
               configuration in the full regression G11 settles at P18.9. Recorded with the reason
               rather than silently omitted.
```

## 7. Runtime

```text
release_cuda  2121 s  against the baseline's 2060 s   +3.0%
cpu_only      1455 s  against 1442 s                  +0.9%
debug_gui     8670 s  against 8172 s                  +6.1%
```

Ordinary machine variance on multi-thousand-second runs, in the slower direction — which is
consistent with the marginal test crossing its cap this time and is the mechanism section 2 names.
The row counts, not the runtimes, are the evidence.
