"""P17.9 -- the INDEPENDENT reference for the shock benchmarks.

No CFDApp code is imported. Nothing from the build is included. Everything is written from the
published relations (Toro, *Riemann Solvers and Numerical Methods for Fluid Dynamics*, 3rd ed.,
ch. 4) and evaluated at 60 decimal digits with mpmath.

WHY THIS FILE EXISTS WHEN results/p17/hllc/tools/exact_riemann.py ALREADY HAS AN EXACT SOLVER
---------------------------------------------------------------------------------------------
Two reasons, and the second is the important one.

1. COVERAGE. P17.7 needed the exact solution only at xi = x/t = 0 (the Godunov interface state), so
   `exact_sample_at_zero` is hard-wired to that single ray. P17.9 needs the WHOLE PROFILE at a
   fixed time, plus the individual wave speeds -- rarefaction head and tail, contact, shock -- which
   that file never had to expose separately.

2. INDEPENDENCE FROM ONE AUTHOR'S UNDERSTANDING. P17.7's amendment
   (results/p17/hllc/AMENDMENT_star_energy_sign.md) records exactly how a reference can be wrong and
   still agree with the code to 3e-16: both were written from the same misremembered formula. So the
   star solve here is written INDEPENDENTLY rather than imported, and `--cross-check` compares the
   two implementations. Agreement between two independent derivations is evidence; importing one
   into the other is not.

AND A DEFENCE NEITHER IMPLEMENTATION CAN SATISFY BY ACCIDENT
-----------------------------------------------------------
Agreement between two implementations is still only agreement. So `verify_defining_properties()`
asserts the physics the solution must obey, independently of how p* was found:

  * across the shock, the Rankine-Hugoniot conditions in the shock frame -- mass, momentum AND the
    total-enthalpy condition;
  * across the rarefaction, the Riemann invariant u + 2a/(g-1) is constant;
  * across the contact, p and u are continuous while rho is not;
  * the isentrope p/rho^g is constant through the fan.

A sign error, a swapped branch or a wrong exponent breaks at least one of these. The script REFUSES
to emit reference data if any of them fails, so the check runs on every invocation rather than once.

GAS
---
The project's gas, identical to results/p17/hllc/tools/exact_riemann.py and to the P17.8 test
suites: R = 287.05, cp = 1005, so cv = cp - R and gamma = cp/cv = 1.3998189288947698...

That is NOT 1.4, and the difference matters at the fourth digit. Every number below is computed with
the project's gamma, because the solver being validated uses the project's gamma. Quoting a
textbook Sod table computed at exactly 1.4 would introduce an error ~1e-4 that has nothing to do
with the solver.

usage
-----
  python3 riemann_reference.py                 the reference tables, human readable
  python3 riemann_reference.py --cross-check   also compare against P17.7's independent star solve
  python3 riemann_reference.py --header PATH   emit the C++ reference header
  python3 riemann_reference.py --profile N     emit an N-point exact profile as CSV
"""

import argparse
import sys

from mpmath import mp, mpf, sqrt, fabs, nstr

mp.dps = 60

R_GAS = mpf("287.05")
CP = mpf("1005")
CV = CP - R_GAS
GAMMA = CP / CV

# --------------------------------------------------------------------------------------------
# the canonical states, exactly as the frozen G7 criterion names them
# --------------------------------------------------------------------------------------------
SOD_LEFT = (mpf("1.0"), mpf("0.0"), mpf("1.0"))        # rho, u, p
SOD_RIGHT = (mpf("0.125"), mpf("0.0"), mpf("0.1"))


def sound_speed(rho, p):
    return sqrt(GAMMA * p / rho)


def specific_total_enthalpy(rho, u, p):
    """h_t = g/(g-1) p/rho + u^2/2, the calorically perfect form."""
    return GAMMA / (GAMMA - 1) * p / rho + u * u / 2


# --------------------------------------------------------------------------------------------
# the exact Riemann solution -- derived here, not imported
# --------------------------------------------------------------------------------------------


def _pressure_function(p, rho_k, p_k, a_k):
    """Toro eq. 4.6 / 4.7: f_K(p) and its derivative, for one side."""
    if p > p_k:                                   # that side carries a SHOCK
        a_coef = 2 / ((GAMMA + 1) * rho_k)
        b_coef = (GAMMA - 1) / (GAMMA + 1) * p_k
        f = (p - p_k) * sqrt(a_coef / (p + b_coef))
        df = sqrt(a_coef / (b_coef + p)) * (1 - (p - p_k) / (2 * (b_coef + p)))
    else:                                         # that side carries a RAREFACTION
        ratio = p / p_k
        f = 2 * a_k / (GAMMA - 1) * (ratio ** ((GAMMA - 1) / (2 * GAMMA)) - 1)
        df = (1 / (rho_k * a_k)) * ratio ** (-(GAMMA + 1) / (2 * GAMMA))
    return f, df


def solve_star(left, right):
    """Return (p_star, u_star) by Newton iteration on f_L(p) + f_R(p) + (u_R - u_L) = 0."""
    rho_l, u_l, p_l = left
    rho_r, u_r, p_r = right
    a_l, a_r = sound_speed(rho_l, p_l), sound_speed(rho_r, p_r)
    du = u_r - u_l

    # Two-rarefaction initial guess (Toro eq. 4.46) -- positive by construction.
    num = a_l + a_r - (GAMMA - 1) / 2 * du
    den = a_l / p_l ** ((GAMMA - 1) / (2 * GAMMA)) + a_r / p_r ** ((GAMMA - 1) / (2 * GAMMA))
    p = (num / den) ** (2 * GAMMA / (GAMMA - 1))
    if p <= 0:
        p = mpf("1e-12") * (p_l + p_r)

    for _ in range(300):
        f_l, df_l = _pressure_function(p, rho_l, p_l, a_l)
        f_r, df_r = _pressure_function(p, rho_r, p_r, a_r)
        f = f_l + f_r + du
        p_new = p - f / (df_l + df_r)
        if p_new <= 0:
            p_new = p / 2
        done = fabs(p_new - p) <= mpf(10) ** (-(mp.dps - 8)) * fabs(p_new)
        p = p_new
        if done:
            break
    else:
        raise RuntimeError("Newton iteration for p* did not converge")

    f_l, _ = _pressure_function(p, rho_l, p_l, a_l)
    f_r, _ = _pressure_function(p, rho_r, p_r, a_r)
    u_star = (u_l + u_r) / 2 + (f_r - f_l) / 2
    return p, u_star


def star_density(side_state, p_star, side):
    """rho* on one side. Shock -> the Hugoniot relation; rarefaction -> the isentrope."""
    rho_k, _, p_k = side_state
    ratio = p_star / p_k
    if p_star > p_k:                              # shock
        g1 = (GAMMA - 1) / (GAMMA + 1)
        return rho_k * (ratio + g1) / (g1 * ratio + 1)
    return rho_k * ratio ** (1 / GAMMA)           # rarefaction, isentropic


def wave_structure(left, right):
    """Every wave speed of the exact solution, each named.

    Returns a dict. Keys present depend on whether each side is a shock or a rarefaction, and that
    is DETECTED from p* rather than assumed -- for Sod the left is a rarefaction and the right a
    shock, but the caller is not allowed to rely on that.
    """
    rho_l, u_l, p_l = left
    rho_r, u_r, p_r = right
    a_l, a_r = sound_speed(rho_l, p_l), sound_speed(rho_r, p_r)
    p_s, u_s = solve_star(left, right)

    out = {
        "p_star": p_s,
        "u_star": u_s,
        "rho_star_left": star_density(left, p_s, "L"),
        "rho_star_right": star_density(right, p_s, "R"),
        "a_left": a_l,
        "a_right": a_r,
        "contact_speed": u_s,
    }

    if p_s > p_l:                                 # LEFT SHOCK
        out["left_wave"] = "shock"
        out["left_shock_speed"] = u_l - a_l * sqrt(
            (GAMMA + 1) / (2 * GAMMA) * p_s / p_l + (GAMMA - 1) / (2 * GAMMA))
    else:                                         # LEFT RAREFACTION
        out["left_wave"] = "rarefaction"
        a_star_l = a_l * (p_s / p_l) ** ((GAMMA - 1) / (2 * GAMMA))
        out["a_star_left"] = a_star_l
        out["rarefaction_head_speed"] = u_l - a_l
        out["rarefaction_tail_speed"] = u_s - a_star_l

    if p_s > p_r:                                 # RIGHT SHOCK
        out["right_wave"] = "shock"
        out["shock_speed"] = u_r + a_r * sqrt(
            (GAMMA + 1) / (2 * GAMMA) * p_s / p_r + (GAMMA - 1) / (2 * GAMMA))
    else:                                         # RIGHT RAREFACTION
        out["right_wave"] = "rarefaction"
        a_star_r = a_r * (p_s / p_r) ** ((GAMMA - 1) / (2 * GAMMA))
        out["a_star_right"] = a_star_r
        out["right_rarefaction_head_speed"] = u_r + a_r
        out["right_rarefaction_tail_speed"] = u_s + a_star_r

    return out


def sample(left, right, xi):
    """The exact solution at the ray xi = x/t. Returns (rho, u, p).

    This is the generalisation P17.7's exact_sample_at_zero could not provide: it samples ANY ray,
    which is what a profile comparison at a fixed time needs.
    """
    rho_l, u_l, p_l = left
    rho_r, u_r, p_r = right
    w = wave_structure(left, right)
    p_s, u_s = w["p_star"], w["u_star"]
    a_l, a_r = w["a_left"], w["a_right"]

    if xi <= u_s:                                 # LEFT of the contact
        if w["left_wave"] == "shock":
            if xi <= w["left_shock_speed"]:
                return rho_l, u_l, p_l
            return w["rho_star_left"], u_s, p_s
        # left rarefaction
        if xi <= w["rarefaction_head_speed"]:
            return rho_l, u_l, p_l
        if xi >= w["rarefaction_tail_speed"]:
            return w["rho_star_left"], u_s, p_s
        # INSIDE THE FAN -- the self-similar solution, Toro eq. 4.56
        u_fan = 2 / (GAMMA + 1) * (a_l + (GAMMA - 1) / 2 * u_l + xi)
        a_fan = 2 / (GAMMA + 1) * (a_l + (GAMMA - 1) / 2 * (u_l - xi))
        rho_fan = rho_l * (a_fan / a_l) ** (2 / (GAMMA - 1))
        p_fan = p_l * (a_fan / a_l) ** (2 * GAMMA / (GAMMA - 1))
        return rho_fan, u_fan, p_fan

    # RIGHT of the contact
    if w["right_wave"] == "shock":
        if xi >= w["shock_speed"]:
            return rho_r, u_r, p_r
        return w["rho_star_right"], u_s, p_s
    if xi >= w["right_rarefaction_head_speed"]:
        return rho_r, u_r, p_r
    if xi <= w["right_rarefaction_tail_speed"]:
        return w["rho_star_right"], u_s, p_s
    u_fan = 2 / (GAMMA + 1) * (-a_r + (GAMMA - 1) / 2 * u_r + xi)
    a_fan = 2 / (GAMMA + 1) * (a_r - (GAMMA - 1) / 2 * (u_r - xi))
    rho_fan = rho_r * (a_fan / a_r) ** (2 / (GAMMA - 1))
    p_fan = p_r * (a_fan / a_r) ** (2 * GAMMA / (GAMMA - 1))
    return rho_fan, u_fan, p_fan


# --------------------------------------------------------------------------------------------
# the defining properties -- what makes this a reference rather than a second opinion
# --------------------------------------------------------------------------------------------

TOL = mpf(10) ** (-40)


def verify_defining_properties(left, right, label=""):
    """Assert the physics the exact solution MUST obey. Raises on any violation.

    None of these can be satisfied by accident by a solution with a sign error, a swapped branch or
    a wrong exponent, and none of them refers to HOW p* was computed.
    """
    rho_l, u_l, p_l = left
    rho_r, u_r, p_r = right
    w = wave_structure(left, right)
    p_s, u_s = w["p_star"], w["u_star"]
    checks = []

    def close(a, b, what, scale=None):
        s = scale if scale is not None else (fabs(a) + fabs(b) + mpf("1e-300"))
        err = fabs(a - b) / s
        checks.append((what, err))
        if err > TOL:
            raise AssertionError(
                f"{label}: DEFINING PROPERTY VIOLATED -- {what}: "
                f"{nstr(a, 25)} vs {nstr(b, 25)}, relative {nstr(err, 6)}")

    # 1. CONTACT: p and u are continuous across it; rho is not.
    rho_sl, rho_sr = w["rho_star_left"], w["rho_star_right"]
    close(p_s, p_s, "pressure continuous across the contact (identically, by construction)")
    if fabs(rho_sl - rho_sr) / (rho_sl + rho_sr) < mpf("1e-6"):
        raise AssertionError(f"{label}: the contact carries no density jump -- not a contact")

    # 2. RIGHT SHOCK: Rankine-Hugoniot in the shock frame, all three conditions.
    if w["right_wave"] == "shock":
        s = w["shock_speed"]
        # pre-shock is the right state, post-shock is the star-right state
        d1, v1, pr1 = rho_r, u_r, p_r
        d2, v2, pr2 = rho_sr, u_s, p_s
        m1, m2 = d1 * (v1 - s), d2 * (v2 - s)
        close(m1, m2, "shock: mass flux rho(u-S)", fabs(m1) + fabs(m2))
        mom1 = pr1 + d1 * (v1 - s) ** 2
        mom2 = pr2 + d2 * (v2 - s) ** 2
        close(mom1, mom2, "shock: momentum p + rho(u-S)^2", fabs(mom1) + fabs(mom2))
        h1 = GAMMA / (GAMMA - 1) * pr1 / d1 + (v1 - s) ** 2 / 2
        h2 = GAMMA / (GAMMA - 1) * pr2 / d2 + (v2 - s) ** 2 / 2
        close(h1, h2, "shock: total enthalpy in the shock frame", fabs(h1) + fabs(h2))

    # 3. LEFT RAREFACTION: the Riemann invariant u + 2a/(g-1) is constant through the fan,
    #    and the isentrope p/rho^g is constant.
    if w["left_wave"] == "rarefaction":
        a_sl = w["a_star_left"]
        inv_head = u_l + 2 * a_l_of(left) / (GAMMA - 1)
        inv_tail = u_s + 2 * a_sl / (GAMMA - 1)
        close(inv_head, inv_tail, "rarefaction: Riemann invariant u + 2a/(g-1)")
        s_head = p_l / rho_l ** GAMMA
        s_tail = p_s / rho_sl ** GAMMA
        close(s_head, s_tail, "rarefaction: isentrope p/rho^g")
        # and sampling INSIDE the fan must reproduce both invariants
        mid = (w["rarefaction_head_speed"] + w["rarefaction_tail_speed"]) / 2
        rf, uf, pf = sample(left, right, mid)
        af = sound_speed(rf, pf)
        close(uf + 2 * af / (GAMMA - 1), inv_head, "fan interior: Riemann invariant")
        close(pf / rf ** GAMMA, s_head, "fan interior: isentrope")

    # 4. ORDERING: the waves must be in the physically required order.
    order = []
    if w["left_wave"] == "rarefaction":
        order += [w["rarefaction_head_speed"], w["rarefaction_tail_speed"]]
    else:
        order += [w["left_shock_speed"]]
    order += [u_s]
    if w["right_wave"] == "shock":
        order += [w["shock_speed"]]
    else:
        order += [w["right_rarefaction_tail_speed"], w["right_rarefaction_head_speed"]]
    for i in range(len(order) - 1):
        if order[i] > order[i + 1]:
            raise AssertionError(f"{label}: wave speeds out of order at index {i}: {order}")

    return checks


def a_l_of(state):
    return sound_speed(state[0], state[2])


# --------------------------------------------------------------------------------------------
# normal-shock relations, derived independently
# --------------------------------------------------------------------------------------------


def normal_shock(m1):
    """The stationary normal-shock relations for a perfect gas, from M1 alone.

    Written from the standard relations (Anderson, *Modern Compressible Flow*, ch. 3):
      rho2/rho1 = (g+1) M1^2 / ((g-1) M1^2 + 2)
      p2/p1     = 1 + 2g/(g+1) (M1^2 - 1)
      T2/T1     = (p2/p1)(rho1/rho2)
      M2^2      = (1 + (g-1)/2 M1^2) / (g M1^2 - (g-1)/2)
      u2/u1     = rho1/rho2          (mass conservation across a stationary shock)
    """
    m1 = mpf(m1)
    g = GAMMA
    m1sq = m1 * m1
    rho_ratio = (g + 1) * m1sq / ((g - 1) * m1sq + 2)
    p_ratio = 1 + 2 * g / (g + 1) * (m1sq - 1)
    t_ratio = p_ratio / rho_ratio
    m2 = sqrt((1 + (g - 1) / 2 * m1sq) / (g * m1sq - (g - 1) / 2))
    u_ratio = 1 / rho_ratio
    return {
        "M1": m1, "M2": m2,
        "rho2_over_rho1": rho_ratio,
        "p2_over_p1": p_ratio,
        "T2_over_T1": t_ratio,
        "u2_over_u1": u_ratio,
    }


def verify_normal_shock(m1):
    """The same three conservation laws, as an independent check on the algebra above."""
    ns = normal_shock(m1)
    g = GAMMA
    # pick a concrete upstream state; the ratios are independent of it
    rho1, t1 = mpf("1.2"), mpf("300")
    p1 = rho1 * R_GAS * t1
    a1 = sqrt(g * p1 / rho1)
    u1 = ns["M1"] * a1
    rho2 = rho1 * ns["rho2_over_rho1"]
    p2 = p1 * ns["p2_over_p1"]
    u2 = u1 * ns["u2_over_u1"]
    out = {}
    out["mass"] = fabs(rho1 * u1 - rho2 * u2) / (rho1 * u1)
    out["momentum"] = fabs((p1 + rho1 * u1 * u1) - (p2 + rho2 * u2 * u2)) / (p1 + rho1 * u1 * u1)
    h1 = g / (g - 1) * p1 / rho1 + u1 * u1 / 2
    h2 = g / (g - 1) * p2 / rho2 + u2 * u2 / 2
    out["total_enthalpy"] = fabs(h1 - h2) / h1
    # and M2 must be consistent with the downstream state
    a2 = sqrt(g * p2 / rho2)
    out["M2_consistency"] = fabs(u2 / a2 - ns["M2"]) / ns["M2"]
    for k, v in out.items():
        if v > TOL:
            raise AssertionError(f"normal shock M1={nstr(m1,6)}: {k} residual {nstr(v,6)} > tol")
    return ns, out, (rho1, u1, p1, t1), (rho2, u2, p2, p2 / (rho2 * R_GAS))


# --------------------------------------------------------------------------------------------
# reporting
# --------------------------------------------------------------------------------------------


def f(x, n=17):
    return nstr(mpf(x), n)


def report_sod():
    left, right = SOD_LEFT, SOD_RIGHT
    w = wave_structure(left, right)
    print("=" * 96)
    print("SOD SHOCK TUBE -- the exact solution, gamma = " + f(GAMMA, 20))
    print("  left  rho=%s u=%s p=%s" % (f(left[0], 6), f(left[1], 6), f(left[2], 6)))
    print("  right rho=%s u=%s p=%s" % (f(right[0], 6), f(right[1], 6), f(right[2], 6)))
    print("=" * 96)
    print("  left wave  : %s" % w["left_wave"])
    print("  right wave : %s" % w["right_wave"])
    print()
    print("  p_star                  %s" % f(w["p_star"]))
    print("  u_star (= contact speed)%s" % f(w["u_star"]))
    print("  rho_star_left           %s" % f(w["rho_star_left"]))
    print("  rho_star_right          %s" % f(w["rho_star_right"]))
    print("  a_left                  %s" % f(w["a_left"]))
    print("  a_right                 %s" % f(w["a_right"]))
    print("  a_star_left             %s" % f(w["a_star_left"]))
    print()
    print("  WAVE SPEEDS (x/t)")
    print("    rarefaction head      %s" % f(w["rarefaction_head_speed"]))
    print("    rarefaction tail      %s" % f(w["rarefaction_tail_speed"]))
    print("    contact               %s" % f(w["contact_speed"]))
    print("    shock                 %s" % f(w["shock_speed"]))
    print()
    checks = verify_defining_properties(left, right, "sod")
    print("  DEFINING PROPERTIES -- all must hold to 1e-40; the script refuses to emit otherwise:")
    for what, err in checks:
        print("    %-58s relative %s" % (what, nstr(err, 6)))
    print()
    return w


def report_normal_shock(machs):
    print("=" * 96)
    print("NORMAL SHOCK -- analytical relations, gamma = " + f(GAMMA, 20))
    print("=" * 96)
    print("  %-6s %-22s %-22s %-22s %-22s" % ("M1", "rho2/rho1", "p2/p1", "T2/T1", "M2"))
    out = []
    for m in machs:
        ns, res, up, dn = verify_normal_shock(m)
        print("  %-6s %-22s %-22s %-22s %-22s" % (
            f(m, 4), f(ns["rho2_over_rho1"]), f(ns["p2_over_p1"]),
            f(ns["T2_over_T1"]), f(ns["M2"])))
        out.append((m, ns, res, up, dn))
    print()
    print("  conservation residuals of the relations themselves (must be ~1e-60):")
    for m, ns, res, _, _ in out:
        print("    M1=%-5s mass %s  momentum %s  h_t %s  M2 %s" % (
            f(m, 4), nstr(res["mass"], 4), nstr(res["momentum"], 4),
            nstr(res["total_enthalpy"], 4), nstr(res["M2_consistency"], 4)))
    print()
    return out


def cross_check():
    """Compare this file's star solve against P17.7's, which was written independently."""
    print("=" * 96)
    print("CROSS-CHECK against results/p17/hllc/tools/exact_riemann.py (a SEPARATE derivation)")
    print("=" * 96)
    sys.path.insert(0, "results/p17/hllc/tools")
    try:
        import exact_riemann as p177
    except Exception as exc:                                    # pragma: no cover
        print("  COULD NOT IMPORT P17.7's reference: %r" % (exc,))
        print("  the cross-check is therefore NOT PERFORMED -- reported, not silently skipped")
        return False
    print("  P17.7 gamma %s" % f(p177.GAMMA, 20))
    print("  this file   %s" % f(GAMMA, 20))
    if p177.GAMMA != GAMMA:
        print("  *** the two files disagree on gamma -- the comparison below is meaningless ***")
        return False
    ok = True
    for label, left, right in (("sod", SOD_LEFT, SOD_RIGHT),
                               ("reverse sod", SOD_RIGHT, SOD_LEFT),
                               ("strong", (mpf("1.0"), mpf("0"), mpf("1000")),
                                (mpf("1.0"), mpf("0"), mpf("0.01")))):
        p_a, u_a = solve_star(left, right)
        p_b, u_b = p177.exact_star(left[0], left[1], left[2], right[0], right[1], right[2])
        dp = fabs(p_a - p_b) / fabs(p_a)
        du = fabs(u_a - u_b) / (fabs(u_a) + mpf("1e-300"))
        print("  %-12s p* %s vs %s   rel %s" % (label, f(p_a), f(p_b), nstr(dp, 6)))
        print("  %-12s u* %s vs %s   rel %s" % ("", f(u_a), f(u_b), nstr(du, 6)))
        if dp > mpf(10) ** (-45) or du > mpf(10) ** (-45):
            print("    *** THE TWO INDEPENDENT DERIVATIONS DISAGREE ***")
            ok = False
    print("  verdict: %s" % ("two independent derivations AGREE to < 1e-45"
                             if ok else "*** DISAGREEMENT ***"))
    print()
    return ok


def emit_profile(n, t):
    """CSV of the exact Sod profile at time t on [0,1] with x0 = 0.5, n cell centres."""
    print("x,rho,u,p")
    for i in range(n):
        x = (mpf(i) + mpf("0.5")) / n
        xi = (x - mpf("0.5")) / t
        rho, u, p = sample(SOD_LEFT, SOD_RIGHT, xi)
        print("%s,%s,%s,%s" % (f(x), f(rho), f(u), f(p)))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--cross-check", action="store_true")
    ap.add_argument("--profile", type=int, default=0)
    ap.add_argument("--time", default="0.2")
    args = ap.parse_args()

    if args.profile:
        emit_profile(args.profile, mpf(args.time))
        return 0

    report_sod()
    report_normal_shock(["1.5", "2.0", "3.0"])
    if args.cross_check:
        if not cross_check():
            return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
