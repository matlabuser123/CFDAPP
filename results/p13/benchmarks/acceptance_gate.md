# P13-007 — Production benchmark suite: acceptance gate

## A note on when this was frozen, because it matters

The audit (`audit.md`) and every baseline in it — including the five-run noise measurement that
sets the threshold — were completed **before any production change**, as the method requires.

This gate file, however, was written **after** the `--output-dir` change was implemented, and that
is a process slip worth recording rather than hiding by back-dating it. What the rule protects
against is criteria tuned to fit results, so the specific mitigation is: **every criterion below is
stated against a baseline measured in the audit, before the change**, and **none of the
verification has been run yet.** The one exception is noted inline at C1, which was exercised
during implementation, and its measured numbers are given so the reader can judge it rather than
take my word.

Stop at the first failure. No threshold weakened; amendments recorded separately.

---

## Group O — output goes where it is told  (DETECTION)

| id | criterion | measured baseline (audit) |
| --- | --- | --- |
| O1 | Every file-writing benchmark accepts `--output-dir <path>` and writes there. | 0 of 5 |
| O2 | A run with `--output-dir` leaves the repository's tracked evidence **byte-identical**. | 34 tracked files overwritable |
| O3 | `--output-dir` with no argument exits nonzero rather than silently using the default. | flag did not exist |
| O4 | An uncreatable output directory exits nonzero rather than writing nowhere. | flag did not exist |
| O5 | With no arguments, a benchmark writes exactly where it always did. | PRESERVATION |

O5 is deliberate: changing the default would invalidate every documented recipe and make recorded
results wrong about their own provenance.

## Group R — a regression can actually be reported  (DETECTION)

| id | criterion | measured baseline (audit) |
| --- | --- | --- |
| R1 | A comparison of a run against a baseline exists and returns a verdict. | 0 benchmarks could fail |
| R2 | It compares only metrics that survive this machine's noise, admitted by a **dimensional floor**, not a stretched tolerance. | n/a |
| R3 | A run against an identical-binary baseline **passes**. | n/a |
| R4 | A deliberately slowed run **fails**, naming the metric and the ratio. | n/a |
| R5 | Comparing zero metrics is a **failure**, never a pass. | n/a |
| R6 | A current run missing a baseline case is a **failure**, not a skip. | n/a |
| R7 | The threshold is quoted against measured noise, and its blind spot is stated in the tool itself. | worst spread 7.9% |

**Threshold, derived not chosen:** worst measured run-to-run spread on an admitted metric is
**7.9%** (20x20 `runtime_per_iteration_ms`; 5.2% at 40x40, 3.5% at 80x80, over five identical
runs). The threshold is **1.25x**, roughly 3x that worst case. It cannot detect a regression
smaller than 25%, and the tool says so in its own documentation.

## Group S — a suite, not seven programs  (DETECTION)

| id | criterion | measured baseline (audit) |
| --- | --- | --- |
| S1 | One driver runs the benchmark suite into a single directory and returns a verdict. | no driver existed |
| S2 | The driver captures the environment alongside the results. | only within one benchmark |
| S3 | The suite is reachable from `ctest` by a fast smoke entry, so it cannot rot unnoticed. | 0 benchmarks registered |
| S4 | The smoke entry does **not** run the long benchmarks. | n/a |

## Group E — the baseline describes this program  (DETECTION)

| id | criterion | measured baseline (audit) |
| --- | --- | --- |
| E1 | A baseline is recorded on this machine from the current build, with its environment. | committed baseline is 0.1.0-dev, OpenMP off, CUDA off |
| E2 | Comparing across different build configurations is **refused**, not silently performed. | n/a |

E2 matters more than it looks: comparing a CUDA+OpenMP build against a serial one and calling the
difference a regression is worse than not comparing at all.

## Group P — preservation

| id | criterion |
| --- | --- |
| P1 | No benchmark's measurement changes. Changing what is measured and then comparing to a baseline makes the comparison meaningless. |
| P2 | The full regression passes: Release+CUDA, Debug+GUI, CPU-only, clang-format-18 at CI scope. |
| P3 | The two benchmarks that write no files (`spmv_scaling`, `gpu_spmv`) are left alone. |

## Group NV — non-vacuity controls  (each MUST fail)

| id | control | must break |
| --- | --- | --- |
| NV1 | Ignore `--output-dir` and write to the default | O1, O2 |
| NV2 | Slow a benchmark deliberately (inflate a recorded metric) | R4 |
| NV3 | Point the comparator at a baseline with no comparable metric | R5 |
| NV4 | Delete a case from the current run | R6 |
| NV5 | Compare a CUDA build's run against a serial baseline | E2 |
| NV6 | Raise the threshold above the injected slowdown | R4 must stop firing — proving the threshold, not the plumbing, is what detects |

---

## Dry-run record, measured on the unchanged tree (from `audit.md`)

```text
B1  benchmarks accepting an output path           0 of 5   -> all 5
B2  tracked files overwritable by a run          34        -> 0
B3  benchmarks reachable from one entry point     0        -> ctest smoke + driver
B4  benchmarks that can report a failure          0        -> a comparator that returns a verdict
B5  baseline build identity          0.1.0-dev, no OpenMP  -> measured on this machine
B7  worst run-to-run spread, admitted metric    7.9%       -> threshold 1.25x
```

Every row has a measured baseline differing from its target. P1–P3 are PRESERVATION and already
pass, which is correct for that kind and is not counted as an achievement.

**Already exercised during implementation, recorded here rather than claimed fresh:**

```text
O1  --output-dir <dir>      4 files written to the given directory, exit 0
O3  --output-dir            exit 2, "Error: --output-dir requires a path"
O4  uncreatable path        exit 2, "could not create output directory ... No such file or directory"
R3  identical-binary run    3 metrics compared, 1.00x / 0.99x / 1.04x, PASS
```

## Gate verdict

P13-007 closes only when O, R, S, E and P pass and every NV control fails.
