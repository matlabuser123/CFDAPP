# P18.0 baseline — Debug+GUI

```text
RESULT: reproduces the P17.11 baseline EXACTLY, including its 4 inherited failures.
        NO FIFTH FAILURE. Nothing moved between the two runs on an unchanged tree.
```

| Quantity | Measured | P17.11 baseline | Verdict |
|---|---:|---:|---|
| registered | 2838 | 2838 | match |
| matching the frozen exclusions | 13 | 13 | match |
| **selected** | **2825** | 2825 | match |
| **passed** | **2770** | 2770 | match |
| **failed** | **4** | 4 | match |
| **skipped** | **2** | 2 | match |
| **disabled (Not Run)** | **49** | 49 | match |
| ctest `out of` | 2776 | 2776 | match |
| **runtime** | **8514 s** | 8857 s | −3.9% |

```text
configuration ASSERTED after configure:
    build=Debug  gui=ON  cuda=ON  openmp=OFF
build          exit 0 (300 s), including the Qt GUI target
staleness      exit 0
ctest exit     8   (non-zero BECAUSE of the 4 inherited timeouts)
```

## The 4 failures are the inherited ones, named (§3)

```text
  2754 - DriverSeegmiller.MeshSequence                            (Timeout)
  2759 - DriverSeegmiller.CorrectedSequenceIsDeterministic        (Timeout)
  2761 - DriverSeegmillerInlet.MappedInletPreservesTheMassFlow    (Timeout)
  2762 - DriverSeegmillerInlet.MediumGridAB                       (Timeout)

IDENTICAL SET to P17.11's, test id for test id. All four are TIMEOUTS at the frozen 7200 s, not
assertion failures: each was still running when ctest stopped it. None produced a wrong number.

CLASSIFICATION: INHERITED BASELINE BEHAVIOUR, not a P18 regression.
  * they are pre-P17 backward-facing-step validation cases using no species and no compressible
    code;
  * they PASS in both Release configurations of this very run (measured again here);
  * Debug is >3.8x-6.0x slower than the 7200 s limit was set for;
  * P17.11 left G10 PARTIAL for exactly this and did not raise the timeout.
  results/p17/qualification/G10_DEBUG_GUI_TIMEOUTS.md

THE TIMEOUT WAS NOT RAISED HERE EITHER. §3 forbids weakening criteria to obtain a clean baseline,
and a limit chosen because 7200 did not pass would be fitted to the observed failure.
```

## Library hashes

```text
src/libcfdcore.a   1e9ec4b8b81ca77287a756b60e5f7db682bccfd6a66d74b2212c6baef2d425e9
cuda/libcfdcuda.a  9fc788477760c6c1a2022a45abcd03942b09fbb6999af93c6ebaf1ae0e74d042

These DIFFER from the Release archives, and that is correct, not a finding: a Debug build is
different object code. The Release archives (188e5039... and 1305a82a...) are the ones that must be
stable, and both were.
```

## One inherited side effect, re-confirmed here (L36)

```text
This Debug run rewrote 50 tracked results/validation reports with DEBUG-computed physics values --
l1/l2/linf, observed_order, asymptotic_ratio, extrapolated_error and the GCI fields -- not merely
timings. The tracked files are RELEASE-generated.

VERIFIED FOR THIS RUN rather than assumed from P17.11: the non-timing keys are confined to the
MMS and grid-convergence reports (the iteratively-converged quantities), and `wall_shear` changes
count exactly 0. That is the L36 signature.

results/validation was restored to its tracked state; 0 files modified afterwards.
results/p18/tools/restore_validation_noise.sh records the justification.
```
