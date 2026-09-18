# CI-PERF-001 — faster CI

**Status: ✅ COMPLETE — qualified on the real CI run.**

```text
commit      548401aef0d3cbab9552b1964ceac7efd7ad6154
run         35385979133   exact-SHA match, 17/17 success
baseline    276.3 min  ->  measured 100.6 min     2.75x, 63.6% reduction
gate        < 120 min  ->  PASS with 19.4 min margin
tests       1977 listed / 1932 executed / 45 disabled, unchanged
```

Full qualification evidence: [`qualification/summary.md`](qualification/summary.md).

Baseline: `c1355eaa77a1b98e37926d20f262b6dae76e45c5`, run 35352132866 (12/12 green).
No production CFD, numerics, CUDA, test or threshold behaviour was changed.

## 1. Baseline

Full record in [`baseline/summary.md`](baseline/summary.md). In short:

```text
total wall           276.3 min (4 h 36 min)   critical path: build-test (clang, debug)
clang debug          build   5.5 min + test 270.1 min
gcc   debug          build   3.0 min + test 227.1 min
gcc   release        build   5.0 min + test  32.9 min
sanitizers ×5        build ~4.4 min + test  60-96 min   (65.5-101.2 min wall, 1.55x spread)
clang-tidy 2.8 · format 0.5 · python 0.4 · sanitizer-coverage 0.1

test population      1977 listed = 1932 executed + 45 disabled
```

## 2. The two causes — and neither was "too many tests"

1. **`build-test` ran its tests serially.** `ctest --preset <x> --output-on-failure` with no `-j`,
   and `CMakePresets.json`'s testPresets carry no `jobs` field. 270 minutes of serial tests on a
   4-vCPU runner. The sanitizers job has run the *same* 1977-test population under `-j$(nproc)` for
   months at a strictly harsher configuration, so parallel execution was already proven here.

2. **Sharding balanced count, not time.** `ctest -I <shard>,,5` strides over the test index. The
   suite's slowest test is 106× the mean, so striding produced the 65.5–101.2 min sanitizer spread;
   the job pays for the worst shard, making ~36 min of every run pure imbalance.

## 3. What changed

**`.github/workflows/ci.yml`**

* `build-test` becomes a 7-entry matrix: `gcc-release` ×1, `gcc-debug` ×3, `clang-debug` ×3.
* Its test step gains `-j$(nproc)` and `--timeout 7200`. The timeout is **new** and is a hang guard
  only — this job previously had none, so a deadlock consumed the full 6 h job limit before failing.
* Shard selection is by **measured runtime**, via `scripts/ci/shard_tests.py`, emitted as an explicit
  `ctest -I 0,0,0,<indices>` list.
* `sanitizers` keeps 5 shards but switches from stride to the same runtime-balanced assignment.
  No sanitizer flag, option, timeout, tolerance or enabled/disabled-test change.
* New `build-test-coverage` job auditing the debug/release partitions.
* `sanitizer-coverage` now calls the same shared audit — strictly stronger than the inline check it
  replaces (it additionally fails an empty shard and a selected-but-never-executed enabled test).
* ccache on every compiling job, keyed per compiler+build-type.

**`scripts/ci/`** — `shard_tests.py` (LPT assignment), `audit_shards.py` (coverage audit),
`build_runtime_table.py`, `test_runtimes.json` (the measured table),
`selftest_audit.sh` and `selftest_sharding.sh` (negative controls).

## 4. Why runtime balancing, and how coverage stays guaranteed

Assignment is longest-processing-time-first greedy. Coverage is **structural, not statistical**:

* every test in the **live** `ctest -N` list is assigned — the table only sets *weights*, never
  membership, so a stale table costs balance and never coverage;
* a test with no recorded runtime still gets assigned (median weight), so a newly added test cannot
  be silently dropped;
* the parse **fails closed**: a second, looser regex counts `Test #N:` lines independently, and any
  disagreement aborts rather than sharding an incomplete list;
* CI proves the partition from what the shards actually recorded.

### A defect this caught in its own implementation

The first version of `shard_tests.py` matched test names with `(?P<name>\S+)\s*$`. `ctest -N` prints
a disabled test as `Test #205: Foo.Bar (Disabled)`, so that pattern matched **1932 of 1977** lines
and silently dropped all 45 disabled tests — **and `--check` reported the partition COMPLETE**,
because the checker used the same regex as the assigner. A vacuous check of exactly the kind
`CLAUDE.md` §5.2 exists to prevent. Fixed by capturing the full trailing name, giving disabled tests
weight 0, and adding the independent line counter above.

A second defect followed from the fix: with weight 0, a disabled test never raises its bucket's
load, so that bucket stayed the minimum and absorbed every remaining zero-weight test. Time balance
was still 1.00×, but the counts were absurd. Fixed by breaking ties on test count, then index.

## 5. Predicted result — and where the prediction was wrong

Recorded as written before the run, because two of its assumptions did not hold:

* it assumed wall = serial ÷ 4 vCPU; the measured parallel efficiency was **2.4–3.6×**, not 4×;
* it assumed equal serial load gives equal wall time, which is false under `-j` — a shard's wall
  time is its parallel makespan, bounded below by its longest single test.

Predicted ~85 min; **measured 100.6 min**. The direction and the mechanism were right, the
arithmetic was optimistic. The measured run is the evidence; see
[`qualification/summary.md`](qualification/summary.md).

Predicted wall per shard = `max(heaviest single test, serial load ÷ 4 vCPU)` + build:

| Job | Baseline | Predicted | |
| --- | ---: | ---: | --- |
| clang debug | 276.3 min | **~28 min** | 3 shards, 90.0 min serial each |
| gcc debug | 230.7 min | **~22 min** | 3 shards, 75.7 min serial each |
| gcc release | 38.5 min | **~13 min** | 1 shard, parallel |
| sanitizers | 101.2 min | **~85 min** | 5 shards, 276.4 min serial each |
| **total wall** | **276.3 min** | **~85 min** | **≈3.2× faster** |

Measured shard balance, against the exact `ctest -N` listing CI produced:

```text
clang_debug  3 shards   90.0 min each   spread 1.00x   floor 14.8 min
gcc_debug    3 shards   75.7 min each   spread 1.00x   floor 12.5 min
asan         5 shards  276.4 min each   spread 1.00x   floor 80.6 min
```

**The pipeline floor is 80.6 min**, set entirely by
`CompressibleCoupledProductionCaseTest.RepeatedRunIsDeterministic` (4837.6 s under ASan). At five
shards the sanitizer job is already *at* that floor — a sixth shard changes nothing, which is why
the count stays at 5. Beating ~85 min would mean making that one test cheaper under ASan: a
production change, out of scope, and not something to obtain by skipping or weakening it.

## 6. Verification performed

| Check | Result |
| --- | --- |
| Workflow YAML parses; 7 job definitions → 17 jobs | **PASS** |
| Every job invoking `scripts/ci/` has `actions/checkout` | **PASS**, 7/7 |
| Shard partition, clang_debug / gcc_debug / asan, against CI's real 1977-test listing | **COMPLETE**, 1.00× spread, 0 missing / 0 duplicate / 0 unexpected / 0 empty |
| **Sharding mechanism end to end vs real `ctest`** | **PASS** — ctest accepted a 7753-char index list and selected exactly the requested indices; union = 2029 live tests, 0 duplicates, 0 missing, 0 unexpected |
| **Audit negative control** (`selftest_audit.sh`) | **7/7** — passes a correct partition; **fails all six injected defects**: missing test, duplicate assignment, unexpected test, empty shard, full-list disagreement, silent non-execution |
| Executed-test capture vs the real release log | **exact** — records 1932, not 1977; all 1932 enabled tests present, 0 missing |
| Local full Debug suite under `-j` | see below |
| Production CFD / numerics / CUDA / tests / thresholds changed | **none** |

### The one assumption the whole speedup rests on

That the Debug suite passes under parallel `ctest`. Two independent pieces of evidence:

1. CI already runs the same 1977-test population under `-j$(nproc)` in the **sanitizers** job, at a
   strictly harsher configuration (ASan/UBSan memory and timing overhead), and it passed 12/12 on
   `c1355ea`.
2. A local full `ctest --preset debug -j16` run, on the rebuilt `c1355ea` tree:

```text
100% tests passed, 0 tests failed out of 1984      (1984 executed + 45 disabled = 2029 listed;
Total Test time (real) = 1131.48 s (18.9 min)       the local build has GUI ON, CI has it OFF)
```

Serial cost of the same suite is 13 622 s, so `-j16` gave **12.0×** here — and, more to the point,
**zero failures**: no timing-sensitive test, no shared-file contention, no flake.
Log: [`baseline/local_parallel_debug.log`](baseline/local_parallel_debug.log).

### Generated-file audit, and the race hypothesis it had to rule out

The run rewrote 50 tracked `results/validation/**` reports. The alternative explanation that had to
be excluded was serious: *parallel execution racing on shared report files*, which would make the
whole recommendation unsafe. It is ruled out on three independent grounds:

* **50 files is exactly what the recorded SERIAL Debug regression rewrote**
  (`results/gpu-pcorr-001/summary.md` §8). Parallelism changed neither which files are rewritten nor
  how many.
* **Every difference is at the round-off floor.** Non-timing values above 1e-6: max relative
  difference **1.815e-05**, at `extrapolated_error` — a Richardson extrapolation, which amplifies
  round-off by construction. Above 1e-3: **3.378e-06**. A race corrupts or grossly perturbs; it does
  not produce round-off-magnitude noise in precisely the recorded file set.
* **All 1984 tests passed**, including the validation tests that gate these very numbers against
  their committed values. Every numerical gate held at these differences.

Filenames captured **before** restoring (a gap disclosed twice in earlier phases) in
[`baseline/generated_files_audit.md`](baseline/generated_files_audit.md); all 50 then restored, and
`results/validation` is clean. Nothing was committed.

## 7. What remains — why this is BLOCKED, not PASS

| Gate item | State |
| --- | --- |
| all required tests preserved | **MET** — 1977 listed / 1932 executed, unchanged; nothing removed, skipped or reordered |
| debug sharding verified | **MET** — partition and mechanism both proven against real ctest |
| sanitizer coverage preserved | **MET** — same 5-shard population, stronger audit |
| coverage audit PASS | **MET** — and non-vacuous, 6/6 mutants detected |
| no weakened gates | **MET** — no tolerance, threshold, sanitizer option or timeout weakened; the one timeout added is new and is a hang guard |
| no hidden/skipped failures | **MET** — `set -o pipefail`, `fail-fast: false`, audits run `if: always()`, `***Not Run`/`***Skipped` excluded from the executed set |
| CI reproducible | **MET** — deterministic assignment; ccache keyed per compiler+build-type and content-hashed |
| **actual optimized CI evidence recorded** | **NOT MET — requires a push, which is not authorized** |
| **target <2 h achieved** | **NOT MET — predicted ~85 min, not measured** |

Per the phase's own instruction, the gate is **not weakened to fit**: CI-PERF-001 remains `[ ]`, the
measured and predicted results are recorded above, and the remaining bottleneck is named — the
80.6-minute ASan cost of a single test.

**The only thing needed to finish: authorization to commit and push, then one CI run to measure.**
