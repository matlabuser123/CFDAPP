# P15 — final regression

Written for: a reviewer deciding whether P15 can close.

Run on the closed tree, `2026-09-22T14:38Z → 15:16Z`, wall **38 min**.
Log: `logs/final/` (`ctest_*.log`, `build_*.log`).

---

## Result

| Configuration | Build | ctest | Tests | Failures |
|---|---|---|---|---|
| `release_cuda` (`build/final`) | ok, 46 s | exit 0, 298 s | **2160** | **0** |
| `debug_gui` (`build/debug`) | ok, 299 s | exit 0, 1328 s | **2145** | **0** |
| `cpu_only` (`build/cpuonly`) | ok, 83 s | exit 0, 233 s | **2085** | **0** |

**100 % passed in all three. Zero failures.**

`ctest -j 16`, the parallelism derived in `parallelism.md` (OpenMP is OFF in every
configuration, so `ctest -j` cannot oversubscribe; the floor is the longest
single test).

### SIMPLE fingerprint (P13-001)

```
cases fingerprinted: 10
IDENTICAL -- SIMPLE is bitwise unchanged
```

This is the standing guard that P15 changed nothing about the pressure–velocity
solve. It is a **bitwise** comparison over ten cases, not a tolerance check.

---

## Test-count deltas, and why they are what they are

Against the pre-patch gate (`release_cuda` 2106, `debug_gui` 2100,
`cpu_only` 2040):

| Configuration | Before | After | Δ |
|---|---|---|---|
| `release_cuda` | 2106 | 2160 | **+54** |
| `debug_gui` | 2100 | 2145 | **+45** |
| `cpu_only` | 2040 | 2085 | **+45** |

The two CPU configurations gain **exactly the same 45**, and `release_cuda`
gains **9 more**. That 9 is the `BoundaryEncoding.*` suite, which is GPU-only and
therefore builds in no other configuration. The arithmetic is self-consistent —
a mismatch there would have meant a test silently not registering, which is
exactly the failure P13-007 hit (a smoke test that never registered, revealed
only because the test counts were identical to the previous phase).

The 45 are P15's staged patches plus this session's additions: 23
`ThermalPropertyCase`, 5 `VariablePropertyProductionCase`, 1
`TimestepConvergence`, net +1 `ConvectiveBoundary` (one replaced, two added),
and the thermal transient / enthalpy / Robin / Case E–G tests from the staged
patch set.

No test was removed. The one stale test
(`ConvectiveBoundary.BoundaryValueThrowsRatherThanGuessing`) was **re-pointed,
not deleted** — see `negative_controls.md`.

---

## Formatting: 22 deviations, none of them P15's, none of them in CI's scope

The regression script's format check reported 22 files:

```
cuda/kernels/CsrSpmvKernel.cu
cuda/kernels/DeviceBoundaryConditionsKernel.cu
cuda/kernels/DeviceConvectionKernel.cu
... 19 more, all under cuda/
```

**This is not a P15 regression, and it is not a CI failure.** Three facts,
each checked rather than assumed:

1. **CI does not check `cuda/`.** `ci.yml`'s format step is
   `find include src apps tests \( -name '*.cpp' -o -name '*.hpp' -o -name '*.h' \)`.
   `cuda/` appears in no `find` anywhere in `ci.yml`, and `*.cu` is not among the
   extensions. Run at CI's exact scope, the tree is **clean — exit 0, no
   output**.

2. **P15 touched none of the 22.** The four `cuda/` files P15 edited —
   `DeviceBoundaryConditions.cpp`, `DeviceGradientPlan.cpp`,
   `DeviceDiffusionPlan.cpp`, `DeviceConvectionPlan.cpp` — are **CLEAN**, and so
   are all eleven files P15 touched outside `cuda/`. Fifteen for fifteen.

3. **The deviations were surfaced by a widened probe, not created.** The earlier
   `regression.sh` checked `include src apps tests`, matching CI.
   `final_regression.sh` adds `cuda` and `*.cu`, so these files were format-checked
   here for the first time. They pre-date P15.

**They were deliberately not fixed.** Reformatting 22 files P15 never touched
would be an unrelated change under no authorization, and it would invalidate the
regression above by modifying sources after it ran. Recorded here so the next
phase can decide whether to bring `cuda/` into CI's format scope — which is a
real gap worth closing, just not P15's to close.

This is the same shape as the `cpu-only-build-blind-spot` finding: a directory
no routine gate covers. Worth naming as such.

---

## What this regression does and does not establish

**Does:** every test in the repository passes in all three configurations after
P15's changes; SIMPLE is bitwise unchanged; the CI format gate is clean at its
own scope.

**Does not:** say anything about GitHub CI itself. The CI blocker for `3bd32f9`
(no runner assigned) is unresolved and independent of P15. **`KNOWN-GOOD`
remains `548401a`** and must not move until real CI succeeds.
