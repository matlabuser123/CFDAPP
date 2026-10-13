"""P17.9 -- the MANDATORY dry-run of the acceptance gate, before it is frozen.

CLAUDE.md §5.2 requires every criterion to be run against the unchanged baseline before freezing,
and §5.2's last paragraph names the exact situation P17.9 is in:

    "If the baseline cannot exercise the new behaviour being gated, the dry-run is insufficient.
     Ask separately whether a CORRECT post-change implementation can satisfy the criterion, and add
     a control that can fail."

No benchmark exists yet, so there is no numerical profile to dry-run against. What CAN be dry-run --
and what this file does -- is the MEASUREMENT APPARATUS, on inputs whose right answer is known
exactly:

  A  the detectors on the EXACT (sharp) Sod profile
        -> they must return the analytical wave positions. If they cannot measure a perfect
           profile, no number they produce from a numerical one means anything.

  B  the detectors on a SYNTHETICALLY SMEARED profile
        -> this is §5.2's "can a correct implementation satisfy the criterion?" question, answered
           without needing the solver. A first-order scheme returns a monotone profile smeared over
           a few cells; if the frozen 2 dx bound were unachievable for such a profile, the gate
           would be impossible rather than strict, and freezing it would be a mistake.

  C  the detectors on a profile SHIFTED by 3 dx      (gate control V3)
        -> they must report the shift. A detector that always returns the analytical answer would
           pass every position criterion.

  D  the shock detector on a smooth ramp WITH NO JUMP (gate control V4)
        -> it must report NO SHOCK rather than inventing a position.

  E  the plateau sampling policy on the exact profile
        -> it must recover p*, u*, rho*_L, rho*_R essentially exactly.

The detectors implemented here are the SAME ALGORITHMS the C++ benchmark will use, specified in
acceptance_gate.md §2 and §3. The C++ implementation is checked against this file's outputs on the
identical synthetic inputs, so a divergence between the two is caught rather than assumed away.
"""

import sys

from mpmath import mp, mpf, fabs, nstr

sys.path.insert(0, "results/p17/shock_validation/tools")
from riemann_reference import (GAMMA, SOD_LEFT, SOD_RIGHT, sample, wave_structure, f)

mp.dps = 50

T_END = mpf("0.2")
X0 = mpf("0.5")


# --------------------------------------------------------------------------------------------
# the detectors, exactly as acceptance_gate.md §2 fixes them
# --------------------------------------------------------------------------------------------


def crossing_position(xs, qs, q_pre, q_post, max_transition_cells=None):
    """The 50 % crossing of a JUMP, linearly interpolated between cell centres.

    Returns None when there is no jump to find -- which is how 'no such wave' is reported, rather
    than by returning a plausible-looking number.

    THE TRANSITION-WIDTH TEST IS NOT OPTIONAL, and the first version of this function omitted it.
    A 50 % crossing exists on ANY monotone profile joining q_pre to q_post, including a smooth ramp
    with no discontinuity anywhere -- the dry-run's control V4 duly got a "shock" at x = 0.9176 on a
    straight line (PRESERVED_01_dryrun_detectors_broken.log). A detector that cannot tell a jump
    from a slope makes every position criterion vacuous.

    So a crossing is accepted only when it joins two PLATEAUS: the mean over 5 cells starting 5
    cells before the crossing must sit within 10 % of the jump of q_pre, and likewise after for
    q_post. A discontinuity has flat states either side of it; a slope does not.
    """
    mid = (q_pre + q_post) / 2
    rising = q_post > q_pre
    for i in range(len(xs) - 1):
        a, b = qs[i], qs[i + 1]
        lo, hi = (a, b) if a <= b else (b, a)
        if lo <= mid <= hi and a != b:
            if (rising and b > a) or ((not rising) and b < a):
                if max_transition_cells is not None:
                    # A JUMP IS A TRANSITION BETWEEN TWO PLATEAUS. That is the discriminator, and
                    # the transition-WIDTH test that preceded it was not enough: a gentle ramp
                    # crosses the shock's own 10-90 band in only ~26 cells at nx = 200, because that
                    # band is narrow compared with the ramp's full range, so a width limit of nx/5
                    # still admitted it. The plateau test asks the question that actually
                    # distinguishes the two, and it is the same property gate criterion W5 names.
                    jump = fabs(q_post - q_pre)
                    tol = jump / 10
                    gap, span = 5, 5
                    lo_a, lo_b = i - gap - span + 1, i - gap
                    hi_a, hi_b = i + gap, i + gap + span - 1
                    if lo_a < 0 or hi_b >= len(qs):
                        return None
                    pre = sum(qs[lo_a:lo_b + 1]) / (lo_b - lo_a + 1)
                    post = sum(qs[hi_a:hi_b + 1]) / (hi_b - hi_a + 1)
                    if fabs(pre - q_pre) > tol or fabs(post - q_post) > tol:
                        return None
                t = (mid - a) / (b - a)
                return xs[i] + t * (xs[i + 1] - xs[i])
    return None


def fan_extent(xs, rhos, x_max, rho_outer, rho_inner, frac=mpf("0.01")):
    """The rarefaction fan's head and tail, CONFINED to x < x_max.

    head = the first abscissa where rho has departed from the outer plateau (rho_outer) by more
           than `frac` of the fan's own total drop
    tail = the last abscissa where rho has not yet reached the inner plateau (rho_inner) to within
           the same tolerance

    THE CONFINEMENT IS THE FIX. The first version selected every cell that differed from BOTH
    plateaus, which swept in the star-RIGHT region between the contact and the shock -- it differs
    from rho_L and from rho*_L too. The fan came out at 147 cells against an exact 44.5 and the tail
    was wrong by 102 dx. `x_max` is the DETECTED contact position, never the analytical one, so the
    detector still never sees the expected answer (authorization section 28).
    """
    drop = fabs(rho_outer - rho_inner)
    if drop == 0:
        return None, None, 0
    tol = frac * drop
    idx = [i for i in range(len(xs)) if xs[i] < x_max]
    if len(idx) < 3:
        return None, None, 0
    head_i = None
    for i in idx:
        if fabs(rhos[i] - rho_outer) > tol:
            head_i = i
            break
    # THE TAIL IS THE FIRST ARRIVAL AT THE INNER PLATEAU, SCANNING FORWARD FROM THE HEAD --
    # not the last departure from it scanning backward. The backward form failed on the smeared
    # profile by 40 dx: the CONTACT's smearing bleeds leftward into the star-left plateau, so the
    # last cell differing from rho*_L sits next to the contact, not at the end of the fan. Scanning
    # forward asks "where does the fan finish?", which is what the tail IS, and the contact's
    # influence further right cannot affect a first arrival.
    tail_i = None
    if head_i is not None:
        for i in idx:
            if i > head_i and fabs(rhos[i] - rho_inner) <= tol:
                tail_i = i
                break
    if head_i is None or tail_i is None or tail_i < head_i:
        return None, None, 0
    head = xs[head_i]
    if head_i > 0:
        span = fabs(rhos[head_i] - rhos[head_i - 1])
        if span > 0:
            frac_back = (fabs(rhos[head_i] - rho_outer) - tol) / span
            head = xs[head_i] - (xs[head_i] - xs[head_i - 1]) * min(max(frac_back, mpf(0)), mpf(1))
    tail = xs[tail_i]
    if tail_i > 0:
        span = fabs(rhos[tail_i] - rhos[tail_i - 1])
        if span > 0:
            # the arrival happened between tail_i-1 and tail_i; interpolate back to the crossing
            frac_back = (tol - fabs(rhos[tail_i] - rho_inner)) / span
            tail = xs[tail_i] - (xs[tail_i] - xs[tail_i - 1]) * min(max(frac_back, mpf(0)), mpf(1))
    return head, tail, tail_i - head_i + 1


def plateau_mean(xs, qs, x_lo, x_hi, dx, exclude=5):
    """Mean of q over (x_lo, x_hi) with `exclude` cells trimmed from each end.

    Returns (mean, n_used). n_used < 5 means the window is TOO NARROW and the caller must fail the
    criterion rather than widen it (acceptance_gate.md §3).
    """
    lo = x_lo + exclude * dx
    hi = x_hi - exclude * dx
    vals = [qs[i] for i in range(len(xs)) if lo <= xs[i] <= hi]
    if len(vals) < 5:
        return None, len(vals)
    return sum(vals) / len(vals), len(vals)


# --------------------------------------------------------------------------------------------
# profile builders
# --------------------------------------------------------------------------------------------


def exact_profile(nx, t=T_END):
    dx = mpf(1) / nx
    xs, rho, u, p = [], [], [], []
    for i in range(nx):
        x = (mpf(i) + mpf("0.5")) * dx
        r, uu, pp = sample(SOD_LEFT, SOD_RIGHT, (x - X0) / t)
        xs.append(x); rho.append(r); u.append(uu); p.append(pp)
    return xs, rho, u, p, dx


def smear(values, passes):
    """A monotone 3-point average, applied `passes` times.

    This is a STAND-IN for numerical diffusion, not a model of it. Its only job is to produce the
    qualitative thing a first-order scheme produces -- a monotone profile spread over a few cells --
    so the achievability of the 2 dx bound can be tested before the solver exists.
    """
    out = list(values)
    for _ in range(passes):
        nxt = list(out)
        for i in range(1, len(out) - 1):
            nxt[i] = (out[i - 1] + 2 * out[i] + out[i + 1]) / 4
        out = nxt
    return out


def shift(values, cells):
    """Rigidly translate a profile by `cells` cells (gate control V3)."""
    n = len(values)
    out = list(values)
    for i in range(n):
        j = i - cells
        out[i] = values[min(max(j, 0), n - 1)]
    return out


# --------------------------------------------------------------------------------------------
# the dry-run
# --------------------------------------------------------------------------------------------

W = wave_structure(SOD_LEFT, SOD_RIGHT)
EXACT = {
    "rarefaction_head": X0 + W["rarefaction_head_speed"] * T_END,
    "rarefaction_tail": X0 + W["rarefaction_tail_speed"] * T_END,
    "contact": X0 + W["contact_speed"] * T_END,
    "shock": X0 + W["shock_speed"] * T_END,
}

failures = []


def check(label, ok, detail=""):
    print("    %-62s %s %s" % (label, "OK  " if ok else "*** FAIL ***", detail))
    if not ok:
        failures.append(label)


def measure(xs, rho, u, p, dx):
    """Run every detector, in the order the later ones depend on.

    The CONTACT is located first because the fan search is confined to the region left of it. Every
    detector sees only the numerical profile; none is given an expected position.
    """
    nx = len(xs)
    max_cells = nx // 5
    out = {}
    out["contact"] = crossing_position(xs, rho, W["rho_star_left"], W["rho_star_right"], max_cells)
    out["shock"] = crossing_position(xs, rho, W["rho_star_right"], SOD_RIGHT[0], max_cells)
    x_max = out["contact"] if out["contact"] is not None else xs[-1]
    head, tail, ncell = fan_extent(xs, rho, x_max, SOD_LEFT[0], W["rho_star_left"])
    out["rarefaction_head"] = head
    out["rarefaction_tail"] = tail
    out["fan_cells"] = ncell
    return out


def run_case(title, xs, rho, u, p, dx, expect_within, expect_shock=True):
    print("  %s" % title)
    m = measure(xs, rho, u, p, dx)
    if not expect_shock:
        check("shock detector reports NO SHOCK on a jumpless profile",
              m["shock"] is None, "got %s" % (f(m["shock"], 8) if m["shock"] is not None else "None"))
        return m
    for wave in ("rarefaction_head", "rarefaction_tail", "contact", "shock"):
        got = m[wave]
        if got is None:
            check("%s detected" % wave, False, "detector returned None")
            continue
        err = fabs(got - EXACT[wave])
        lim = expect_within[wave] * dx
        check("%-18s err %8s = %6s dx  (limit %s dx)" % (
                  wave, nstr(err, 4), nstr(err / dx, 4), nstr(expect_within[wave], 3)),
              err <= lim)
    return m


print("=" * 100)
print("P17.9 GATE DRY-RUN -- the measurement apparatus, before the gate is frozen")
print("=" * 100)
print("exact positions at t = %s, x0 = %s:" % (f(T_END, 4), f(X0, 4)))
for k, v in EXACT.items():
    print("    %-18s %s" % (k, f(v, 14)))
print()

NX = 200
xs, rho, u, p, dx = exact_profile(NX)
print("nx = %d, dx = %s" % (NX, f(dx, 8)))
print()

print("A. the detectors on the EXACT (sharp) profile -- must return the analytical positions")
mA = run_case("", xs, rho, u, p, dx, {"rarefaction_head": 2, "rarefaction_tail": 4,
                                      "contact": 2, "shock": 2})
check("W2: the fan spans >= 3 cells", mA["fan_cells"] >= 3, "measured %d cells" % mA["fan_cells"])
print()

print("B. the detectors on a SMEARED profile -- can a CORRECT first-order result meet 2 dx?")
for passes in (2, 5, 10):
    sm_rho = smear(rho, passes)
    sm_u = smear(u, passes)
    sm_p = smear(p, passes)
    print("  smearing passes = %d" % passes)
    run_case("", xs, sm_rho, sm_u, sm_p, dx,
             {"rarefaction_head": 2, "rarefaction_tail": 4, "contact": 2, "shock": 2})
print()

print("C. CONTROL V3 -- the profile shifted by +3 dx. The position criteria MUST break.")
sh_rho = shift(rho, 3)
mC = measure(xs, sh_rho, u, p, dx)
broke = 0
for wave in ("contact", "shock"):
    got = mC[wave]
    err = fabs(got - EXACT[wave]) if got is not None else None
    detected = got is not None and err > 2 * dx
    print("    %-18s err %s = %s dx  -> %s" % (
        wave, nstr(err, 4) if err is not None else "None",
        nstr(err / dx, 4) if err is not None else "-",
        "DETECTED (criterion breaks)" if detected else "*** NOT DETECTED ***"))
    if detected:
        broke += 1
check("V3: a 3 dx shift breaks the position criteria", broke == 2, "%d of 2 waves" % broke)
print()

print("D. CONTROL V4 -- a monotone ramp with NO jump. The shock detector must report NO SHOCK.")
ramp = [SOD_LEFT[0] + (SOD_RIGHT[0] - SOD_LEFT[0]) * mpf(i) / (NX - 1) for i in range(NX)]
run_case("", xs, ramp, u, p, dx, {}, expect_shock=False)
print()

print("E. the plateau SAMPLING POLICY on the exact profile -- must recover the star states")
star_l, n_l = plateau_mean(xs, rho, EXACT["rarefaction_tail"], EXACT["contact"], dx)
star_r, n_r = plateau_mean(xs, rho, EXACT["contact"], EXACT["shock"], dx)
p_l, _ = plateau_mean(xs, p, EXACT["rarefaction_tail"], EXACT["contact"], dx)
u_l, _ = plateau_mean(xs, u, EXACT["rarefaction_tail"], EXACT["contact"], dx)
pre, n_pre = plateau_mean(xs, rho, EXACT["shock"], mpf(1), dx)
for label, got, want, n in (("rho_star_left", star_l, W["rho_star_left"], n_l),
                            ("rho_star_right", star_r, W["rho_star_right"], n_r),
                            ("p_star", p_l, W["p_star"], n_l),
                            ("u_star", u_l, W["u_star"], n_l),
                            ("pre-shock rho", pre, SOD_RIGHT[0], n_pre)):
    if got is None:
        check("%s window wide enough" % label, False, "only %d cells" % n)
        continue
    rel = fabs(got - want) / fabs(want)
    check("%-16s %s vs %s  rel %s  (%d cells)" % (label, f(got, 10), f(want, 10), nstr(rel, 4), n),
          rel < mpf("1e-12"))
print()

print("F. window widths at the finest mesh -- the 5-cell exclusion must leave a usable window")
for nxx in (200, 400, 800):
    d = mpf(1) / nxx
    w_l = (EXACT["contact"] - EXACT["rarefaction_tail"]) / d - 10
    w_r = (EXACT["shock"] - EXACT["contact"]) / d - 10
    w_pre = (mpf(1) - EXACT["shock"]) / d - 5
    print("    nx=%-4d star-left %5.1f cells   star-right %5.1f cells   pre-shock %5.1f cells"
          % (nxx, float(w_l), float(w_r), float(w_pre)))
    check("nx=%d windows all >= 5 cells" % nxx, min(w_l, w_r, w_pre) >= 5)
print()

print("=" * 100)
print("DRY-RUN FAILURES: %d" % len(failures))
for fl in failures:
    print("   *** %s" % fl)
print("=" * 100)
sys.exit(1 if failures else 0)
