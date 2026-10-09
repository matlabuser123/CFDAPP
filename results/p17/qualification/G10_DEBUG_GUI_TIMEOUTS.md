# P17.11 — G10: the Debug+GUI configuration has 4 timeouts

Required by §44 and §25. The Debug+GUI regression had never been run in any P17 phase — every phase
from P17.0 onward deferred it here — so this is its first execution against the P17 tree, and it is
the reason G10 closes as PARTIAL rather than PASS.

```text
GATE        G10 -- full regression
CONFIGS     release_cuda  PASS   2712 passed, 0 failed
            cpu_only      PASS   2493 passed, 0 failed
            debug_gui     FAIL   2770 passed, 4 FAILED (all timeouts), ctest exit 8
VERDICT     G10 = PARTIAL. Two of three required configurations pass fully.
            §25: "One required configuration failing means G10 is not fully closed."
ACTION      The frozen --timeout 7200 is NOT raised. See §4.
```

---

## 1. The four failures

```text
  29/2825 Test #2759: DriverSeegmiller.CorrectedSequenceIsDeterministic      ***Timeout 7200.65 sec
  30/2825 Test #2762: DriverSeegmillerInlet.MediumGridAB                     ***Timeout 7200.69 sec
  31/2825 Test #2761: DriverSeegmillerInlet.MappedInletPreservesTheMassFlow  ***Timeout 7200.73 sec
  32/2825 Test #2754: DriverSeegmiller.MeshSequence                          ***Timeout 7200.75 sec
```

All four are **timeouts at exactly the frozen limit**, not assertion failures. Not one produced a
wrong number: each was still running when ctest stopped it.

## 2. They are not P17 tests

```text
tests/integration/backward_facing_step/test_driver_seegmiller.cpp
tests/integration/backward_facing_step/test_driver_seegmiller_inlet.cpp
tests/integration/backward_facing_step/DriverSeegmillerCase.cpp
tests/integration/backward_facing_step/DriverSeegmillerPrecursor.cpp
```

The Driver–Seegmiller backward-facing step is an incompressible-SIMPLE / turbulence validation case
that predates P17. It uses no compressible code: no EOS, no HLLC, no density-based solver, no
total-energy equation.

## 3. They pass in both Release configurations, on this exact tree

```text
test                                              release_cuda      cpu_only        debug_gui
DriverSeegmiller.MeshSequence                     Passed 1199.2s    Passed 1028.1s  >7200s TIMEOUT
DriverSeegmiller.CorrectedSequenceIsDeterministic Passed 1880.2s    Passed 1504.6s  >7200s TIMEOUT
DriverSeegmillerInlet.MappedInletPreserves...     Passed 1489.8s    Passed 1219.4s  >7200s TIMEOUT
DriverSeegmillerInlet.MediumGridAB                Passed 1882.8s    Passed 1521.3s  >7200s TIMEOUT

implied Debug slowdown, a LOWER BOUND because Debug was cut off rather than completing:
    >6.00x, >3.83x, >4.83x, >3.82x
```

## 4. P17 did not cause it — checked, not assumed

```text
The obvious worry is that P17 made these tests slower. It did not. Their CPU-only runtimes across
two phases on the same hardware:

test                                              P17.9      P17.11     change
CorrectedSequenceIsDeterministic                  1389.6s -> 1504.6s    +8.3%
MappedInletPreservesTheMassFlow                   1671.2s -> 1219.4s   -27.0%
MediumGridAB                                      1690.1s -> 1521.3s   -10.0%

TWO OF THE THREE GOT FASTER. The spread is run-to-run machine load, not a trend: these are
multi-thousand-second solves on a laptop under varying load, and +-27% is ordinary for them. There is
no P17-attributable slowdown to find.

AND THE STRUCTURAL ARGUMENT AGREES WITH THE MEASUREMENT: P17 added a CUDA translation unit, two GPU
headers and test files. src/libcfdcore.a came out of both Release configurations BYTE-IDENTICAL to
the archives P17.8 and P17.9 closed with (188e5039... and 1305a82a...), so the CPU code these tests
execute is bit-for-bit what it was before P17.10. Code that is byte-identical cannot have become
slower.
```

## 5. Why the timeout was not raised

```text
The frozen execution policy is `--timeout 7200`, inherited verbatim from P13's G10 closeout and used
unchanged by every P17 phase. Raising it to 14400 would turn all four timeouts into passes and G10
into PASS.

THAT IS EXACTLY THE MOVE THIS PROJECT'S GATE PROTOCOL FORBIDS. CLAUDE.md section 5.3: a frozen
threshold is not relaxed to get past a stop; a replacement must be DERIVED, not fitted to the
observed failure. A timeout chosen because 7200 did not pass is fitted to the observed failure by
definition.

IT WOULD ALSO BE THE WRONG DIAGNOSIS. The honest reading is not "the limit is too small" but "the
Debug configuration is 4-6x slower than the limit was set for, on four long pre-existing validation
cases". Whether the right answer is a per-test timeout, a Debug-specific exclusion, or accepting that
Debug+GUI cannot run these four within 2 hours, is a decision about the qualification contract -- and
it is not P17.11's to make unilaterally, because it changes what G10 means for every future phase.
```

## 6. What Debug+GUI DID establish, which is the point of running it

```text
This was the configuration's first run in P17, so its value is not only the four timeouts:

  built clean                       378 s, exit 0, including the Qt GUI target cfdapp_gui
  GUI tests registered and ran      2838 registered vs 2776 for release_cuda -- 62 GUI-side tests
  debug assertions                  2770 tests passed WITH Debug assertions active; no assertion
                                    fired anywhere in the P17 code
  staleness                         0 stale binaries
  every P17 test passed             the focused P17 suites are all inside the 2770
  4 failures, all timeouts          0 assertion failures, 0 exceptions, 0 wrong answers

So the configuration qualified P17's code and failed only on wall-clock limits for four non-P17
tests. That distinction is what makes G10 PARTIAL rather than FAIL.
```

## 7. A second thing the first Debug run exposed: it rewrites tracked PHYSICS values

```text
The runtime-noise audit after the three-configuration regression found changes that were NOT timing
keys -- l1, l2, linf, observed_order, asymptotic_ratio and extrapolated_error in
results/validation/mms/. Every previous P17 phase's audit reported "timing only", and every previous
P17 phase ran ONLY Release configurations.

ATTRIBUTED BY EXPERIMENT, not by assumption. The same MMS test was run from each configuration's own
binary in turn, with the tracked file restored between runs:

    release_cuda   writes 8 changed keys, ALL "seconds"          -> timing only
    cpu_only       writes 8 changed keys, ALL "seconds"          -> timing only
    debug_gui      writes l1/l2/linf/observed_order/... changed  -> 158 differing lines

So the Debug binary writes different numbers than the Release binary. Magnitude, by field:

    field                   n    worst relative difference
    extrapolated_error      4    1.128e-05     <- Richardson extrapolation AMPLIFIES its inputs
    observed_order          4    4.929e-07
    l1                     18    4.136e-07
    linf                   18    3.664e-07
    l2                     18    3.597e-07
    reduction_factors       6    3.282e-07
    asymptotic_ratio        4    2.732e-07
    seconds                 4    8.526e-01     <- timing, expected

WHAT IT IS. ~4e-7 is far above round-off (1e-16) and far below anything physical. These are
ITERATIVELY CONVERGED solves: a Debug build and a Release build take slightly different paths and
stop at slightly different points inside the same convergence tolerance. The extrapolated_error
column is larger only because Richardson extrapolation divides a difference of nearly-equal numbers.

WHAT IT IS NOT. It is not a P17 defect: the tests PASS in every configuration, and the same
divergence would have appeared in any phase that ran Debug. It is a pre-existing property of the
project that went unseen because Debug+GUI had been deferred for eleven sub-phases.

WHY IT MATTERS ANYWAY. The tracked results/validation reports are RELEASE-GENERATED. Anyone who runs
the Debug regression and commits the working tree will silently replace validated Release numbers
with Debug ones. Nothing in the harness warns about this today. P17.11 restored the tree
(0 modified) and this is recorded as limitation L36.
```

## 8. Status

```text
G10           PARTIAL
release_cuda  PASS  -- 2776 registered / 2763 selected / 2712 passed / 0 failed / 2 skipped /
                       49 disabled / 2061 s. Every count matched the pre-declared prediction.
cpu_only      PASS  -- 2553 / 2540 / 2493 / 0 / 0 / 47 / 1527 s. Every count matched.
debug_gui     FAIL  -- 2838 / 2825 / 2770 / 4 timeouts / 2 skipped / 49 disabled / 8857 s.
                       No prediction had been declared, because the configuration had never run.
TIMEOUT       unchanged at 7200 s
EVIDENCE      logs/40_p17_11_regress.log, logs/40_debug_gui_ctest.log
```
