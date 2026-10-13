# P18.8 §57 — regression prediction, ADDENDUM 2: corrected baseline and method

`regression_prediction.md` (sha256 `ab941cf5…`) and `regression_prediction_addendum_2.md`'s
predecessor, `regression_prediction_addendum.md`, are **not edited**. This addendum corrects the
*baseline figures* and the *execution method* they rested on.

Written **before** attempt 2 was launched. Attempt 1 is marked
`logs/regression_INVALID_AS_AUTHORITATIVE.md` and produced no evidence.

## 1. The execution policy — the thing attempt 1 got wrong

Inherited verbatim from P18.7 ← P17.10 ← P13's G10 closeout:

```
OMP_NUM_THREADS=1   ctest -j 8   --timeout 7200   -E <5 frozen exclusions>
```

Exclusions: `SSTAutomaticWallChannel`, `SSTCompositeWallChannel`, `SSTMomentumFluxChannel`,
`WallArchitectureDiagnostic`, `DriverSeegmiller\.CorrectedMeshSequence`.

Attempt 1 used `-j 16`, no `OMP_NUM_THREADS`, and no exclusions. With OpenMP ON, sixteen heavyweight
tests each spawned threads across all 32 logical CPUs; the same tests inflated by **3.8× to 62×** and
15 of 16 hit the 7200 s cap. **A run under different execution conditions than the baseline is not
comparable to the baseline, whatever it reports.**

## 2. The corrected baseline, from P18.7's own log

My earlier predictions used 3028 / 2805 / 3090 as the "selected" baselines. Those figures are not in
P18.7's evidence. Its recorded log says:

| config | registered | ran (`out of`) | passed | failed | skipped | exit |
| --- | --- | --- | --- | --- | --- | --- |
| `release_cuda` | **3041** | **2979** | 2977 | 0 | 2 | 0 |
| `cpu_only` | **2818** | **2758** | 2758 | 0 | 0 | 0 |
| `debug_gui` | **3103** | **3041** | 3036 | **3** | 2 | 8 |

The *passed* baselines I used (2977 / 2758 / 3036) were correct, so the predicted passed counts in
addendum 1 stand unchanged. Only the registered/ran basis was wrong.

`registered − ran` decomposes as `disabled + excluded`: 3041 − 49 − 13 = 2979, 2818 − 47 − 13 = 2758,
3103 − 49 − 13 = 3041. All three are consistent, which is what makes the decomposition trustworthy
rather than a fitted explanation.

## 3. The third configuration is `debug_gui`, not `debug_cuda`

Attempt 1 relabelled it after reading `CFDAPP_ENABLE_GUI`, **which does not exist**. The real
variable is `CFDAPP_BUILD_GUI`, and `build/qual-debug` has `CFDAPP_BUILD_GUI:BOOL=ON`. All three
caches match P18.7's asserted expectations exactly:

| label | dir | build | gui | cuda | openmp |
| --- | --- | --- | --- | --- | --- |
| `release_cuda` | `build/qual-final` | Release | OFF | ON | ON |
| `cpu_only` | `build/qual-cpuonly` | Release | OFF | OFF | ON |
| `debug_gui` | `build/qual-debug` | Debug | **ON** | ON | OFF |

The relabel is withdrawn. The harness now asserts all four options per configuration and refuses to
test one that does not match.

## 4. The corrected prediction

P18.8 adds **17** species rows (all configurations) and **23** GPU rows (CUDA configurations only,
of which **3** are `DISABLED_`), from `--gtest_list_tests`.

| | registered | ran | passed | failed | skipped | disabled |
| --- | --- | --- | --- | --- | --- | --- |
| `release_cuda` | **3081** | **3016** | **3014** | **0** | **2** | **52** |
| `cpu_only` | **2835** | **2775** | **2775** | **0** | **0** | **47** |
| `debug_gui` | **3143** | **3078** | **3073** | **3** | **2** | **52** |

Each row is internally consistent: `passed + failed + skipped = ran`, and
`ran = registered − disabled − 13 excluded`.

**One figure is already confirmed empirically.** Attempt 1, which ran without exclusions, reported
`3081` tests to run for `release_cuda` — exactly `3041 + 40`. So the registered count is measured,
not merely predicted.

## 5. Corrected expectations about the long tests

The claim I repeated — that `DriverSeegmiller.MeshSequence` takes ~7000 s and sets a ~2-hour wall
time — **is wrong**. P18.7 measured it at **1219.90 s, passing**. That timeout history belongs to
`DriverSeegmiller.CorrectedMeshSequence`, which is one of the 5 **excluded** patterns precisely
because it sits on the cap.

P18.7's entire `release_cuda` suite ran in **2083 s (35 minutes)** for 2 979 tests.

So the expected cost is **~4 min build + ~35–40 min ctest per configuration ≈ 2 hours for all
three**, not the 6 hours I stated twice.

## 6. Risks, restated

1. **The 3 `debug_gui` failures must reproduce their recorded values exactly.** A known failure stays
   known only while it does (CLAUDE.md §8).
2. **The 3 `DISABLED_` sanitizer controls must land in the disabled count, never the passed count.**
   They perform a deliberate out-of-bounds access and a deliberate uninitialised read.
3. **Any timeout is now a real finding**, not a scheduling artifact — the execution policy matches the
   baseline's, so a test that times out under it did not time out for P18.7.
4. `cpu_only` is the only configuration compiling the shared header with CUDA **OFF**.
5. A full `ctest` rewrites tracked `results/validation/**` with timing-only changes; that is runtime
   noise, to be classified and reverted, never presented as a P18.8 change.
