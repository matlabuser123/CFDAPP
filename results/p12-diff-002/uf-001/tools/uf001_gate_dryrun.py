#!/usr/bin/env python3
"""P12-DIFF-002-UF-001.10: DRY RUN of the proposed replacement criteria, BEFORE any authoritative
test is edited.

The de Vahl Davis values are FIXED and appear only as the comparison target; nothing here is
derived from CFDApp output.

Proposed criteria, all evaluated on the parity-consistent pair n = 10 and n = 20 (both even, so
the mid-plane is interpolated the same way on both -- see the parity rule in summary.md):

  P1  refinement moves TOWARD the fixed literature value:  err_20 < err_10, each quantity.
  P2  convergence-rate floor: err_10 / err_20 >= 1.5. The scheme's documented formal order is 1
      (first-order upwind convection), which predicts a factor 2.0 per grid doubling; 1.5 admits
      a 25 % pre-asymptotic shortfall.
  P3  precision envelope at 20x20, using the numbers THIS FILE ALREADY CARRIES in its own
      DISABLED_Grid20x20MatchesDeVahlDavisRa1e3: u_max <= 0.08, v_max <= 0.08, Nu_avg <= 0.05.
      No threshold is loosened -- the precision claim moves from the non-asymptotic 10x10 grid to
      the 20x20 grid at this file's own pre-existing 20x20 bounds.
  P4  the extremum LOCATION must match the literature's reported location within one cell:
      |y(u_max) - 0.813| <= 1/n and |x(v_max) - 0.179| <= 1/n, at BOTH grids. These literature
      numbers exist in DeVahlDavis1983.hpp and the current tests never used them.

Controls that must FAIL (non-vacuity): a corrupted wall coefficient (far-cell doubled; far-cell
sign flipped). The superseded two-point operator is expected to PASS and that is correct -- it is
a legitimate lower-order scheme, not a defect; the wall-operator claim is carried by the
wall-shear order study (UF-001.7), not by these criteria.
"""

import glob
import math
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
LOGS = os.path.join(HERE, "..", "logs")

LIT = {"nu_avg": 1.12, "u_max": 3.634, "v_max": 3.679}
LIT_U_Y = 0.813
LIT_V_X = 0.179

COLS = ("n converged nu_avg u_discrete u_discrete_y u_parabolic u_parabolic_y u_spline u_global "
        "v_discrete v_discrete_x v_parabolic v_parabolic_x v_spline v_global mass_imb heat_imb "
        "outer final_outer_change").split()

P3_ENVELOPE = {"nu_avg": 0.05, "u_max": 0.08, "v_max": 0.08}
P2_FLOOR = 1.5


def load(patterns):
    rows = {}
    for pat in patterns:
        for path in sorted(glob.glob(os.path.join(LOGS, pat))):
            for line in open(path, encoding="utf-8", errors="replace"):
                if not line.startswith("CSV,"):
                    continue
                parts = line.strip().split(",")[1:]
                if len(parts) != len(COLS):
                    continue
                rec = {k: float(v) for k, v in zip(COLS, parts)}
                rec["n"] = int(rec["n"])
                if rec["converged"] == 1.0:
                    rows[rec["n"]] = rec
    return rows


QUANT = [("nu_avg", "nu_avg", None, None),
         ("u_max", "u_discrete", "u_discrete_y", LIT_U_Y),
         ("v_max", "v_discrete", "v_discrete_x", LIT_V_X)]


def evaluate(label, rows, expect):
    print(f"\n{'=' * 100}")
    print(f"{label}   (expected overall: {expect})")
    print("=" * 100)
    if 10 not in rows or 20 not in rows:
        print("  n=10 and/or n=20 did not converge -> criteria cannot be evaluated")
        return None
    r10, r20 = rows[10], rows[20]
    results = []
    for litkey, col, loccol, litloc in QUANT:
        e10 = abs(r10[col] - LIT[litkey]) / LIT[litkey]
        e20 = abs(r20[col] - LIT[litkey]) / LIT[litkey]
        p1 = e20 < e10
        ratio = (e10 / e20) if e20 > 0 else float("inf")
        p2 = ratio >= P2_FLOOR
        p3 = e20 <= P3_ENVELOPE[litkey]
        print(f"  {litkey:<8} err_10 {e10:8.5f}  err_20 {e20:8.5f}  ratio {ratio:6.3f}")
        print(f"           P1 toward-literature {'PASS' if p1 else 'FAIL'}"
              f"   P2 rate>={P2_FLOOR} {'PASS' if p2 else 'FAIL'}"
              f"   P3 err_20<={P3_ENVELOPE[litkey]} {'PASS' if p3 else 'FAIL'}")
        results += [p1, p2, p3]
        if loccol is not None:
            for r, n in ((r10, 10), (r20, 20)):
                d = abs(r[loccol] - litloc)
                h = 1.0 / n
                p4 = d <= h
                print(f"           P4 location n={n}: {r[loccol]:.4f} vs literature {litloc}"
                      f"  |diff| {d:.4f} <= h {h:.4f}  {'PASS' if p4 else 'FAIL'}")
                results.append(p4)
    overall = "PASS" if all(results) else "FAIL"
    flag = "" if overall == expect else "   <<< UNEXPECTED"
    print(f"\n  OVERALL: {overall}  ({sum(results)}/{len(results)} criteria){flag}")
    return overall == expect


def main():
    print(__doc__)
    print(f"FIXED literature values (never modified): Nu_avg {LIT['nu_avg']}, "
          f"u_max {LIT['u_max']} at y {LIT_U_Y}, v_max {LIT['v_max']} at x {LIT_V_X}")
    cases = [
        ("CURRENT (DIFF-002)", ["01_grids_current.log", "02_grids_current_fine.log"], "PASS"),
        ("PRE-DIFF-002 two-point (a legitimate lower-order scheme)",
         ["05_grids_baseline.log", "06_grids_baseline_fine.log"], "PASS"),
        ("CONTROL far-cell coefficient DOUBLED", ["11_grids_ctrl_farx2.log"], "FAIL"),
        ("CONTROL far-cell coefficient SIGN FLIPPED", ["12_grids_ctrl_signflip.log"], "FAIL"),
    ]
    outcomes = []
    for label, pats, expect in cases:
        rows = load(pats)
        if not rows:
            print(f"\n{label}: NO DATA (log not present yet)")
            outcomes.append(None)
            continue
        outcomes.append(evaluate(label, rows, expect))
    print(f"\n{'=' * 100}")
    print("DRY-RUN SUMMARY")
    print("=" * 100)
    bad = [c[0] for c, o in zip(cases, outcomes) if o is False]
    missing = [c[0] for c, o in zip(cases, outcomes) if o is None]
    if missing:
        print(f"  MISSING DATA: {missing}")
    print(f"  behaved as pre-registered: {'YES' if not bad and not missing else 'NO -- ' + str(bad)}")
    print("\n  Honest scope note: P1-P4 are NOT wall-operator-discriminating -- the superseded")
    print("  two-point scheme passes them, correctly, because it converges to the same continuum")
    print("  limit. The wall operator's correctness is pinned by the UF-001.7 wall-shear order")
    print("  study (two-point 1.000 / DIFF-002 2.000, quadratic-exact), not by these criteria.")
    return 0 if not bad and not missing else 1


if __name__ == "__main__":
    sys.exit(main())
