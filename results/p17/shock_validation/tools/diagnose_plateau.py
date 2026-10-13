"""P17.9 DIAGNOSTIC -- why does the star-right plateau error stop converging?

Investigation only (CLAUDE.md section 5.3 step 2). Changes nothing, decides nothing. It reads the
profile CSVs the benchmark already wrote and re-measures the star-right plateau with a range of
exclusion widths, to separate two hypotheses:

  H1  the SOLVER's post-shock plateau is not converging      -> a real defect
  H2  the fixed 5-cell exclusion leaves the window contaminated by the CONTACT's smeared tail,
      which spreads like sqrt(n_steps) while the window grows like nx -> an INSTRUMENT limit
"""
import csv, math, sys

EXACT_RHO_STAR_R = 0.26560611841121122
EXACT_RHO_STAR_L = 0.42627989804573955
EXACT_CONTACT = 0.68550398701451403
EXACT_SHOCK = 0.85041856284399202

print("%-6s %-8s %-12s %-12s %-12s %-12s %-12s" %
      ("nx", "excl", "window", "rho*_R", "|err|", "contact w", "err/excl5"))
base = {}
for nx in (200, 400, 800):
    path = "results/p17/shock_validation/data/sod_profile_nx%d.csv" % nx
    rows = list(csv.DictReader(open(path)))
    x = [float(r["x"]) for r in rows]
    rho = [float(r["rho_num"]) for r in rows]
    dx = 1.0 / nx
    # measured contact smear width: cells between 10% and 90% of the contact jump
    lo = min(EXACT_RHO_STAR_L, EXACT_RHO_STAR_R) + 0.1 * abs(EXACT_RHO_STAR_L - EXACT_RHO_STAR_R)
    hi = min(EXACT_RHO_STAR_L, EXACT_RHO_STAR_R) + 0.9 * abs(EXACT_RHO_STAR_L - EXACT_RHO_STAR_R)
    cw = sum(1 for v in rho if lo <= v <= hi)
    for excl in (5, 10, 20, "half-contact"):
        e = int(math.ceil(cw / 2.0)) + 5 if excl == "half-contact" else excl
        loX = EXACT_CONTACT + e * dx
        hiX = EXACT_SHOCK - 5 * dx
        vals = [rho[i] for i in range(len(x)) if loX <= x[i] <= hiX]
        if len(vals) < 5:
            print("%-6d %-8s window too narrow (%d cells)" % (nx, excl, len(vals)))
            continue
        m = sum(vals) / len(vals)
        err = abs(m - EXACT_RHO_STAR_R)
        key = str(excl)
        base.setdefault(key, []).append((nx, err))
        print("%-6d %-8s %-12d %-12.8f %-12.5e %-12d %-12s" %
              (nx, str(excl), len(vals), m, err, cw, "" if excl != 5 else "<- the frozen policy"))
print()
print("CONVERGENCE of |rho*_R - exact| under refinement, per exclusion policy:")
for k, v in base.items():
    trend = " -> ".join("%.4e" % e for _, e in v)
    mono = all(v[i][1] < v[i - 1][1] for i in range(1, len(v)))
    print("  exclusion %-14s %s    monotone: %s" % (k, trend, "YES" if mono else "NO"))
print()
print("H1 (solver defect) predicts NO exclusion policy converges.")
print("H2 (instrument limit) predicts a smearing-aware exclusion DOES converge.")
