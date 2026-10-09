# P18.8 §57 — regression attempt 1: INVALID AS AUTHORITATIVE

**Marked in place, not deleted** (CLAUDE.md §4). The raw log of the attempt is
`logs/regression_attempt1_INVALID/release_cuda.log`.

**Nothing here is evidence about the code.** The run was stopped after the first configuration
produced 15 timeouts against a baseline of 0 failures, and the cause is my harness, not the tree.

## What the attempt reported

`release_cuda`, `ctest -j 16 --timeout 7200`, no exclusions, no `OMP_NUM_THREADS`:

| | |
| --- | --- |
| tests ctest would run | 3081 |
| completed before the run was stopped | 16 |
| **timeouts at ~7200 s** | **15** |
| passes | 1 (`WallFunctionChannelGpu.IsBitwiseDeterministic`, 6927 s) |

ctest scheduled **longest-first** from its persisted `CTestCostData`, so all 16 heavyweight tests
started simultaneously and every one of them blew the 7200 s cap.

## Why it is invalid: the inflation factors

The same tests, as P18.7 measured them under the inherited execution policy:

| test | P18.7 | attempt 1 | factor |
| --- | --- | --- | --- |
| `NaturalConvectionValidation.GridConvergence` | 116.59 s pass | 7202.88 s **timeout** | **62×** |
| `ResidentTurbulentSimple.ResidentGpuSolveMatchesTheCpuProductionSolve` | 164.05 s pass | 7202.58 s **timeout** | **44×** |
| `ResidentChannelProduction.DivergenceOnsetCheckpointSweep` | 253.00 s pass | 7202.48 s **timeout** | **28×** |
| `WallFunctionChannelGpu.IsBitwiseDeterministic` | 255.93 s pass | 6927.00 s pass | **27×** |
| `WallFunctionChannelHighReGpu.EveryG1ValidMeshPassesTheAdoptedGate` | 888.44 s pass | 7201.59 s **timeout** | **8.1×** |
| `DriverSeegmiller.MeshSequence` | **1219.90 s pass** | 7200.14 s **timeout** | **5.9×** |
| `DriverSeegmillerInlet.MediumGridAB` | 1884.85 s pass | 7200.18 s **timeout** | **3.8×** |

P18.7's whole `release_cuda` suite took **2083 s — 35 minutes** for 2 979 tests, 100 % passed.

## The three ways my harness differed from the baseline's method

The policy is inherited verbatim from P17.10, which inherited it from P13's G10 closeout
(`results/p18/conservation_validation/tools/p18_7_regress.sh:5`):

```
OMP_NUM_THREADS=1 ctest -j 8 --timeout 7200 -E <5 frozen exclusions>
```

| | baseline | attempt 1 | consequence |
| --- | --- | --- | --- |
| `OMP_NUM_THREADS` | **1** | unset | with OpenMP ON, each of 16 concurrent tests spawned threads across all 32 logical CPUs — catastrophic oversubscription. This is the dominant cause. |
| ctest jobs | **8** | 16 | twice the concurrency on a hybrid CPU, against CLAUDE.md's "go lower for long suites" |
| exclusions | **5 frozen patterns** | none | ran tests the baseline never ran, so the counts were not comparable even apart from the timeouts |

The 5 frozen patterns: `SSTAutomaticWallChannel`, `SSTCompositeWallChannel`,
`SSTMomentumFluxChannel`, `WallArchitectureDiagnostic`, `DriverSeegmiller\.CorrectedMeshSequence`.

## Three further errors of mine that this uncovered

1. **I read the wrong CMake variable.** My configuration probe queried `CFDAPP_ENABLE_GUI`, which
   does not exist; the real name is **`CFDAPP_BUILD_GUI`**. So I concluded `build/qual-debug` had GUI
   off and relabelled the configuration `debug_cuda`. It has **`CFDAPP_BUILD_GUI:BOOL=ON`** and is
   exactly P18.7's `debug_gui`. The relabel is withdrawn.
2. **My frozen prediction's "selected" baseline was wrong.** It used 3028 / 2805 / 3090; P18.7's own
   log records **3041 / 2818 / 3103** registered. The *passed* baselines it used (2977 / 2758 /
   3036) were correct, so the predicted passed counts stand — see
   `regression_prediction_addendum_2.md`.
3. **I attributed a timeout history to the wrong test.** I had been describing
   `DriverSeegmiller.MeshSequence` as having 7 measurements of 6765–7202 s against the cap, 5 passes
   and 2 timeouts. P18.7 measured it at **1219.90 s, passing**. That history belongs to
   `DriverSeegmiller.CorrectedMeshSequence` — which is one of the 5 **excluded** patterns, precisely
   because it sits on the cap. Every statement I made about `MeshSequence` setting a ~2-hour wall
   time was wrong, and so was the ~6-hour estimate built on it.

   **CORRECTION TO ITEM 3, added 2026-10-02 after attempt 2 completed.** The paragraph above is
   itself wrong, and is kept visible rather than rewritten. The straddling history DOES belong to
   `DriverSeegmiller.MeshSequence` — in the **Debug** configuration. P18.7's `debug_gui` log measures
   it at **6963.18 s**, which is one of the seven figures in that history, and attempt 2 measured
   **7200.08 s (timeout)** as the eighth. What was right in item 3 is narrower than what it claimed:
   in **Release** the test runs at 1220–1324 s and is nowhere near the cap, so no configuration's
   wall time is set by it, and the ~6-hour estimate was wrong because `debug_gui`'s 8 156 s ctest
   dominates — not because of this test.

   Two errors in opposite directions, from the same habit of generalising one configuration's
   measurement to the others. See `regression.md` §3.

## What this cost and what it did not

It cost ~3.5 hours of machine time and produced no evidence. It did **not** corrupt anything: no
source was edited during the run, the partial output is preserved here rather than deleted, and no
figure from it has been reported as a result.

The real regression takes **~35–40 minutes per configuration**, so roughly **2 hours** for all
three including builds — not the 6 hours I told the user twice.
