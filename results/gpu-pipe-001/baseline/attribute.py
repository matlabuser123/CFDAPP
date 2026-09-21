#!/usr/bin/env python3
"""GPU-PIPE-001 Phase 1: attribute transfers to purpose from measured invariants.

There is no per-purpose counter in the build, so nothing here is read from an
instrumented breakdown.  Instead each claim is tied to a measured ratio that is
constant across a 1024x range in problem size -- which is what makes the
attribution falsifiable rather than a guess.
"""

import csv
import statistics as st
from pathlib import Path

HERE = Path(__file__).resolve().parent
rows = [r for r in csv.DictReader(open(HERE / "data" / "runs.csv")) if r["backend"] == "GPU"]
GRIDS = ["20x20", "40x40", "80x80", "160x160", "320x320", "640x640"]


def med(g, key):
    v = [float(r[key]) for r in rows if r["grid"] == g]
    return st.median(v)


print("=== D2H attribution: reductions predict 7 downloads per Krylov iteration ===")
print("BiCGSTAB per iteration: dot(rHat,r), dot(rHat,v), l2Norm(v), l2Norm(s),")
print("                        dot(t,t), dot(t,s), l2Norm(r)  = 7 reductions")
print(f"\n{'grid':>9} {'D2H':>9} {'krylov':>8} {'7*krylov':>9} {'residual':>9} {'% explained':>12}")
for g in GRIDS:
    d, k = med(g, "d2h_calls"), med(g, "gpu_linear_solver_iterations")
    pred = 7 * k
    print(f"{g:>9} {d:>9.0f} {k:>8.0f} {pred:>9.0f} {d-pred:>9.0f} {pred/d*100:>11.2f}%")

print("\n=== H2D attribution: uploads per linear solve ===")
print(f"{'grid':>9} {'H2D':>9} {'solves':>8} {'H2D/solve':>10}")
for g in GRIDS:
    h, s = med(g, "h2d_calls"), med(g, "gpu_linear_solves")
    print(f"{g:>9} {h:>9.0f} {s:>8.0f} {h/s:>10.2f}")

print("\n=== payload size per D2H call (the partial-sums buffer) ===")
print(f"{'grid':>9} {'cells':>9} {'bytes/call':>11} {'doubles':>9} {'~cells/512':>11}")
for g in GRIDS:
    d, b = med(g, "d2h_calls"), med(g, "d2h_bytes")
    n = med(g, "cells")
    per = b / d
    print(f"{g:>9} {n:>9.0f} {per:>11.0f} {per/8:>9.0f} {n/512:>11.1f}")

print("\n=== purpose breakdown, as the measurements support it ===")
print(f"{'purpose':<22} {'direction':<10} {'share':>8}  basis")
tot_d = sum(med(g, "d2h_calls") for g in GRIDS)
tot_h = sum(med(g, "h2d_calls") for g in GRIDS)
print(
    f"{'convergence/Krylov':<22} {'D2H':<10} {'~100%':>8}  7.00 per Krylov iteration at every grid"
)
print(f"{'matrix + RHS + x0':<22} {'H2D':<10} {'~100%':>8}  ~3 per linear solve at every grid")
for label in ("field", "boundary", "diagnostics", "residual (separate)"):
    print(f"{label:<22} {'-':<10} {'unmeasured':>8}  no per-purpose counter exists in this build")
print(f"\ntotal D2H calls across the ladder: {tot_d:,.0f}   total H2D: {tot_h:,.0f}")
