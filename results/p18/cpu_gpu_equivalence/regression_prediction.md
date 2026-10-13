# P18.8 §57 — the three-configuration regression PREDICTION

**Written and frozen BEFORE the regression was run.** A prediction made after seeing the counts is
not a prediction; the point is that an unexplained difference is a finding rather than something to
rationalise afterwards.

## The three configurations, read from their CMake caches

| Label | Directory | Build type | CUDA | OpenMP |
| --- | --- | --- | --- | --- |
| `release_cuda` | `build/qual-final` | Release | **ON** | ON |
| `cpu_only` | `build/qual-cpuonly` | Release | OFF | ON |
| `debug_cuda` | `build/qual-debug` | Debug | **ON** | OFF |

P18.7 called the third `debug_gui`; its cache shows `CFDAPP_ENABLE_GUI` unset and CUDA **ON**, so it
is relabelled `debug_cuda` here. The label changes; the configuration does not.

## What P18.8 adds

| Test file | Rows | Registers in |
| --- | --- | --- |
| `tests/unit/species/test_device_species_equivalence.cpp` (link one) | 17 | **all three** — deliberately not under `tests/unit/gpu`, so the shared arithmetic is also compared in the CPU-only build |
| `tests/unit/gpu/test_device_species_gpu_equivalence.cpp` (link two) | 11 | CUDA configs only |
| `tests/unit/gpu/test_device_species_a2_dryrun.cpp` | 6 | CUDA configs only |
| `tests/unit/gpu/test_device_species_sanitizer_control.cpp` | 1 active + **3 `DISABLED_`** | CUDA configs only |

So **+17** rows in `cpu_only` and **+17 + 18 = +35** rows in each CUDA config, of which 3 are
`DISABLED_` and must appear in the disabled count, never the passed count.

## The predicted counts

Baseline figures are P18.7's recorded close.

| | selected | passed | failed | skipped | disabled |
| --- | --- | --- | --- | --- | --- |
| `release_cuda` baseline | 3028 | 2977 | 0 | 2 | 49 |
| `release_cuda` **predicted** | **3063** | **3009** | **0** | **2** | **52** |
| `cpu_only` baseline | 2805 | 2758 | 0 | 0 | 47 |
| `cpu_only` **predicted** | **2822** | **2775** | **0** | **0** | **47** |
| `debug_cuda` baseline | 3090 | 3036 | **3** | 2 | 49 |
| `debug_cuda` **predicted** | **3125** | **3068** | **3** | **2** | **52** |

Arithmetic: CUDA configs gain 35 selected, of which 32 are active and 3 disabled, so passed rises
by 32 and disabled by 3. `cpu_only` gains 17 selected, all active, so passed rises by 17 and
disabled is unchanged.

## The three `debug_cuda` failures must reproduce EXACTLY

A known failure stays known only while it reproduces its recorded values (CLAUDE.md §8). Any new
failure, or any change in those three, is a regression and stops the phase.

## Named risks, declared in advance

1. **`DriverSeegmiller.MeshSequence`** has 7 recorded measurements against the unchanged 7200 s cap
   — 7123.03 P, 7201.94 T, 6764.75 P, 7034.57 P, 7200.12 T, 7168.06 P, 6963.18 P — i.e. 5 pass and
   2 timeout, every one within 6 % of the cap. **This run may land on either side and that is not a
   P18.8 regression.** It sets the wall time for any configuration that includes it. P18.9's G10
   must predict a branch; P18.8 records which side this run fell on and nothing more.
2. **The 3 `DISABLED_` sanitizer controls** perform a deliberate out-of-bounds access and a
   deliberate uninitialised read. If any of them appears in the *passed* count rather than the
   disabled count, the registration is wrong and a deliberate defect is running in the routine
   suite.
3. **`cpu_only` is the only configuration that compiles the shared arithmetic header with CUDA
   OFF.** Files reachable only under `if(NOT CFDAPP_ENABLE_CUDA)` are compiled by no other gate in
   this repo, so a failure here would most likely be a missing include rather than numerics.
4. **Every full `ctest` run rewrites tracked `results/validation/**` with timing-only changes**
   (CLAUDE.md §7). Those are runtime noise, must be classified and reverted, and must not be
   presented as a P18.8 change.
