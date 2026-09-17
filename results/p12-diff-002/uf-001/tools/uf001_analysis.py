#!/usr/bin/env python3
"""P12-DIFF-002-UF-001.6/.8: grid-convergence and continuum-limit analysis.

Reads the raw CSV rows the C++ grid probe emitted (logs/01,02 for the current library and
logs/05,06 for the pre-DIFF-002 baseline) and computes, for each quantity and each extrema
extraction convention:

  * error against the FIXED de Vahl Davis value (never modified),
  * pairwise observed order,
  * Richardson extrapolation and GCI -- ONLY where the sequence is monotone and the pairwise
    orders are consistent, never from a non-asymptotic sequence,
  * the distance of the extrapolated value from the literature value.

Parity rule, carried over from UC-001: the mid-plane profile is sampled directly when n is odd
(x = 0.5 or y = 0.5 is a cell-centre line) and interpolated between two lines when n is even, so
a convergence sequence must not mix parities. Only parity-consistent subsequences are used for
order estimates; every grid is still reported raw.
"""

import glob
import math
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
LOGS = os.path.join(HERE, "..", "logs")

# The fixed literature values (de Vahl Davis 1983, Ra = 1e3, Pr = 0.71), h->0 Richardson
# extrapolated. NEVER modified by this investigation.
LIT = {"nu_avg": 1.12, "u_max": 3.634, "v_max": 3.679}
LIT_LOC = {"u_max_y": 0.813, "v_max_x": 0.179}

COLS = ("n converged nu_avg u_discrete u_discrete_y u_parabolic u_parabolic_y u_spline u_global "
        "v_discrete v_discrete_x v_parabolic v_parabolic_x v_spline v_global mass_imb heat_imb "
        "outer final_outer_change").split()


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
                rec = {}
                for k, v in zip(COLS, parts):
                    rec[k] = float(v)
                rec["n"] = int(rec["n"])
                if rec["converged"] == 1.0:
                    rows[rec["n"]] = rec
    return [rows[n] for n in sorted(rows)]


def pairwise_orders(ns, errs):
    out = []
    for i in range(len(ns) - 1):
        r = ns[i + 1] / ns[i]
        if errs[i] > 0 and errs[i + 1] > 0 and r > 1:
            out.append(math.log(errs[i] / errs[i + 1]) / math.log(r))
        else:
            out.append(float("nan"))
    return out


def constant_ratio_triplet(ns):
    """The FINEST constant-refinement-ratio triplet available in ns (Richardson needs r21 == r32).
    Returns the three indices, or None."""
    best = None
    for i in range(len(ns)):
        for j in range(i + 1, len(ns)):
            for k in range(j + 1, len(ns)):
                if abs((ns[j] / ns[i]) - (ns[k] / ns[j])) < 1e-9:
                    if best is None or ns[k] > ns[best[2]] or (
                            ns[k] == ns[best[2]] and ns[i] > ns[best[0]]):
                        best = (i, j, k)
    return best


def richardson(ns, vals):
    """Three-level Richardson on a constant-ratio triplet. Returns (p, extrapolated, gci, triplet)."""
    if len(ns) < 3:
        return None
    idx = constant_ratio_triplet(ns)
    if idx is None:
        return None
    i, j, k = idx
    n1, n2, n3 = ns[i], ns[j], ns[k]
    r21 = n2 / n1
    phi1, phi2, phi3 = vals[i], vals[j], vals[k]  # coarse -> fine
    e21, e32 = phi2 - phi1, phi3 - phi2
    if e21 == 0.0 or e32 / e21 <= 0:
        return None  # oscillatory: no order, no GCI
    ratio = e21 / e32
    if ratio <= 1.0:
        return None  # differences not shrinking: not convergent, no GCI
    p = math.log(ratio) / math.log(r21)
    # Credibility guard. An observed order far below the scheme's lowest formal order means the
    # grid-to-grid differences are barely shrinking, and Richardson then divides by a nearly zero
    # (r^p - 1): the "extrapolation" is an artifact, not an estimate. Report it as not computed
    # rather than printing a number. (This fires on the baseline's v_max parabolic sequence,
    # p = 0.062, whose raw extrapolation is 5.19 against a literature 3.679.)
    if p < 0.5 or p > 3.0:
        return ("REJECTED", p, (n1, n2, n3))
    ext = phi3 + e32 / (r21 ** p - 1.0)
    gci = 1.25 * abs(e32 / phi3) / (r21 ** p - 1.0)
    return p, ext, gci, (n1, n2, n3)


def parity_subsets(ns):
    even = [n for n in ns if n % 2 == 0]
    odd = [n for n in ns if n % 2 == 1]
    return {"even": even, "odd": odd}


def report(label, rows):
    print("=" * 104)
    print(f"{label}   ({len(rows)} converged grids: {[r['n'] for r in rows]})")
    print("=" * 104)
    print()
    print("## raw values (nothing derived)")
    hdr = (f"  {'n':>4} {'Nu_avg':>10} {'u_disc':>10} {'u_disc_y':>9} {'u_parab':>10} "
           f"{'u_par_y':>9} {'v_disc':>10} {'v_disc_x':>9} {'v_parab':>10} {'v_par_x':>9} "
           f"{'mass':>9} {'heat':>9} {'outer':>6}")
    print(hdr)
    for r in rows:
        print(f"  {r['n']:>4} {r['nu_avg']:>10.6f} {r['u_discrete']:>10.6f} "
              f"{r['u_discrete_y']:>9.4f} {r['u_parabolic']:>10.6f} {r['u_parabolic_y']:>9.4f} "
              f"{r['v_discrete']:>10.6f} {r['v_discrete_x']:>9.4f} {r['v_parabolic']:>10.6f} "
              f"{r['v_parabolic_x']:>9.4f} {r['mass_imb']:>9.1e} {r['heat_imb']:>9.1e} "
              f"{int(r['outer']):>6}")

    print()
    print("## relative error against the FIXED literature value")
    quantities = [("nu_avg", "nu_avg", "Nu_avg"),
                  ("u_max", "u_discrete", "u_max discrete (current convention)"),
                  ("u_max", "u_parabolic", "u_max parabolic sub-grid"),
                  ("u_max", "u_spline", "u_max spline"),
                  ("u_max", "u_global", "u_max GLOBAL field (different convention)"),
                  ("v_max", "v_discrete", "v_max discrete (current convention)"),
                  ("v_max", "v_parabolic", "v_max parabolic sub-grid"),
                  ("v_max", "v_spline", "v_max spline"),
                  ("v_max", "v_global", "v_max GLOBAL field (different convention)")]
    ns_all = [r["n"] for r in rows]
    for litkey, col, name in quantities:
        errs = [abs(r[col] - LIT[litkey]) / LIT[litkey] for r in rows]
        cells = "  ".join(f"n={n}:{e:8.5f}" for n, e in zip(ns_all, errs))
        print(f"  {name:<38} {cells}")

    print()
    print("## pairwise observed order, PARITY-CONSISTENT subsequences only")
    for litkey, col, name in quantities:
        for par, ns in parity_subsets(ns_all).items():
            if len(ns) < 2:
                continue
            sub = [r for r in rows if r["n"] in ns]
            errs = [abs(r[col] - LIT[litkey]) / LIT[litkey] for r in sub]
            ps = pairwise_orders(ns, errs)
            cells = "  ".join(
                f"{ns[i]}->{ns[i+1]}:{p:6.3f}" for i, p in enumerate(ps) if not math.isnan(p))
            if cells:
                print(f"  {name:<38} [{par:>4}] {cells}")

    print()
    print("## Richardson extrapolation / GCI -- only where the sequence supports it")
    for litkey, col, name in quantities:
        for par, ns in parity_subsets(ns_all).items():
            if len(ns) < 3:
                continue
            sub = [r for r in rows if r["n"] in ns]
            vals = [r[col] for r in sub]
            res = richardson(ns, vals)
            if res is None:
                print(f"  {name:<38} [{par:>4}] NOT computed: no constant-ratio triplet, or the "
                      f"sequence is oscillatory / its differences do not shrink -- a GCI here "
                      f"would be meaningless")
                continue
            if res[0] == "REJECTED":
                print(f"  {name:<38} [{par:>4}] {res[2]}  NOT computed: observed order "
                      f"{res[1]:.3f} is outside [0.5, 3.0] -- the differences barely shrink, so "
                      f"Richardson would be an artifact")
                continue
            p, ext, gci, trip = res
            d = abs(ext - LIT[litkey]) / LIT[litkey]
            print(f"  {name:<38} [{par:>4}] {trip}  p {p:6.3f}  extrapolated {ext:10.6f}  "
                  f"GCI {gci:8.5f}  |extrap - literature|/literature {d:8.5f}")
    print()


def main():
    cur = load(["01_grids_current.log", "02_grids_current_fine.log"])
    base = load(["05_grids_baseline.log", "06_grids_baseline_fine.log"])
    if not cur:
        print("no current-library rows found", file=sys.stderr)
        return 1
    print(__doc__)
    print(f"FIXED literature values: Nu_avg {LIT['nu_avg']}, u_max {LIT['u_max']} "
          f"(y {LIT_LOC['u_max_y']}), v_max {LIT['v_max']} (x {LIT_LOC['v_max_x']})")
    print()
    report("CURRENT (DIFF-002)", cur)
    if base:
        report("PRE-DIFF-002 BASELINE (two-point wall flux)", base)

    # Head-to-head on the grids both libraries completed.
    common = sorted(set(r["n"] for r in cur) & set(r["n"] for r in base))
    if common:
        print("=" * 104)
        print("HEAD-TO-HEAD: relative error against the literature, same grids")
        print("=" * 104)
        print()
        c = {r["n"]: r for r in cur}
        b = {r["n"]: r for r in base}
        for litkey, col, name in [("nu_avg", "nu_avg", "Nu_avg"),
                                  ("u_max", "u_discrete", "u_max discrete"),
                                  ("u_max", "u_parabolic", "u_max parabolic"),
                                  ("v_max", "v_discrete", "v_max discrete"),
                                  ("v_max", "v_parabolic", "v_max parabolic")]:
            print(f"  {name}")
            for n in common:
                ec = abs(c[n][col] - LIT[litkey]) / LIT[litkey]
                eb = abs(b[n][col] - LIT[litkey]) / LIT[litkey]
                verdict = "DIFF-002 better" if ec < eb else "baseline better"
                print(f"    n={n:<4} baseline {eb:8.5f}   DIFF-002 {ec:8.5f}   "
                      f"ratio {ec/eb:6.3f}   {verdict}")
            print()
    return 0


if __name__ == "__main__":
    sys.exit(main())
