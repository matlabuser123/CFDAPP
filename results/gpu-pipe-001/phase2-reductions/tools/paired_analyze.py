#!/usr/bin/env python3
"""GPU-PIPE-001 Phase 2: reduce the paired before/after runs to the report.

Medians across repeats, per grid, per side. Also asserts the correctness
invariants that matter more than the timings: identical residuals, identical
continuity, identical iteration counts.
"""

import re
import statistics as st
from collections import defaultdict
from pathlib import Path

RAW = Path(__file__).resolve().parent.parent / "paired-benchmarks" / "raw.txt"

rows = []
for line in RAW.read_text().splitlines():
    if "RESULT" not in line:
        continue
    d = dict(re.findall(r"(\w+)=(\S+)", line))
    rows.append(d)

by = defaultdict(list)
for r in rows:
    by[(int(r["edge"]), r["side"])].append(r)

GRIDS = sorted({int(r["edge"]) for r in rows})


def med(rs, key, cast=float):
    return st.median(cast(r[key]) for r in rs)


print("=== CORRECTNESS: before vs after must be identical ===")
allsame = True
for g in GRIDS:
    b, a = by[(g, "before")], by[(g, "after")]
    for key in ("p_res", "cont", "mass", "p_lin_it", "krylov_it", "solves", "outer"):
        bv = {r[key] for r in b}
        av = {r[key] for r in a}
        same = bv == av and len(bv) == 1
        if not same:
            allsame = False
            print(f"  {g}^2 {key}: before={bv} after={av}   *** DIFFERS ***")
    print(f"  {g}^2  p_res={b[0]['p_res']}  krylov_it={b[0]['krylov_it']}  -> "
          f"{'IDENTICAL' if bv == av else 'DIFFERS'}")
print(f"\n  all correctness fields identical: {allsame}")

print("\n=== TRANSFERS AND SYNCHRONIZATION (medians) ===")
hdr = (f"{'grid':>6} {'metric':>12} {'before':>14} {'after':>14} {'change':>10}")
print(hdr)
for g in GRIDS:
    b, a = by[(g, "before")], by[(g, "after")]
    for key, label in (("d2h_calls", "D2H calls"), ("d2h_bytes", "D2H bytes"),
                       ("syncs", "syncs"), ("launches", "launches"),
                       ("h2d_calls", "H2D calls")):
        bv, av = med(b, key), med(a, key)
        pct = (av - bv) / bv * 100 if bv else 0.0
        print(f"{g:>6} {label:>12} {bv:>14,.0f} {av:>14,.0f} {pct:>+9.1f}%")
    print()

print("=== REDUCTIONS PER KRYLOV ITERATION ===")
print(f"{'grid':>6} {'krylov':>9} {'D2H before':>12} {'per it':>8} {'D2H after':>11} {'per it':>8} "
      f"{'red_groups':>11} {'fusion':>7}")
for g in GRIDS:
    b, a = by[(g, "before")], by[(g, "after")]
    k = med(b, "krylov_it")
    db, da = med(b, "d2h_calls"), med(a, "d2h_calls")
    rg, rq = med(a, "red_groups"), med(a, "red_quantities")
    print(f"{g:>6} {k:>9,.0f} {db:>12,.0f} {db/k:>8.2f} {da:>11,.0f} {da/k:>8.2f} "
          f"{rg:>11,.0f} {rq/rg:>7.2f}x")

print("\n=== RUNTIME (paired, same session, median of repeats) ===")
print(f"{'grid':>6} {'solve before':>13} {'solve after':>12} {'change':>9} "
      f"{'gpu_solve before':>17} {'gpu_solve after':>16} {'change':>9}")
for g in GRIDS:
    b, a = by[(g, "before")], by[(g, "after")]
    sb, sa = med(b, "solve_s"), med(a, "solve_s")
    gb, ga = med(b, "gpu_solve_s"), med(a, "gpu_solve_s")
    print(f"{g:>6} {sb:>13.3f} {sa:>12.3f} {(sa-sb)/sb*100:>+8.1f}% "
          f"{gb:>17.3f} {ga:>16.3f} {(ga-gb)/gb*100:>+8.1f}%")

print("\n  spread across repeats (max-min), to judge whether the change exceeds noise")
print(f"{'grid':>6} {'before spread':>14} {'after spread':>13} {'difference':>11}")
for g in GRIDS:
    b, a = by[(g, "before")], by[(g, "after")]
    sbv = [float(r["solve_s"]) for r in b]
    sav = [float(r["solve_s"]) for r in a]
    print(f"{g:>6} {max(sbv)-min(sbv):>14.3f} {max(sav)-min(sav):>13.3f} "
          f"{st.median(sbv)-st.median(sav):>11.3f}")
