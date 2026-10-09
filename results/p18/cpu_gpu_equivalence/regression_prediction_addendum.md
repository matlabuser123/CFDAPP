# P18.8 §57 — regression prediction, ADDENDUM 1

`regression_prediction.md` is **not edited**. It stands at sha256
`ab941cf5f7cc08766ccd1c96a68e0618a90b712ee21364455cbf59b09dc1dc69`.

**Why an addendum.** That prediction was frozen, then I added **two tests** while closing coverage
gaps that `coverage_map.md` exposed — `E1_EveryCompositionCaseInTheFrozenListIsBitwiseOnDevice` and
`N10_ADifferentTimestepProducesADifferentTrajectory` (see `acceptance_gate_A2_erratum.md` §3). The
original row arithmetic is therefore two short per CUDA configuration.

**This is still a prediction, not a reconciliation.** It is written while the regression is running
and **before any configuration has reported a count**: the run was launched at 2026-10-02T03:46:40Z
and no `ctest` summary exists yet. The row counts below come from `--gtest_list_tests` on the two
binaries, which enumerates registrations without running anything.

Also recorded: the partial first launch of the regression was **killed and discarded** before any
configuration completed, precisely because editing test sources while the harness builds from the
working tree would have meant different configurations compiled from different sources. The run
being predicted here is the relaunch from a stable tree.

## Measured registrations

`gtest_discover_tests` registers one `ctest` row per gtest test.

| Binary | P18.8 rows | of which `DISABLED_` | active |
| --- | --- | --- | --- |
| `CFDSpeciesTests` (link one) | **17** | 0 | 17 |
| `CFDGpuTests` (link two + dry-run + E11 + controls) | **23** | **3** | 20 |

The 23 break down as `P188GpuEquivalence` 13, `P188A2DryRun` 6, `P188DeviceInvalidState` 1,
`P188SanitizerControl` 3 (all `DISABLED_`).

## The corrected prediction

| | selected | passed | failed | skipped | disabled |
| --- | --- | --- | --- | --- | --- |
| `release_cuda` baseline (P18.7) | 3028 | 2977 | 0 | 2 | 49 |
| `release_cuda` **predicted** | **3068** | **3014** | **0** | **2** | **52** |
| `cpu_only` baseline | 2805 | 2758 | 0 | 0 | 47 |
| `cpu_only` **predicted** | **2822** | **2775** | **0** | **0** | **47** |
| `debug_cuda` baseline | 3090 | 3036 | **3** | 2 | 49 |
| `debug_cuda` **predicted** | **3130** | **3073** | **3** | **2** | **52** |

Arithmetic: a CUDA configuration gains `17 + 23 = 40` rows, of which 37 are active and 3 disabled,
so passed rises by 37 and disabled by 3. `cpu_only` gains 17, all active.

Superseded figures from the original prediction, kept visible so the change is auditable: 3063 /
3009 for `release_cuda` and 3125 / 3068 for `debug_cuda`.

## The named risks are unchanged

All four risks in the original prediction stand, in particular:

1. **`DriverSeegmiller.MeshSequence`** — 7 recorded measurements against the unchanged 7200 s cap, 5
   pass and 2 timeout, every one within 6 %. Either outcome here is **not** a P18.8 regression; it
   sets the wall time and P18.8 records only which side this run fell on.
2. **The 3 `DISABLED_` controls must appear in the disabled count, never the passed count.** They
   perform a deliberate out-of-bounds access and a deliberate uninitialised read.
3. `cpu_only` is the only configuration that compiles the shared header with CUDA **OFF**.
4. A full `ctest` rewrites tracked `results/validation/**` with timing-only changes; that is runtime
   noise, to be classified and reverted, never presented as a P18.8 change.
