#!/usr/bin/env python3
"""GPU-PIPE-001 final ladder: optimized vs the Phase-1 baseline.

Both runs use the same harness, machine and settings; the Phase-1 run is the
recorded baseline (results/gpu-pipe-001/baseline/data/runs.csv) and this one is
the optimized tree. Cross-run timing carries ~10% session variance (measured in
Phase 1), so runtime deltas here are corroborating evidence -- the paired
same-session runs in phase2/phase3 are the authority on speed-up. Transfer and
synchronization counts are exact and not subject to that caveat.
"""

import csv
import statistics as st
from pathlib import Path

HERE = Path(__file__).resolve().parent
BASE = HERE.parent / "baseline" / "data" / "runs.csv"
NEW = HERE / "data" / "runs.csv"
GRIDS = ["20x20", "40x40", "80x80", "160x160", "320x320", "640x640"]


def load(p):
    return list(csv.DictReader(open(p)))


def med(rows, grid, backend, key):
    v = [float(r[key]) for r in rows if r["grid"] == grid and r["backend"] == backend]
    return st.median(v) if v else float("nan")


b, n = load(BASE), load(NEW)

print("=== END-TO-END (simple_solve_seconds, median) ===")
print(f"{'grid':>9} {'cells':>8} {'CPU':>9} {'GPU base':>10} {'GPU new':>9} "
      f"{'base x':>8} {'new x':>8} {'GPU gain':>9}")
for g in GRIDS:
    cells = int(med(n, g, "GPU", "cells"))
    cpu = med(n, g, "CPU", "simple_solve_seconds")
    gb = med(b, g, "GPU", "simple_solve_seconds")
    gn = med(n, g, "GPU", "simple_solve_seconds")
    print(f"{g:>9} {cells:>8} {cpu:>9.3f} {gb:>10.3f} {gn:>9.3f} "
          f"{cpu/gb:>7.3f}x {cpu/gn:>7.3f}x {(gn-gb)/gb*100:>+8.1f}%")

print("\n=== TRANSFERS AND SYNCHRONIZATION (GPU, median) ===")
print(f"{'grid':>9} {'D2H base':>10} {'D2H new':>9} {'change':>8} "
      f"{'H2D base':>9} {'H2D new':>8} {'D2H MB base':>12} {'D2H MB new':>11}")
for g in GRIDS:
    db, dn = med(b, g, "GPU", "d2h_calls"), med(n, g, "GPU", "d2h_calls")
    hb, hn = med(b, g, "GPU", "h2d_calls"), med(n, g, "GPU", "h2d_calls")
    bb, bn = med(b, g, "GPU", "d2h_bytes"), med(n, g, "GPU", "d2h_bytes")
    print(f"{g:>9} {db:>10,.0f} {dn:>9,.0f} {(dn-db)/db*100:>+7.1f}% "
          f"{hb:>9,.0f} {hn:>8,.0f} {bb/1e6:>12.1f} {bn/1e6:>11.1f}")

print("\n=== REDUCTIONS PER KRYLOV ITERATION ===")
print(f"{'grid':>9} {'krylov base':>12} {'per it base':>12} {'krylov new':>11} {'per it new':>11}")
for g in GRIDS:
    kb, kn = med(b, g, "GPU", "gpu_linear_solver_iterations"), med(n, g, "GPU", "gpu_linear_solver_iterations")
    db, dn = med(b, g, "GPU", "d2h_calls"), med(n, g, "GPU", "d2h_calls")
    print(f"{g:>9} {kb:>12,.0f} {db/kb:>12.2f} {kn:>11,.0f} {dn/max(kn,1):>11.2f}")

print("\n=== CROSSOVER ===")
for label, rows in (("baseline", b), ("optimized", n)):
    below = [g for g in GRIDS if med(rows, g, "CPU", "simple_solve_seconds") /
             med(rows, g, "GPU", "simple_solve_seconds") < 1.0]
    above = [g for g in GRIDS if med(rows, g, "CPU", "simple_solve_seconds") /
             med(rows, g, "GPU", "simple_solve_seconds") >= 1.0]
    print(f"  {label:<10} GPU slower at: {', '.join(below) or 'none':<32} "
          f"GPU faster at: {', '.join(above) or 'none'}")

print("\n=== CLEANLINESS ===")
for g in GRIDS:
    rows = [r for r in n if r["grid"] == g and r["backend"] == "GPU"]
    clean = {r["ran_cleanly"] for r in rows}
    status = {r["status"] for r in rows}
    print(f"  {g:>9} ran_cleanly={'/'.join(sorted(clean))} status={'/'.join(sorted(status))}")
