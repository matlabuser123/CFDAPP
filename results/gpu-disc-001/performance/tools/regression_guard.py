#!/usr/bin/env python3
"""GPU-DISC-001Q -- performance-regression guard.

Records a baseline of the metrics that matter and compares a later run against
it. Two classes of metric, deliberately treated differently:

  EXACT      transfer counts, byte counts, allocation counts, synchronization
             counts, kernel counts, outer iterations. These are deterministic
             -- they do not vary between runs of the same build on the same
             case. Any change is a real change and is reported at a 0%
             threshold.

  TIMING     wall seconds and per-stage seconds. These carry genuine machine
             noise. The threshold is derived from OBSERVED spread in this
             gate's own data, not invented.

The distinction is the point. A guard that puts a loose timing threshold on an
exact counter would miss a doubling of per-iteration transfers; a guard that
puts a tight threshold on wall time would cry wolf on every run.

Usage:
  regression_guard.py record   <runs.csv> [baseline.json]   -- write a baseline
  regression_guard.py check    <runs.csv> [baseline.json]   -- compare to it

Exit codes: 0 pass, 1 regression detected, 2 usage/data error.

CI note: this needs a GPU, so it is a LOCAL qualification tool. The `record`
side produces a plain JSON file that a CPU-only CI job can still diff for
unexpected edits, and the `check` side runs wherever a GPU exists.
"""

import csv
import json
import statistics as st
import sys
from pathlib import Path

ROOT = Path("/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp")
DEFAULT_BASELINE = ROOT / "results/gpu-disc-001/performance/regression-guard/baseline.json"

# Metrics compared exactly. A single differing count is a regression.
EXACT = ["outer_iterations", "h2d_calls", "h2d_bytes", "d2h_calls", "d2h_bytes",
         "allocations", "reallocations", "kernels", "syncs"]

# Timing metrics, with the threshold applied to the MEDIAN.
TIMING = ["wall_seconds", "discretization_s", "linear_solve_s"]

# Derived from this gate's own measured spread. The worst per-point spread
# observed across the cavity series was 24.9% (20x20 gpu-pipe, where the run is
# short and startup jitter dominates); typical spread was 2-9%. A 35% threshold
# sits clear of the worst observed noise while still catching the kind of
# regression worth catching -- a stage that doubles, a path that silently falls
# back to the CPU, an optimisation that is quietly reverted.
#
# This is a LOCAL QUALIFICATION threshold and an informational one. It is not a
# release gate, and it is deliberately broad: the project has no pre-existing
# performance-threshold policy, so inventing a tight one here would manufacture
# false failures rather than catch real regressions.
TIMING_THRESHOLD = 0.35

# Small absolute floor: below this, percentage change is meaningless noise.
TIMING_FLOOR_SECONDS = 0.05


def load(path):
    with open(path) as f:
        return list(csv.DictReader(f))


def key_of(row):
    return f"{row['case']}|{row['grid']}|{row['arm']}|{row['scheme']}"


def summarise(rows):
    """Median timings and exact counters per (case, grid, arm, scheme)."""
    groups = {}
    for r in rows:
        groups.setdefault(key_of(r), []).append(r)
    out = {}
    for k, g in groups.items():
        entry = {"repeats": len(g), "cells": int(g[0]["cells"])}
        for m in TIMING:
            entry[m] = st.median([float(r[m]) for r in g])
        for m in EXACT:
            vals = {int(float(r[m])) for r in g}
            # A counter that is NOT identical across repeats is itself a finding
            # -- it means the metric is not deterministic and cannot be guarded
            # exactly. Recorded as a list so `check` can say so.
            entry[m] = sorted(vals)[0] if len(vals) == 1 else sorted(vals)
        out[k] = entry
    return out


def record(csvPath, baselinePath):
    data = summarise(load(csvPath))
    baselinePath.parent.mkdir(parents=True, exist_ok=True)
    baselinePath.write_text(json.dumps(
        {"source": str(csvPath.name), "timing_threshold": TIMING_THRESHOLD,
         "timing_floor_seconds": TIMING_FLOOR_SECONDS,
         "exact_metrics": EXACT, "timing_metrics": TIMING, "points": data}, indent=2))
    nonDeterministic = [(k, m) for k, e in data.items() for m in EXACT
                        if isinstance(e[m], list)]
    print(f"recorded {len(data)} benchmark points -> {baselinePath}")
    print(f"  timing threshold {TIMING_THRESHOLD:.0%} (median), floor "
          f"{TIMING_FLOOR_SECONDS}s; exact metrics at 0%")
    if nonDeterministic:
        print(f"  !! {len(nonDeterministic)} counters varied across repeats and cannot be "
              f"guarded exactly:")
        for k, m in nonDeterministic[:10]:
            print(f"       {k}  {m} = {data[k][m]}")
    else:
        print("  every exact counter was identical across repeats -- all guardable")
    return 0


def check(csvPath, baselinePath):
    if not baselinePath.exists():
        print(f"no baseline at {baselinePath} -- run `record` first")
        return 2
    base = json.loads(baselinePath.read_text())
    now = summarise(load(csvPath))
    threshold = base.get("timing_threshold", TIMING_THRESHOLD)
    floor = base.get("timing_floor_seconds", TIMING_FLOOR_SECONDS)

    regressions, improvements, missing, exactChanges = [], [], [], []
    for k, b in base["points"].items():
        if k not in now:
            missing.append(k)
            continue
        n = now[k]
        for m in base.get("exact_metrics", EXACT):
            if isinstance(b.get(m), list) or isinstance(n.get(m), list):
                continue  # not deterministic; `record` already flagged it
            if b.get(m) != n.get(m):
                exactChanges.append((k, m, b.get(m), n.get(m)))
        for m in base.get("timing_metrics", TIMING):
            bv, nv = b.get(m, 0.0), n.get(m, 0.0)
            if bv < floor and nv < floor:
                continue
            if bv <= 0:
                continue
            delta = (nv - bv) / bv
            if delta > threshold:
                regressions.append((k, m, bv, nv, delta))
            elif delta < -threshold:
                improvements.append((k, m, bv, nv, delta))

    print(f"compared {len(now)} points against {baselinePath.name}")
    print(f"  timing threshold {threshold:.0%}, floor {floor}s; exact metrics at 0%")

    if missing:
        print(f"\n  {len(missing)} baseline points are MISSING from this run:")
        for k in missing[:10]:
            print(f"    {k}")

    if exactChanges:
        print(f"\n  EXACT-COUNTER CHANGES ({len(exactChanges)}) -- deterministic metrics moved:")
        for k, m, bv, nv in exactChanges[:20]:
            pct = 100.0 * (nv - bv) / bv if bv else float("inf")
            print(f"    {k:46s} {m:16s} {bv} -> {nv}  ({pct:+.1f}%)")

    if regressions:
        print(f"\n  TIMING REGRESSIONS ({len(regressions)}) -- slower than baseline by "
              f">{threshold:.0%}:")
        for k, m, bv, nv, d in regressions:
            print(f"    {k:46s} {m:18s} {bv:8.3f} -> {nv:8.3f} s  ({d:+.1%})")

    if improvements:
        print(f"\n  timing improvements ({len(improvements)}) -- faster by >{threshold:.0%} "
              f"(informational, not a failure):")
        for k, m, bv, nv, d in improvements[:10]:
            print(f"    {k:46s} {m:18s} {bv:8.3f} -> {nv:8.3f} s  ({d:+.1%})")

    failed = bool(regressions or exactChanges or missing)
    print("\n" + ("PERFORMANCE GUARD: REGRESSION DETECTED" if failed
                  else "PERFORMANCE GUARD: PASS -- no regression"))
    return 1 if failed else 0


def main():
    if len(sys.argv) < 3:
        print(__doc__)
        return 2
    action = sys.argv[1]
    csvPath = Path(sys.argv[2])
    baselinePath = Path(sys.argv[3]) if len(sys.argv) > 3 else DEFAULT_BASELINE
    if not csvPath.exists():
        print(f"no such CSV: {csvPath}")
        return 2
    if action == "record":
        return record(csvPath, baselinePath)
    if action == "check":
        return check(csvPath, baselinePath)
    print(__doc__)
    return 2


if __name__ == "__main__":
    raise SystemExit(main())
