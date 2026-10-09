# G10 — failure audit

Every non-PASS result across the three configurations, classified. **No result is
left as "FAILED — unknown".**

```text
Release+CUDA   2488 tests   0 failed   0 Not Run   98 Disabled
Debug+GUI      2550 tests   3 failed   0 Not Run   98 Disabled
CPU-only       2288 tests   0 failed   0 Not Run   94 Disabled
```

---

## The three Debug+GUI failures

```text
2533 - DriverSeegmiller.CorrectedSequenceIsDeterministic       (Timeout)
2535 - DriverSeegmillerInlet.MappedInletPreservesTheMassFlow   (Timeout)
2536 - DriverSeegmillerInlet.MediumGridAB                      (Timeout)
```

**All three are Timeouts. Zero assertion failures anywhere in any configuration.**

### Classification: category 3 — environmental / resource limitation

Not a production regression, and the reasoning is measured rather than asserted.

**1. All three PASS in both other configurations, on identical source.**

```text
test                                Release+CUDA   CPU-only     Debug+GUI
MappedInletPreservesTheMassFlow     1529.35 s      1170.41 s    Timeout
CorrectedSequenceIsDeterministic    1950.04 s      1466.65 s    Timeout
MediumGridAB                        2029.67 s      1479.59 s    Timeout
```

Same source tree, same exclusions, same `OMP_NUM_THREADS=1 -j 8 --timeout 7200`
invocation. The only variable is `CMAKE_BUILD_TYPE`.

**2. The Debug slowdown was measured, over 123 independent samples.**

Every test present in both the Release+CUDA and Debug+GUI logs with a recorded
time, taking more than 5 s in Release:

```text
samples        123
mean ratio     5.0x
median ratio   5.4x
min / max      0.9x / 6.9x
```

Representative rows:

```text
NaturalConvectionValidation.Grid10x10MatchesDeVahlDavisRa1e3    92.84 ->  467.35   x5.0
WallFunctionChannelHighRe.EveryG1ValidMeshPassesTheAdoptedGate  52.64 ->  341.20   x6.5
SSTWallFunctionChannel.LowReStressSweep                         29.77 ->  203.96   x6.9
CavityGhiaValidation.Grid20x20IsDeterministic                   22.78 ->  133.48   x5.9
```

**3. The timeouts are therefore arithmetic, not defects.** At the measured mean
ratio:

```text
MappedInletPreservesTheMassFlow    1529 s  ->  ~7582 s    ceiling 7200   EXCEEDS
CorrectedSequenceIsDeterministic   1950 s  ->  ~9668 s    ceiling 7200   EXCEEDS
MediumGridAB                       2030 s  -> ~10063 s    ceiling 7200   EXCEEDS
```

These three are already the slowest non-excluded tests in the suite in Release,
consuming a quarter to a third of the ceiling there. A 5x Debug factor puts all
three past it. No Debug build of this source can complete them inside 7200 s.

### What was NOT done about it

* **The 7200 s timeout was not raised.** That would make the number fit the
  result.
* **No exclusion was added.** Excluding them would turn a measured limitation
  into an invisible one, and the instruction forbids creating an exclusion to
  obtain a green result.
* **No test was modified, shortened or disabled.**

Consequently **Full Debug+GUI regression cannot be reported PASS**, and it is
not. It is reported as 2547 of 2550 passing with three explained Debug-runtime
timeouts, pending a decision (see `final_closeout.md` §blockers).

### Why this was not caught earlier

Prior Debug+GUI evidence does not cover these tests at all:

```text
results/p15/thermal-energy/logs/ctest_debug_gui.log   100% passed, 2100 tests
results/p13/packaging/logs/ctest_debug_gui.log        100% passed, 2042 tests
results/p13/benchmarks/logs/ctest_debug_gui.log       100% passed, 2043 tests
```

None contains a `DriverSeegmiller` line — the suite postdates them. G10 is the
first Debug+GUI run to include it, which is the point of running the full matrix
rather than extrapolating from one configuration.

---

## The 98 / 98 / 94 Disabled tests

`Disabled` is a GoogleTest `DISABLED_` prefix, not a ctest exclusion, and is
pre-existing in all three configurations. They are the long grid-refinement and
convergence studies (`ChannelFlowValidation.*Fine*`, `SIMPLEMMS.FineGridStudy`,
`MMS3DTest.OperatorRefinementStudy`, `Duct3DProductionCase.*`,
`LidDrivenCube3DProductionCase.*`, `BackwardFacingStep.*`, and similar), disabled
in the source by earlier phases. The CPU-only count is 94 rather than 98 because
four disabled GPU tests are not built at all when `CFDAPP_ENABLE_CUDA=OFF`.

Not introduced by this qualification, and not counted as PASS.

---

## Zero Not Run

`Not Run` is 0 in all three configurations. Nothing was skipped for a missing
fixture, missing binary or unmet dependency.

---

## The first G10 invocation

Killed after 5 h 38 m at 64/2537 with 7 Timeouts. Category 6, **invalid
invocation** — `OMP_NUM_THREADS=1` was omitted from the frozen policy, giving
~256 runnable threads on 32 cores (observed load 255.94). Fully preserved with
its diagnosis at `logs/PRESERVED_invalid_no_omp_limit/WHY_INVALID.md`. No result
from it is reported as a failure of the code.
