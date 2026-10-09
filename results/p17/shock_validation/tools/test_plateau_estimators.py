"""P17.9 Amendment 3 -- does ANY estimator isolate the star-right density from contact smearing?

Investigation before amending. If none converges, S6 stays FAILED (the authorization is explicit).
Reads only the profile CSVs the benchmark already wrote; changes nothing.

CONTAMINATION HYPOTHESIS, stated so it can be refuted:
  the star-right window is bounded by the CONTACT on the left and the SHOCK on the right. Both
  smear. A contact has a density jump but NO pressure or velocity jump, so its tail can corrupt a
  density measurement while leaving p and u untouched -- which is exactly the observed pattern.
  An estimator that finds the FLATTEST sub-window should therefore isolate the plateau from both.
"""
import csv, math

EXACT = 0.26560611841121122
CONTACT = 0.68550398701451403
SHOCK = 0.85041856284399202

def load(nx):
    rows = list(csv.DictReader(open("results/p17/shock_validation/data/sod_profile_nx%d.csv" % nx)))
    return ([float(r["x"]) for r in rows], [float(r["rho_num"]) for r in rows],
            [float(r["p_num"]) for r in rows], 1.0 / nx)

def window(x, q, lo, hi):
    return [q[i] for i in range(len(x)) if lo <= x[i] <= hi]

def mean_excl(x, q, dx, excl):
    v = window(x, q, CONTACT + excl * dx, SHOCK - excl * dx)
    return sum(v) / len(v) if len(v) >= 5 else None

def flattest(x, q, dx, span=5, gap=2):
    """Mean of the span-cell sub-window with the SMALLEST max-min spread, between the two waves."""
    idx = [i for i in range(len(x)) if CONTACT + gap * dx <= x[i] <= SHOCK - gap * dx]
    if len(idx) < span:
        return None, None
    best, bestspread = None, None
    for s in range(len(idx) - span + 1):
        w = [q[idx[s + k]] for k in range(span)]
        spread = max(w) - min(w)
        if bestspread is None or spread < bestspread:
            bestspread, best = spread, sum(w) / span
    return best, bestspread

print("%-6s %-26s %-14s %-12s" % ("nx", "estimator", "rho*_R", "|err|"))
res = {}
for nx in (200, 400, 800):
    x, rho, p, dx = load(nx)
    cands = [("mean, exclude 5 (frozen)", mean_excl(x, rho, dx, 5)),
             ("mean, exclude 10", mean_excl(x, rho, dx, 10)),
             ("mean, exclude 20", mean_excl(x, rho, dx, 20)),
             ("flattest 5-cell window", flattest(x, rho, dx, 5)[0]),
             ("flattest 9-cell window", flattest(x, rho, dx, 9)[0])]
    for name, val in cands:
        if val is None:
            print("%-6d %-26s window too narrow" % (nx, name)); continue
        err = abs(val - EXACT)
        res.setdefault(name, []).append(err)
        print("%-6d %-26s %-14.10f %-12.5e" % (nx, name, val, err))
    print()

print("CONVERGENCE under refinement (200 -> 400 -> 800):")
for name, errs in res.items():
    mono = all(errs[i] < errs[i-1] for i in range(1, len(errs)))
    rate = math.log(errs[0] / errs[-1]) / math.log(4.0) if errs[-1] > 0 else float('inf')
    print("  %-26s %s   monotone: %-4s  overall rate: %.3f"
          % (name, " -> ".join("%.4e" % e for e in errs), "YES" if mono else "NO", rate))
print()
print("CONTROL -- the same estimators on PRESSURE, which the contact CANNOT contaminate")
print("(a contact has no pressure jump). If p converges under the frozen 5-cell policy while rho")
print("does not, the contamination is demonstrated objectively rather than asserted.")
EXACT_P = 0.30313762666080176
pres = {}
for nx in (200, 400, 800):
    x, rho, p, dx = load(nx)
    for name, val in [("mean, exclude 5 (frozen)", mean_excl(x, p, dx, 5)),
                      ("flattest 9-cell window", flattest(x, p, dx, 9)[0])]:
        pres.setdefault(name, []).append(abs(val - EXACT_P))
for name, errs in pres.items():
    mono = all(errs[i] < errs[i-1] for i in range(1, len(errs)))
    rate = math.log(errs[0] / errs[-1]) / math.log(4.0)
    print("  p*  %-26s %s   monotone: %-4s  rate: %.3f"
          % (name, " -> ".join("%.4e" % e for e in errs), "YES" if mono else "NO", rate))
