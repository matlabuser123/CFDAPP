# CI-PERF-001 — exact-SHA CI qualification

**PASSED.** Measured on the real run; no predicted value is used as evidence.

```text
commit SHA        548401aef0d3cbab9552b1964ceac7efd7ad6154
workflow run      35385979133
run head SHA      548401aef0d3cbab9552b1964ceac7efd7ad6154   -> exact match: YES
start             2026-09-18T19:26:50Z
finish            2026-09-18T21:07:25Z
total wall time   100.6 min
conclusion        success   (17/17 jobs, 0 failures)
```

## 1. Result against the gate

| | |
| --- | ---: |
| Baseline (run 35352132866) | **276.3 min** |
| Qualification (run 35385979133) | **100.6 min** |
| Speed-up | **2.75×** |
| Wall-time reduction | **63.6 %** |
| Gate | **< 120 min — PASS**, 19.4 min margin |

## 2. Every job

| Job | Wall | Result |
| --- | ---: | --- |
| sanitizers (shard 4/5) | **100.3 min** | success — **sets the run's wall time** |
| sanitizers (shard 5/5) | 95.1 min | success |
| sanitizers (shard 1/5) | 93.2 min | success |
| sanitizers (shard 2/5) | 82.2 min | success |
| sanitizers (shard 3/5) | 61.4 min | success |
| build-test (gcc-debug 1/3) | 46.9 min | success |
| build-test (gcc-debug 2/3) | 45.4 min | success |
| build-test (gcc-debug 3/3) | 45.0 min | success |
| build-test (clang-debug 1/3) | 43.7 min | success |
| build-test (clang-debug 3/3) | 38.6 min | success |
| build-test (clang-debug 2/3) | 21.0 min | success |
| build-test (gcc-release 1/1) | 19.9 min | success |
| clang-tidy | 4.6 min | success |
| python | 0.5 min | success |
| format | 0.4 min | success |
| build-test-coverage | 0.1 min | success |
| sanitizer-coverage | 0.1 min | success |

Per-job against baseline:

```text
clang debug    276.3 min (1 job)  ->  43.7 min worst of 3   6.3x
gcc   debug    230.7 min (1 job)  ->  46.9 min worst of 3   4.9x
gcc   release   38.5 min          ->  19.9 min              1.9x
sanitizers     101.2 min worst    -> 100.3 min worst        1.01x  (floor-bound)
```

## 3. Test counts — preserved exactly

Every configuration executed the same population as the baseline:

| Config | Shards | Executed per shard | Total executed | Listed | Disabled |
| --- | --- | --- | ---: | ---: | ---: |
| clang-debug | 3 | 1630 + 151 + 151 | **1932** | 1977 | 45 |
| gcc-debug | 3 | 896 + 136 + 900 | **1932** | 1977 | 45 |
| gcc-release | 1 | 1932 | **1932** | 1977 | 45 |
| asan | 5 | 387 + 387 + 386 + 386 + 386 | **1932** | 1977 | 45 |

**1977 listed / 1932 executed / 45 disabled**, unchanged from the baseline. Every job reported
`100% tests passed, 0 tests failed`. No test was removed, skipped, reordered or newly disabled.

## 4. Coverage audits — both PASS

As CI printed them:

```text
build-test-coverage    clang-debug shard coverage: COMPLETE, each test in exactly one shard
                       gcc-debug   shard coverage: COMPLETE, each test in exactly one shard
                       gcc-release shard coverage: COMPLETE, each test in exactly one shard
sanitizer-coverage     asan        shard coverage: COMPLETE, each test in exactly one shard
```

Re-audited **independently** on this machine from the downloaded artifacts
(`audit_independent.txt`): all four groups `full list 1977; union 1977 (1977 distinct)`,
`executed 1932 of 1932 enabled`. Zero missing, zero duplicated, zero unexpected, zero empty shards,
and no enabled test selected but never reported a result.

The audit is non-vacuous — `scripts/ci/selftest_audit.sh` detects all six injected defect classes.

## 5. Shard balance — measured

```text
sanitizers    61.4 / 82.2 / 93.2 / 95.1 / 100.3 min   spread 1.63x
gcc-debug     45.0 / 45.4 / 46.9 min                  spread 1.04x
clang-debug   21.0 / 38.6 / 43.7 min                  spread 2.08x
```

**A limitation worth recording, found by this run.** The assignment balances the *sum* of per-test
runtimes, and that sum is balanced to 1.00× on every configuration. But a shard's wall time under
`-j` is its **parallel makespan**, which is bounded below by its longest single test — so equal
serial load does not give equal wall time. gcc-debug happened to land at 1.04×; clang-debug at
2.08×; the sanitizer spread improved only from 1.55× to 1.63× — i.e. **not improved**.

The sanitizer job is floor-bound regardless: `CompressibleCoupledProductionCaseTest.
RepeatedRunIsDeterministic` costs 4838 s (80.6 min) under ASan and no partition can split one test.
Its shard cannot finish sooner than that, which caps the whole pipeline at ~85 min however the rest
is arranged. Balancing by makespan rather than by sum is the correct next refinement, and it would
help the two debug configurations, not the sanitizer floor.

## 6. ccache — cold, and it contributed nothing to this result

```text
Hits:               2 / 382 ( 0.52%)
Misses:           380 / 382 (99.48%)
Cacheable calls:  382 / 382 (100.0%)      — identical in all 7 build-test jobs
```

This was the **first** run with ccache, so every cache was cold, exactly as expected. The 2.75×
is therefore attributable **entirely to parallel test execution and runtime-balanced sharding**, not
to caching. 100 % of compiler calls were cacheable, so a subsequent run on an unchanged tree should
see a high hit rate — but that is a prediction, not evidence, and is not claimed here.

Correctness conditions hold: keys are per compiler and build type, ccache hashes the preprocessed
source, compiler binary and full flag set, and a miss compiles normally — so the cache can make a
build faster, never different.

## 7. Gate checklist

| Requirement | Result |
| --- | --- |
| exact SHA matches | **YES** — 548401aef0d3… run head == commit |
| all required jobs PASS | **YES** — 17/17, 0 failures |
| all required tests preserved | **YES** — 1977 / 1932 / 45 in every configuration |
| runtime-balanced sharding works | **YES** — all four partitions exact; serial-load spread 1.00× |
| coverage audit PASS | **YES** — both jobs, plus an independent re-audit |
| sanitizer coverage preserved | **YES** — same 5 shards, 1932 executed, stronger audit |
| no weakened tests/thresholds/gates | **YES** — no tolerance, sanitizer option or timeout weakened; the one added timeout is a new hang guard |
| no hidden failures | **YES** — `fail-fast: false`, `set -o pipefail`, audits `if: always()`, `***Not Run`/`***Skipped` excluded from the executed set |
| total CI wall time < 120 min | **YES — 100.6 min** |

**CI-PERF-001 PASSES.**
