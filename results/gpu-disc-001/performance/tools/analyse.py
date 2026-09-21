#!/usr/bin/env python3
"""GPU-DISC-001Q -- turn the raw benchmark CSV into the required tables.

Medians with min/max spread, never a cherry-picked fastest run. Every table
prints the repeat count it was computed from, so a thin point is visible as a
thin point rather than presented like a solid one.
"""

import csv
import json
import statistics as st
import sys
from pathlib import Path

ROOT = Path("/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp")
PERF = ROOT / "results/gpu-disc-001/performance"
RAW = PERF / "raw"
# The recorded GPU-PIPE-001 final benchmark -- the "old" path, measured before
# GPU-DISC-001 existed. Used as corroboration; the in-session `gpu-pipe` arm is
# the authority, because it shares this session's machine state.
OLD = ROOT / "results/gpu-pipe-001/benchmarks/data/runs.csv"

ARMS = ["cpu", "gpu-pipe", "gpu-disc", "disc-only"]
STAGES = [
    ("setup_s", "setup / upload"),
    ("momentum_assembly_s", "momentum assembly"),
    ("momentum_solve_s", "momentum solves"),
    ("response_s", "response coefficients"),
    ("predicted_flux_s", "predicted face flux"),
    ("pressure_assembly_s", "pressure-correction assembly"),
    ("pressure_solve_s", "pressure solve"),
    ("velocity_correction_s", "velocity correction"),
    ("face_flux_correction_s", "face-flux correction"),
    ("bookkeeping_s", "residual / bookkeeping"),
    ("other_s", "other"),
]


def load(path):
    if not path.exists():
        return []
    with open(path) as f:
        return list(csv.DictReader(f))


def rows_for(rows, case, grid, arm):
    return [r for r in rows if r["case"] == case and r["grid"] == grid and r["arm"] == arm]


def med(rows, key):
    v = [float(r[key]) for r in rows]
    return st.median(v) if v else float("nan")


def spread(rows, key):
    v = [float(r[key]) for r in rows]
    if not v:
        return (float("nan"), float("nan"))
    return (min(v), max(v))


def grids_of(rows, case):
    seen = []
    for r in rows:
        if r["case"] == case and r["grid"] not in seen:
            seen.append(r["grid"])
    return seen


def cases_of(rows):
    seen = []
    for r in rows:
        if r["case"] not in seen:
            seen.append(r["case"])
    return seen


def fmt(x, width=9, digits=3):
    return f"{x:{width}.{digits}f}" if x == x else " " * (width - 3) + "n/a"


def speedup_table(rows, case, oldRows):
    print(f"\n=== {case}: end-to-end wall time (median of N repeats) ===")
    print(f"{'grid':>11} {'cells':>8} {'N':>2} "
          f"{'CPU(s)':>9} {'gpu-pipe':>9} {'gpu-disc':>9} {'disc-only':>9}  "
          f"{'pipe x':>7} {'disc x':>7} {'disc/pipe':>9}  {'old x':>7}")
    out = []
    for grid in grids_of(rows, case):
        r = {a: rows_for(rows, case, grid, a) for a in ARMS}
        if not r["cpu"]:
            continue
        cells = int(r["cpu"][0]["cells"])
        n = len(r["cpu"])
        t = {a: med(r[a], "wall_seconds") for a in ARMS}
        pipeX = t["cpu"] / t["gpu-pipe"] if t["gpu-pipe"] else float("nan")
        discX = t["cpu"] / t["gpu-disc"] if t["gpu-disc"] else float("nan")
        gain = t["gpu-pipe"] / t["gpu-disc"] if t["gpu-disc"] else float("nan")
        # The recorded old measurement, for the cavity series only.
        oldX = float("nan")
        if oldRows and case == "cavity2d":
            oc = [x for x in oldRows if x["grid"] == grid and x["backend"] == "CPU"]
            og = [x for x in oldRows if x["grid"] == grid and x["backend"] == "GPU"]
            if oc and og:
                oldX = st.median([float(x["simple_solve_seconds"]) for x in oc]) / st.median(
                    [float(x["simple_solve_seconds"]) for x in og])
        print(f"{grid:>11} {cells:>8} {n:>2} {fmt(t['cpu'])} {fmt(t['gpu-pipe'])} "
              f"{fmt(t['gpu-disc'])} {fmt(t['disc-only'])}  {pipeX:>6.3f}x {discX:>6.3f}x "
              f"{gain:>8.2f}x  {oldX:>6.3f}x")
        out.append({"grid": grid, "cells": cells, "repeats": n,
                    "cpu_s": t["cpu"], "gpu_pipe_s": t["gpu-pipe"], "gpu_disc_s": t["gpu-disc"],
                    "disc_only_s": t["disc-only"], "pipe_speedup": pipeX, "disc_speedup": discX,
                    "disc_over_pipe": gain, "old_recorded_speedup": oldX})

    print(f"\n{'grid':>11}  spread (min..max as % of median)")
    for grid in grids_of(rows, case):
        parts = []
        for a in ARMS:
            rr = rows_for(rows, case, grid, a)
            if not rr:
                continue
            m = med(rr, "wall_seconds")
            lo, hi = spread(rr, "wall_seconds")
            parts.append(f"{a} {100.0 * (hi - lo) / m:4.1f}%" if m else f"{a} n/a")
        print(f"{grid:>11}  " + "   ".join(parts))

    # Crossover: the smallest grid at which the arm beats CPU.
    for arm, label in (("gpu-pipe", "old GPU path"), ("gpu-disc", "new GPU-DISC path")):
        cross = None
        for e in out:
            key = "pipe_speedup" if arm == "gpu-pipe" else "disc_speedup"
            if e[key] > 1.0:
                cross = e["grid"]
                break
        print(f"  crossover, {label:20s}: "
              + (f"{cross} (first grid where it beats the CPU)" if cross
                 else "never within the measured range"))
    return out


def stage_table(rows, case, grid):
    r = {a: rows_for(rows, case, grid, a) for a in ARMS}
    if not r["cpu"]:
        return None
    print(f"\n=== {case} {grid}: stage breakdown (median seconds, % of that arm's wall) ===")
    print(f"{'stage':>30} " + "".join(f"{a:>21}" for a in ARMS))
    totals = {a: med(r[a], "wall_seconds") for a in ARMS}
    table = {}
    for key, label in STAGES:
        cells = []
        for a in ARMS:
            if not r[a]:
                cells.append(f"{'n/a':>21}")
                continue
            v = med(r[a], key)
            pct = 100.0 * v / totals[a] if totals[a] else 0.0
            cells.append(f"{v:>13.4f} {pct:5.1f}%")
            table.setdefault(a, {})[label] = {"seconds": v, "percent": pct}
        print(f"{label:>30} " + "".join(cells))
    print(f"{'-' * 30} " + "".join("-" * 21 for _ in ARMS))
    for key, label in (("discretization_s", "DISCRETIZATION total"),
                       ("linear_solve_s", "LINEAR SOLVE total")):
        cells = []
        for a in ARMS:
            if not r[a]:
                cells.append(f"{'n/a':>21}")
                continue
            v = med(r[a], key)
            pct = 100.0 * v / totals[a] if totals[a] else 0.0
            cells.append(f"{v:>13.4f} {pct:5.1f}%")
            table.setdefault(a, {})[label] = {"seconds": v, "percent": pct}
        print(f"{label:>30} " + "".join(cells))
    print(f"{'wall':>30} " + "".join(f"{totals[a]:>13.4f} {100.0:5.1f}%" if r[a]
                                     else f"{'n/a':>21}" for a in ARMS))
    return table


def transfer_table(rows, case):
    print(f"\n=== {case}: transfers, synchronization, allocations (median, GPU arms) ===")
    print(f"{'grid':>11} {'arm':>10} {'iters':>6} {'H2D':>9} {'H2D MB':>9} {'D2H':>9} "
          f"{'D2H MB':>9} {'H2D/it':>8} {'D2H/it':>8} {'MB/it':>8} {'sync':>8} {'sync/it':>8} "
          f"{'alloc':>7} {'realloc':>8} {'kernels':>9}")
    out = []
    for grid in grids_of(rows, case):
        for a in ("gpu-pipe", "gpu-disc", "disc-only"):
            rr = rows_for(rows, case, grid, a)
            if not rr:
                continue
            it = med(rr, "outer_iterations")
            h2d, d2h = med(rr, "h2d_calls"), med(rr, "d2h_calls")
            h2dB, d2hB = med(rr, "h2d_bytes"), med(rr, "d2h_bytes")
            sync = med(rr, "syncs")
            e = {"grid": grid, "arm": a, "iterations": it,
                 "h2d_calls": h2d, "h2d_bytes": h2dB, "d2h_calls": d2h, "d2h_bytes": d2hB,
                 "h2d_per_iter": h2d / it, "d2h_per_iter": d2h / it,
                 "mb_per_iter": (h2dB + d2hB) / it / 1e6,
                 "syncs": sync, "syncs_per_iter": sync / it,
                 "allocations": med(rr, "allocations"),
                 "reallocations": med(rr, "reallocations"),
                 "kernels": med(rr, "kernels")}
            out.append(e)
            print(f"{grid:>11} {a:>10} {it:>6.0f} {h2d:>9.0f} {h2dB / 1e6:>9.1f} {d2h:>9.0f} "
                  f"{d2hB / 1e6:>9.1f} {e['h2d_per_iter']:>8.1f} {e['d2h_per_iter']:>8.1f} "
                  f"{e['mb_per_iter']:>8.2f} {sync:>8.0f} {e['syncs_per_iter']:>8.1f} "
                  f"{e['allocations']:>7.0f} {e['reallocations']:>8.0f} {e['kernels']:>9.0f}")
    return out


def throughput_table(rows, case):
    print(f"\n=== {case}: throughput ===")
    print("  cell-iterations/s = cells x outer iterations / wall seconds")
    print("  face-iterations/s = faces x outer iterations / wall seconds")
    print(f"{'grid':>11} {'cells':>8} " + "".join(f"{a + ' Mcell-it/s':>20}" for a in ARMS))
    out = []
    for grid in grids_of(rows, case):
        cells = None
        vals = []
        for a in ARMS:
            rr = rows_for(rows, case, grid, a)
            if not rr:
                vals.append(float("nan"))
                continue
            cells = int(rr[0]["cells"])
            it = med(rr, "outer_iterations")
            w = med(rr, "wall_seconds")
            vals.append(cells * it / w / 1e6 if w else float("nan"))
        print(f"{grid:>11} {cells:>8} " + "".join(f"{v:>20.3f}" for v in vals))
        out.append({"grid": grid, "cells": cells,
                    **{a: v for a, v in zip(ARMS, vals)}})
    return out


def memory_table(rows, case):
    print(f"\n=== {case}: GPU memory ===")
    print(f"{'grid':>11} {'cells':>8} {'arm':>10} {'peak DeviceBuffer MB':>22} "
          f"{'bytes/cell':>12} {'driver used MB after':>22}")
    out = []
    for grid in grids_of(rows, case):
        for a in ("gpu-pipe", "gpu-disc", "disc-only"):
            rr = rows_for(rows, case, grid, a)
            if not rr:
                continue
            cells = int(rr[0]["cells"])
            peak = med(rr, "peak_device_bytes")
            after = med(rr, "device_used_bytes_after")
            print(f"{grid:>11} {cells:>8} {a:>10} {peak / 1e6:>22.2f} {peak / cells:>12.1f} "
                  f"{after / 1e6:>22.2f}")
            out.append({"grid": grid, "cells": cells, "arm": a,
                        "peak_device_bytes": peak, "bytes_per_cell": peak / cells,
                        "driver_used_bytes_after": after})
    return out


def integrity(rows):
    print("\n=== numerical integrity of every timed run ===")
    bad = 0
    statuses, finite = set(), set()
    for r in rows:
        statuses.add(r["status"])
        finite.add(r["all_finite"])
        if r["status"] != "MaxIterations" or r["all_finite"] != "1":
            bad += 1
        if r["arm"] in ("gpu-disc", "disc-only") and r["gpu_discretization"] != "1":
            bad += 1
    print(f"  runs                      {len(rows)}")
    print(f"  distinct statuses         {sorted(statuses)}")
    print(f"  all_finite values         {sorted(finite)}")
    print(f"  runs failing integrity    {bad}")
    worst = max((abs(float(r["mass_imbalance"])) for r in rows), default=0.0)
    print(f"  worst |global mass imbalance| over all runs   {worst:.3e}")
    return bad == 0


def main():
    mode = sys.argv[1] if len(sys.argv) > 1 else "cavity"
    rows = load(RAW / f"runs-{mode}.csv")
    if not rows:
        print(f"no data in {RAW / f'runs-{mode}.csv'}")
        return 1
    oldRows = load(OLD)

    report = {"mode": mode}
    for case in cases_of(rows):
        report.setdefault("cases", {})[case] = {
            "speedup": speedup_table(rows, case, oldRows),
            "transfers": transfer_table(rows, case),
            "throughput": throughput_table(rows, case),
            "memory": memory_table(rows, case),
        }
        stages = {}
        for grid in grids_of(rows, case):
            t = stage_table(rows, case, grid)
            if t:
                stages[grid] = t
        report["cases"][case]["stages"] = stages

    ok = integrity(rows)
    report["integrity_ok"] = ok

    out = PERF / f"analysis-{mode}.json"
    out.write_text(json.dumps(report, indent=2))
    print(f"\nwrote {out}")
    return 0 if ok else 1


if __name__ == "__main__":
    raise SystemExit(main())
