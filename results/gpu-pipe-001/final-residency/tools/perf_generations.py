#!/usr/bin/env python3
"""GPU-PIPE-001 Final Residency, Part 8 -- the three-generation comparison.

Reads each generation's own record from performance/generations/ (see the
README there for why they are not all the same file format) and produces:

  1. grid | CPU | old GPU | GPU-DISC | resident pressure | final resident
  2. the per-stage breakdown at 160^2 / 320^2 / 640^2, and the bottleneck
  3. the transfer summary -- initialization, steady state, output

Medians, never means and never the best run. A generation whose record is
missing prints as `-` rather than being interpolated or dropped.
"""
import csv
import pathlib
import re
import statistics
import sys

E = pathlib.Path("/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp/results/gpu-pipe-001/final-residency")
G = E / "performance" / "generations"

GRIDS = ["20x20", "40x40", "80x80", "160x160", "320x320", "640x640"]


def read_runs(path):
    """runs-*.csv -> {(grid, arm): [row, ...]}"""
    if not path.exists():
        return {}
    out = {}
    with open(path, newline="", encoding="utf-8") as f:
        for row in csv.DictReader(f):
            out.setdefault((row["grid"], row["arm"]), []).append(row)
    return out


def median_of(rows, key):
    vals = [float(r[key]) for r in rows if r.get(key) not in (None, "")]
    return statistics.median(vals) if vals else None


def read_gen1(path):
    """The original GPU-PIPE baseline summary: grid -> (cpu, gpu, speedup)."""
    if not path.exists():
        return {}
    out = {}
    with open(path, newline="", encoding="utf-8") as f:
        for row in csv.DictReader(f):
            out[row["grid"]] = (float(row["cpu_median_s"]), float(row["gpu_median_s"]),
                                float(row["speedup"]))
    return out


GEN2_ROW = re.compile(r"^\s*(\d+x\d+)\s+(\d+)\s+(\d+)\s+([\d.]+)\s+([\d.]+)\s+([\d.]+)\s+([\d.]+)")


def read_gen2(path):
    """GPU-DISC-001Q's analysis text: grid -> (cpu, gpu_pipe, gpu_disc, disc_only)."""
    if not path.exists():
        return {}
    out = {}
    started = False
    for line in path.read_text(encoding="utf-8").splitlines():
        if "end-to-end wall time" in line:
            started = True
            continue
        if started and line.strip().startswith("grid"):
            continue
        if started:
            m = GEN2_ROW.match(line)
            if m:
                out[m.group(1)] = (float(m.group(4)), float(m.group(5)), float(m.group(6)),
                                   float(m.group(7)))
            elif out and not line.strip():
                break
    return out


def fmt(v, width=9, digits=3, suffix=""):
    return "-".rjust(width) if v is None else ("%.*f%s" % (digits, v, suffix)).rjust(width)


def main():
    gen1 = read_gen1(G / "gen1-gpu-pipe-baseline-summary.csv")
    gen2 = read_gen2(G / "gen2-gpu-disc-001Q-analysis-cavity.txt")
    gen25 = read_runs(G / "gen25-resident-pressure-runs-cavity.csv")
    gen3 = read_runs(G / "gen3-final-resident-runs-cavity.csv")

    if not gen3:
        print("gen3-final-resident-runs-cavity.csv is missing -- run the benchmark first")
        return 2

    print("=== 1. three generations of the GPU path, cavity2d, median wall time ===")
    print("   generation 1  original GPU-PIPE baseline   GPU solver, CPU discretization")
    print("   generation 2  GPU-DISC integrated path     GPU solver + GPU discretization")
    print("   generation 2.5 resident pressure solve     + device-resident pressure system")
    print("   generation 3  final resident path          + device-resident SIMPLE outer iteration")
    print()
    header = ("      grid     cells      CPU(s)    gen1(s)  gen1 x    gen2(s)  gen2 x   "
              "gen2.5(s) gen2.5 x   gen3(s)  gen3 x   gen3/gen1")
    print(header)
    rows = []
    for grid in GRIDS:
        g3cpu = gen3.get((grid, "cpu"))
        g3gpu = gen3.get((grid, "gpu-disc"))
        if not g3gpu:
            continue
        cpu = median_of(g3cpu, "wall_seconds") if g3cpu else None
        t3 = median_of(g3gpu, "wall_seconds")
        cells = int(g3gpu[0]["cells"])

        t1 = gen1.get(grid, (None, None, None))[1]
        s1 = gen1.get(grid, (None, None, None))[2]
        t2 = gen2.get(grid, (None, None, None, None))[2]
        cpu2 = gen2.get(grid, (None, None, None, None))[0]
        s2 = (cpu2 / t2) if (t2 and cpu2) else None
        g25 = gen25.get((grid, "gpu-disc"))
        t25 = median_of(g25, "wall_seconds") if g25 else None
        cpu25rows = gen25.get((grid, "cpu"))
        cpu25 = median_of(cpu25rows, "wall_seconds") if cpu25rows else None
        s25 = (cpu25 / t25) if (t25 and cpu25) else None
        s3 = (cpu / t3) if (cpu and t3) else None
        improvement = (s3 / s1) if (s3 and s1) else None

        print("%10s %9d %s %s %s %s %s %s %s %s %s %s" % (
            grid, cells, fmt(cpu, 11), fmt(t1, 10), fmt(s1, 7, 3, "x"), fmt(t2, 10),
            fmt(s2, 7, 3, "x"), fmt(t25, 11), fmt(s25, 8, 3, "x"), fmt(t3, 9),
            fmt(s3, 7, 3, "x"), fmt(improvement, 11, 2, "x")))
        rows.append((grid, cells, cpu, t3, s3, s1))

    crossovers = {}
    for label, getter in (("gen1", lambda g: gen1.get(g, (None, None, None))[2]),
                          ("gen3", lambda g: next((r[4] for r in rows if r[0] == g), None))):
        first = next((g for g in GRIDS if (getter(g) or 0) > 1.0), None)
        crossovers[label] = first
    print()
    print("  crossover, original GPU-PIPE baseline : %s" % (crossovers["gen1"] or "never"))
    print("  crossover, final resident path        : %s" % (crossovers["gen3"] or "never"))

    # --- non-vacuity: did the timed arm actually run resident? --------------
    print()
    print("=== 2. was the timed GPU arm actually the resident path? ===")
    for grid in GRIDS:
        g3gpu = gen3.get((grid, "gpu-disc"))
        if not g3gpu:
            continue
        flags = {r.get("resident_loop", "?") for r in g3gpu}
        disc = {r.get("gpu_discretization", "?") for r in g3gpu}
        ok = flags == {"1"} and disc == {"1"}
        print("  %-9s gpu-disc arm: resident_loop=%s gpu_discretization=%s  %s"
              % (grid, ",".join(sorted(flags)), ",".join(sorted(disc)),
                 "yes" if ok else "NO -- this grid's speed-up is NOT a residency measurement"))
        discOnly = gen3.get((grid, "disc-only"))
        if discOnly:
            f2 = {r.get("resident_loop", "?") for r in discOnly}
            print("  %-9s disc-only arm (attribution): resident_loop=%s  %s"
                  % (grid, ",".join(sorted(f2)),
                     "correctly declined" if f2 == {"0"} else "UNEXPECTED"))

    STAGES = [
        ("momentum assembly", "momentum_assembly_s"),
        ("momentum solves", "momentum_solve_s"),
        ("response coefficients", "response_s"),
        ("face-flux prediction", "predicted_flux_s"),
        ("pressure assembly", "pressure_assembly_s"),
        ("pressure solve", "pressure_solve_s"),
        ("velocity correction", "velocity_correction_s"),
        ("face-flux correction", "face_flux_correction_s"),
        ("residual / bookkeeping", "bookkeeping_s"),
        ("setup (one-time)", "setup_s"),
        ("other", "other_s"),
    ]
    print()
    print("=== 3. stage breakdown, final resident path (gpu-disc arm), median ===")
    print("  Transfers and synchronization are NOT separate stages: every transfer happens")
    print("  inside the stage that issues it and is counted there. They are reported")
    print("  separately below as time (upload_s/download_s) and as counts.")
    for grid in ["160x160", "320x320", "640x640"]:
        g3gpu = gen3.get((grid, "gpu-disc"))
        if not g3gpu:
            continue
        total = median_of(g3gpu, "wall_seconds")
        iters = median_of(g3gpu, "outer_iterations") or 1
        print()
        print("  --- %s  (%d outer iterations, total %.3f s) ---" % (grid, int(iters), total))
        values = []
        for label, key in STAGES:
            v = median_of(g3gpu, key)
            if v is None:
                continue
            values.append((label, v))
        for label, v in values:
            print("      %-24s %9.4f s  %6.2f%%   %9.5f s/iteration"
                  % (label, v, 100.0 * v / total if total else 0.0, v / iters))
        up = median_of(g3gpu, "upload_s")
        down = median_of(g3gpu, "download_s")
        if up is not None:
            print("      %-24s %9.4f s  %6.2f%%   (inside the stages above)"
                  % ("  of which upload", up, 100.0 * up / total if total else 0.0))
            print("      %-24s %9.4f s  %6.2f%%   (inside the stages above)"
                  % ("  of which download", down, 100.0 * down / total if total else 0.0))
        syncs = median_of(g3gpu, "syncs")
        red = median_of(g3gpu, "reduction_groups")
        if syncs is not None:
            print("      %-24s %9.0f total  %9.1f /iteration" % ("synchronizations", syncs,
                                                                 syncs / iters))
        if red is not None:
            print("      %-24s %9.0f total  %9.1f /iteration   (the BiCGSTAB algorithm's own)"
                  % ("reduction round trips", red, red / iters))
        worst = max(values, key=lambda kv: kv[1]) if values else None
        if worst:
            print("      DOMINANT STAGE: %s  (%.2f%% of the solve)"
                  % (worst[0], 100.0 * worst[1] / total if total else 0.0))

    print()
    print("=== 4. transfer summary, final resident path ===")
    print("      grid       iters   H2D calls   H2D bytes   D2H calls    D2H bytes   "
          "D2H-red calls  H2D/iter   nonred D2H/iter   alloc  realloc")
    for grid in GRIDS:
        g3gpu = gen3.get((grid, "gpu-disc"))
        if not g3gpu:
            continue
        iters = median_of(g3gpu, "outer_iterations") or 1
        h2dc = median_of(g3gpu, "h2d_calls")
        h2db = median_of(g3gpu, "h2d_bytes")
        d2hc = median_of(g3gpu, "d2h_calls")
        d2hb = median_of(g3gpu, "d2h_bytes")
        red = median_of(g3gpu, "reduction_groups") or 0.0
        print("  %9s %8d %11.0f %11.0f %11.0f %12.0f %14.0f %9.2f %17.2f %7.0f %8.0f"
              % (grid, int(iters), h2dc, h2db, d2hc, d2hb, red, h2dc / iters,
                 (d2hc - red) / iters, median_of(g3gpu, "allocations"),
                 median_of(g3gpu, "reallocations")))
    print()
    print("  H2D/iter counts EVERY host-to-device call, including the one-time initialization")
    print("  amortised over the run; the steady-state per-iteration figures measured as a")
    print("  delta between two budgets are in transfers/after.log, which is the authority.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
