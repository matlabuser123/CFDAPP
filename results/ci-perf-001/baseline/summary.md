# CI-PERF-001 — baseline

Measured from **run 35352132866**, the green exact-SHA qualification of
`c1355eaa77a1b98e37926d20f262b6dae76e45c5` (12/12 success, 2026-09-18 13:45:54Z → 18:22:15Z).
Recorded **before** any optimization, as the phase requires.

Runner: `ubuntu-latest`, 4 vCPU (github-hosted). All times from the GitHub API's own per-job and
per-step timestamps; per-test times from each job's `ctest` output.

## 1. Total workflow wall time

```text
276.3 min  (4 h 36 min)   — set by build-test (clang, debug)
```

## 2. Per-job duration

| Job | Wall | Note |
| --- | ---: | --- |
| **build-test (clang, debug)** | **276.3 min** | **critical path** |
| build-test (gcc, debug) | 230.7 min | |
| sanitizers (shard 2/5) | 101.2 min | worst sanitizer shard |
| sanitizers (shard 4/5) | 93.4 min | |
| sanitizers (shard 1/5) | 92.0 min | |
| sanitizers (shard 3/5) | 72.4 min | |
| sanitizers (shard 5/5) | 65.5 min | best shard — **1.55× spread** |
| build-test (gcc, release) | 38.5 min | |
| clang-tidy | 2.8 min | |
| format | 0.5 min | |
| python | 0.4 min | |
| sanitizer-coverage | 0.1 min | |

## 3. Configure / build / test split

| Job | Install | Configure | Build | Test |
| --- | ---: | ---: | ---: | ---: |
| build-test (clang, debug) | 0.3 min | 0.4 min | **5.5 min** | **270.1 min** |
| build-test (gcc, debug) | 0.1 min | 0.3 min | **3.0 min** | **227.1 min** |
| build-test (gcc, release) | 0.2 min | 0.3 min | **5.0 min** | **32.9 min** |
| sanitizers (per shard) | ~0.2 min | ~0.3 min | **~4.4 min** | 60–96 min |

Dependency/setup overhead (checkout + apt) is ~0.3–0.5 min per job — negligible, and not worth
optimizing.

## 4. Test population and cost

```text
1977 listed = 1932 executed + 45 disabled      (identical in every configuration)

clang debug   16 198 s serial  (270.0 min)   mean  8.38 s
gcc   debug   13 622 s serial  (227.0 min)   mean  7.05 s
asan          82 934 s serial (1382.2 min)   mean 42.9 s
```

The 45 disabled tests are the heavy `DISABLED_` validation cases (Ghia cavity, channel flow, 3D
production, mesh-quality campaign, …). They are listed by `ctest -N`, reported as
`***Not Run (Disabled)`, and deliberately excluded from CI — unchanged by this phase.

## 5. Slowest tests — the sharding floor

No partition can be faster than its heaviest single test.

| Test | clang debug | gcc debug | asan |
| --- | ---: | ---: | ---: |
| `CompressibleCoupledProductionCaseTest.RepeatedRunIsDeterministic` | **890.3 s** | **750.6 s** | **4837.6 s** |
| `NaturalConvectionValidation.GridConvergence` | 831.7 s | 713.7 s | 3423.6 s |
| `SIMPLEMMS.DistortedMesh` | 808.0 s | 658.1 s | 2837.0 s |
| `PoiseuilleValidation.ProductionGridConvergence` | 686.9 s | 576.2 s | 3551.4 s |
| `GradedMeshProductionCase.GradedGridConvergence` | 658.1 s | 541.6 s | 2349.7 s |
| `NaturalConvectionValidation.Grid10x10ConstantProperty…Ra1e3` | 603.5 s | 513.0 s | 3929.0 s |

**Floors:** clang debug 14.8 min, gcc debug 12.5 min, **asan 80.6 min**.

## 6. The two causes of the 4.6 hours

1. **The `build-test` test step ran serially.** `ctest --preset <x> --output-on-failure` with no
   `-j`, and `CMakePresets.json`'s testPresets carry no `jobs` field. The sanitizers job has run the
   same 1977-test population under `-j$(nproc)` for months, at a strictly harsher configuration, so
   parallel execution of this suite was already proven on this runner.

2. **Sharding balanced count, not time.** `ctest -I <shard>,,5` strides over the test index. Against
   a suite with a 106× ratio between the mean and the slowest test, that produced the 65.5–101.2 min
   sanitizer spread: the job pays for the worst shard, so 36 min of every run was pure imbalance.

## 7. Floor analysis — what is achievable

Predicted wall per shard = `max(heaviest single test, shard serial load ÷ 4 vCPU)`:

| Config | shards | serial/shard | predicted wall | floor |
| --- | ---: | ---: | ---: | ---: |
| clang debug | 3 | 90.0 min | **22.5 min** | 14.8 min |
| gcc debug | 3 | 75.7 min | **18.9 min** | 12.5 min |
| gcc release | 1 | 32.9 min | **8.3 min** | 3.2 min |
| asan | 5 | 276.4 min | **80.6 min** | 80.6 min |

**The pipeline floor is the ASan job at 80.6 min**, set entirely by
`CompressibleCoupledProductionCaseTest.RepeatedRunIsDeterministic` (4837.6 s under ASan). At five
shards that job is *already at its floor*: a sixth shard changes nothing, which is why the shard
count stays at 5. Adding build time, the predicted pipeline is **~85 min**, against a 120 min gate.

Going below ~85 min would require making that one test cheaper under ASan — a production change,
explicitly out of scope, and not something to achieve by skipping or weakening it.

## 8. Files

```text
testtimes_clang_debug.txt   1932 per-test times, clang debug
testtimes_gcc_debug.txt     1932 per-test times, gcc debug
testtimes_asan_all.txt      1932 per-test times, union of the 5 asan shards
ci_ctest_full_list.txt      the exact `ctest -N` listing CI produced (1977 lines)
local_parallel_debug.log    local proof the debug suite passes under -j
simulate.sh                 shard-count sweep
check_balance.sh            final predicted balance
```
