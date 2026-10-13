# P17.0 — baseline CPU regression: CPU-only

**PASS — 2297 / 2297, 0 failed. Every frozen prediction matched exactly.**

Run 2026-09-26. Gate: `baseline_gate.md`, frozen before this ran,
sha256 `a147f79c4dc2aac21bae6a39f5afa135cf15921545c32c4537938c3f08c7dfbd`.

---

## Configuration and invocation

```text
tree          build/qual-cpuonly
              CMAKE_BUILD_TYPE=Release  CFDAPP_ENABLE_CUDA=OFF
              CFDAPP_BUILD_GUI=OFF      CFDAPP_ENABLE_OPENMP=ON
invocation    OMP_NUM_THREADS=1 ctest -j 8 --timeout 7200 -E <5 frozen exclusions>
exclusions    the same 5 patterns / 13 tests as P13 G10, verbatim, not widened
```

## Result against the frozen prediction

```text
quantity                     PREDICTED   MEASURED   verdict
------------------------------------------------------------
registered                     2357        2357     MATCH
matching the exclusions          13          13     MATCH
disabled (unique)                47          47     MATCH
executed                       2297        2297     MATCH
failed                            0           0     MATCH
IdealGasEOSTest rows             21          21     MATCH
IdealGasEOSTest rows Passed      21          21     MATCH
```

```text
ctest exit status   0
summary line        100% tests passed, 0 tests failed out of 2297
Not Run             0
runtime             1720.71 s of test time; 1720 s wall (28.7 min)
                    [WSL wall clock -- indicative, not performance evidence]
```

The arithmetic closes exactly:

```text
2357 registered  -  13 excluded  -  47 disabled  =  2297 executed     exact
```

Note the disabled count is **47** here against **49** in Release+CUDA — the two
`DISABLED_` GPU rows that do not exist in a CUDA-off build. Predicted as 47 from G10's
own 94 raw count, and matched. A single instrument that reported the same disabled
number for both configurations would have been suspect.

## Why the prediction was falsifiable

P13 G10 measured 2288 executed here. P17.2's 9 new EOS tests are plain unit tests that
build in every configuration, so `2288 + 9 = 2297` was predicted before the run. A
result of 2288 would have proven the CPU-only binary was stale — and `build/qual-cpuonly`
had **not** been rebuilt since G10 (P17.2 used `build/qual-final` only), so this
configuration is where a staleness problem would actually have shown up.

All 21 `IdealGasEOSTest` rows executed and Passed, the same 12 + 9 decomposition
recorded in `cuda_regression.md`.

## Integrity — CLAUDE.md §8

```text
build                    exit 0 (3 s)
newest production source 2026-09-26T03:44:26Z  src/compressible/IdealGasEOS.cpp
built test binaries      45, from ctest's own registry
oldest built binary      2026-09-26T04:15:30Z  tests/unit/core/CFDCoreTests
stale built binaries     0        -> all 45 are newer than the newest source
external tools           1: /usr/bin/cmake (2024-02-03), listed, not staleness-checked
```

**45 built binaries here against 46 in Release+CUDA.** The missing one is the GPU test
target, which does not exist in a CUDA-off build. That one-binary difference is a
genuine signal that the two registries really are different trees and not the same one
measured twice.

## B6 — CPU-only has no CUDA dependency

```text
nvcc invocations in the build log    0
libcudart references in the build    0
cuda/libcfdcuda.a present            no
ldd build/qual-cpuonly/apps/cli/cfdapp | grep -c cudart    0
```

All four independent checks agree: this configuration neither compiles nor links
anything CUDA. This is the check that would catch a CUDA dependency leaking into the
CPU-only build — relevant because `results/p17/` work is about to add compressible
code, and the CPU-only build is the one gate that compiles files guarded by
`if(NOT CFDAPP_ENABLE_CUDA)`.

## Fingerprints across the run — B8

```text
P17_CODE_FINGERPRINT before  1e307fcd7c70104d02da8adfb226dfd5641a6c8e880fc8169ce43b6ca070c5d2
P17_CODE_FINGERPRINT after   1e307fcd7c70104d02da8adfb226dfd5641a6c8e880fc8169ce43b6ca070c5d2
                             UNMOVED
```

## Logs

```text
driver   logs/10_baseline_regress_mandated.log
build    logs/10_cpu_only_build.log
ctest    logs/10_cpu_only_ctest.log
harness  tools/p17_baseline_regress.sh
```
