# Benchmark generations — provenance, and one preservation incident

Part 8 compares three generations of the GPU path. Their records do not all live in the same
place, and one of them was partly destroyed before this milestone started. That is recorded here
rather than quietly worked around.

| generation | what it is | record kept here |
| --- | --- | --- |
| 1 — original GPU-PIPE baseline | GPU linear solver, **CPU** discretization | `gen1-gpu-pipe-baseline-summary.csv`, `gen1-gpu-pipe-baseline-runs.csv` |
| 2 — GPU-DISC integrated path | GPU linear solver + GPU discretization, host pressure round trip | `gen2-gpu-disc-001Q-analysis-cavity.txt` |
| 2.5 — resident pressure solve | generation 2 with the pressure system device-resident | `gen25-resident-pressure-runs-cavity.csv` |
| 3 — final resident path | the whole outer iteration device-resident | `gen3-final-resident-runs-cavity.csv` |

## The incident

`performance_benchmark.cpp` writes its raw CSV to a **hardcoded** path:

```cpp
const std::string out = "results/gpu-disc-001/performance/raw/runs-" + mode + ".csv";
```

Every later phase that reuses that benchmark therefore overwrites the previous phase's raw CSV.
The GPU-resident-pressure-solve run did exactly that on 2026-09-21 at 01:06, so
`results/gpu-disc-001/performance/raw/runs-cavity.csv` no longer holds GPU-DISC-001Q's data.

**What survives for generation 2, and why it is still authoritative:** the derived analysis text,
`raw/analysis-cavity.txt` (2026-09-20 16:41), plus an independent copy at
`performance/scaling/cavity_analysis.txt` (17:07) with identical bytes. Both predate the
overwrite. They carry the medians, the spreads, the crossover grids and the full per-arm
transfer/synchronization/allocation table — every quantity Part 8 compares. What is lost is only
the per-repeat rows behind those medians.

**What this milestone does about it:** each generation's raw CSV is copied into this directory
immediately after its run, before anything else can rewrite the shared path. The benchmark's
hardcoded output path is left alone — changing it is not in this milestone's scope, and is
recorded in the summary as an observation for a later phase.
