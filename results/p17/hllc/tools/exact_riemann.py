#!/usr/bin/env python3
"""P17.7 -- the INDEPENDENT reference for G6(f).

CLAUDE.md section 6 and the frozen G6 criterion require the reference for an exact Riemann problem to
be computed OUTSIDE CFDApp, and explicitly FORBID adopting CFDApp output as the reference. This file
therefore contains no CFDApp code, includes nothing from the build, and is written from the published
equations alone. It runs at 60 decimal digits via mpmath.

It produces TWO independent things, and keeping them apart is the whole point:

  (A) THE EXACT RIEMANN SOLUTION -- p*, u*, the full wave structure, and the state sampled at
      xi = x/t = 0, hence the exact (Godunov) interface flux. This is the PHYSICAL TRUTH. Comparing
      HLLC against it measures how well the SCHEME approximates reality, which is a property of HLLC
      itself and not of any implementation of it.

  (B) AN ARBITRARY-PRECISION EVALUATION OF THE HLLC ALGORITHM -- the same formulas the C++ will use,
      at 60 digits. Comparing the C++ against this measures IMPLEMENTATION error only, and its
      tolerance is derived from double-precision round-off rather than from the scheme's accuracy.

Conflating the two is the classic way to either excuse an implementation bug as "scheme error" or to
reject a correct implementation for not matching the exact solution. Both are avoided by construction.

FORMULATION, stated so the evidence does not have to say "standard HLLC":
  wave speeds   Davis (1988) direct bounds, as given in Toro 3rd ed. eq. 10.48:
                    S_L = min(u_nL - a_L, u_nR - a_R)
                    S_R = max(u_nL + a_L, u_nR + a_R)
  contact       Toro, Spruce & Speares (1994); Toro 3rd ed. eq. 10.37:
                    S* = [ p_R - p_L + rho_L u_nL (S_L - u_nL) - rho_R u_nR (S_R - u_nR) ]
                         / [ rho_L (S_L - u_nL) - rho_R (S_R - u_nR) ]
  star states   Toro 3rd ed. eq. 10.39, in normal/tangential form:
                    rho*_K   = rho_K (S_K - u_nK) / (S_K - S*)
                    (rhoU)*_K = rho*_K [ U_K + (S* - u_nK) n ]      tangential preserved
                    (rhoE)*_K = rho*_K [ rhoE_K/rho_K
                                         + (S* - u_nK)(S* + p_K/(rho_K (S_K - u_nK))) ]
                    (the pressure term's sign is a PLUS -- it was a minus when this file was first
                     frozen, and that was a real bug; see AMENDMENT_star_energy_sign.md)
  flux          F*_K = F_K + S_K (Q*_K - Q_K)

usage:  python3 exact_riemann.py            prints the reference table
        python3 exact_riemann.py --json     machine-readable, for the C++ test to embed
"""

import json
import sys

from mpmath import mp, mpf, sqrt, fabs

mp.dps = 60

# The project's gas, from results/p17/low_mach/case_family.md:
#   R = 287.05, cp = 1005  =>  cv = cp - R,  gamma = cp/cv
R_GAS = mpf("287.05")
CP = mpf("1005")
CV = CP - R_GAS
GAMMA = CP / CV


def sound_speed(rho, p):
    """a = sqrt(gamma p / rho), identical to sqrt(gamma R T) through p = rho R T."""
    return sqrt(GAMMA * p / rho)


def total_energy_density(rho, vel, p):
    """rhoE = p/(gamma-1) + 0.5 rho |U|^2.

    NOTE ON THE REFERENCE STATE: this is the ZERO-reference form. P17.5 defines
    e = h(T) - R T with h from P15's reference state, so e = cv T only when
    h_ref = T_ref = 0. Every case here uses the zero reference, which makes the two
    identical -- and the C++ tests construct their states through P17.5's
    conservedTotalEnergy() so that the agreement is verified rather than assumed.
    """
    ke = mpf("0.5") * rho * (vel[0] ** 2 + vel[1] ** 2 + vel[2] ** 2)
    return p / (GAMMA - 1) + ke


def euler_flux(rho, vel, p, n):
    """The directional inviscid Euler flux F_n(Q). Returns [mass, momx, momy, momz, energy]."""
    un = vel[0] * n[0] + vel[1] * n[1] + vel[2] * n[2]
    rho_e = total_energy_density(rho, vel, p)
    return [
        rho * un,
        rho * vel[0] * un + p * n[0],
        rho * vel[1] * un + p * n[1],
        rho * vel[2] * un + p * n[2],
        un * (rho_e + p),
    ]


def conserved(rho, vel, p):
    return [rho, rho * vel[0], rho * vel[1], rho * vel[2], total_energy_density(rho, vel, p)]


# ----------------------------------------------------------------------------------
# (A) THE EXACT RIEMANN SOLUTION -- Toro chapter 4, Newton-Raphson on p*
# ----------------------------------------------------------------------------------


def _f_K(p, rho_k, p_k, a_k):
    """Toro eq. 4.6/4.7: the pressure function for one side, and its derivative."""
    if p > p_k:  # shock
        A = 2 / ((GAMMA + 1) * rho_k)
        B = (GAMMA - 1) / (GAMMA + 1) * p_k
        f = (p - p_k) * sqrt(A / (p + B))
        df = sqrt(A / (B + p)) * (1 - (p - p_k) / (2 * (B + p)))
    else:  # rarefaction
        pr = p / p_k
        f = 2 * a_k / (GAMMA - 1) * (pr ** ((GAMMA - 1) / (2 * GAMMA)) - 1)
        df = (1 / (rho_k * a_k)) * pr ** (-(GAMMA + 1) / (2 * GAMMA))
    return f, df


def exact_star(rho_l, un_l, p_l, rho_r, un_r, p_r):
    """Solve f(p*) = f_L + f_R + (un_R - un_L) = 0 for p*, then u*."""
    a_l, a_r = sound_speed(rho_l, p_l), sound_speed(rho_r, p_r)
    du = un_r - un_l

    # A two-rarefaction initial guess (Toro eq. 4.46), which is positive by construction.
    num = a_l + a_r - (GAMMA - 1) / 2 * du
    den = a_l / p_l ** ((GAMMA - 1) / (2 * GAMMA)) + a_r / p_r ** ((GAMMA - 1) / (2 * GAMMA))
    p = (num / den) ** (2 * GAMMA / (GAMMA - 1))
    if p <= 0:
        p = mpf("1e-12") * (p_l + p_r)

    for _ in range(200):
        f_l, df_l = _f_K(p, rho_l, p_l, a_l)
        f_r, df_r = _f_K(p, rho_r, p_r, a_r)
        f = f_l + f_r + du
        df = df_l + df_r
        step = f / df
        p_new = p - step
        if p_new <= 0:  # keep the iterate admissible
            p_new = p / 2
        converged = fabs(p_new - p) <= mpf(10) ** (-(mp.dps - 8)) * fabs(p_new)
        p = p_new
        if converged:
            break
    else:
        raise RuntimeError("exact Riemann Newton iteration did not converge")

    f_l, _ = _f_K(p, rho_l, p_l, a_l)
    f_r, _ = _f_K(p, rho_r, p_r, a_r)
    u_star = (un_l + un_r) / 2 + (f_r - f_l) / 2
    return p, u_star


def exact_wave_extremes(rho_l, un_l, p_l, rho_r, un_r, p_r):
    """The TRUE leftmost and rightmost wave speeds of the exact solution.

    This is what makes G6(f) gateable with a DERIVED criterion rather than a fitted tolerance. The
    requirement on a wave-speed estimator is not that it be accurate but that it BOUND the true
    signal speeds:  S_L <= (true leftmost)  and  S_R >= (true rightmost).  An estimator that fails
    this lets signal escape the control volume and the scheme is unstable regardless of how small
    the flux error happens to look on one case.
    """
    p_s, u_s = exact_star(rho_l, un_l, p_l, rho_r, un_r, p_r)
    a_l, a_r = sound_speed(rho_l, p_l), sound_speed(rho_r, p_r)

    if p_s > p_l:  # left shock
        left = un_l - a_l * sqrt((GAMMA + 1) / (2 * GAMMA) * p_s / p_l + (GAMMA - 1) / (2 * GAMMA))
    else:          # left rarefaction: the head is the fastest left-going signal
        left = un_l - a_l

    if p_s > p_r:  # right shock
        right = un_r + a_r * sqrt((GAMMA + 1) / (2 * GAMMA) * p_s / p_r + (GAMMA - 1) / (2 * GAMMA))
    else:          # right rarefaction
        right = un_r + a_r

    return left, right


def exact_sample_at_zero(rho_l, un_l, p_l, rho_r, un_r, p_r):
    """Sample the exact solution at xi = x/t = 0. Returns (rho, un, p) there.

    This is the Godunov state; F(that state) is the exact interface flux.
    """
    p_s, u_s = exact_star(rho_l, un_l, p_l, rho_r, un_r, p_r)
    a_l, a_r = sound_speed(rho_l, p_l), sound_speed(rho_r, p_r)

    if u_s >= 0:  # the interface is on the LEFT of the contact
        if p_s > p_l:  # left shock
            s_l = un_l - a_l * sqrt((GAMMA + 1) / (2 * GAMMA) * p_s / p_l
                                    + (GAMMA - 1) / (2 * GAMMA))
            if s_l >= 0:
                return rho_l, un_l, p_l
            pr = p_s / p_l
            rho_s = rho_l * (pr + (GAMMA - 1) / (GAMMA + 1)) / ((GAMMA - 1) / (GAMMA + 1) * pr + 1)
            return rho_s, u_s, p_s
        # left rarefaction
        sh_l = un_l - a_l                      # head
        if sh_l >= 0:
            return rho_l, un_l, p_l
        a_sl = a_l * (p_s / p_l) ** ((GAMMA - 1) / (2 * GAMMA))
        st_l = u_s - a_sl                      # tail
        if st_l <= 0:
            rho_s = rho_l * (p_s / p_l) ** (1 / GAMMA)
            return rho_s, u_s, p_s
        # inside the fan
        c = 2 / (GAMMA + 1) * (a_l + (GAMMA - 1) / 2 * un_l)
        rho_f = rho_l * (c / a_l) ** (2 / (GAMMA - 1))
        p_f = p_l * (c / a_l) ** (2 * GAMMA / (GAMMA - 1))
        return rho_f, c, p_f

    # the interface is on the RIGHT of the contact
    if p_s > p_r:  # right shock
        s_r = un_r + a_r * sqrt((GAMMA + 1) / (2 * GAMMA) * p_s / p_r + (GAMMA - 1) / (2 * GAMMA))
        if s_r <= 0:
            return rho_r, un_r, p_r
        pr = p_s / p_r
        rho_s = rho_r * (pr + (GAMMA - 1) / (GAMMA + 1)) / ((GAMMA - 1) / (GAMMA + 1) * pr + 1)
        return rho_s, u_s, p_s
    # right rarefaction
    sh_r = un_r + a_r
    if sh_r <= 0:
        return rho_r, un_r, p_r
    a_sr = a_r * (p_s / p_r) ** ((GAMMA - 1) / (2 * GAMMA))
    st_r = u_s + a_sr
    if st_r >= 0:
        rho_s = rho_r * (p_s / p_r) ** (1 / GAMMA)
        return rho_s, u_s, p_s
    c = 2 / (GAMMA + 1) * (-a_r + (GAMMA - 1) / 2 * un_r)
    rho_f = rho_r * (-c / a_r) ** (2 / (GAMMA - 1))
    p_f = p_r * (-c / a_r) ** (2 * GAMMA / (GAMMA - 1))
    return rho_f, c, p_f


# ----------------------------------------------------------------------------------
# (B) THE HLLC ALGORITHM, at 60 digits
# ----------------------------------------------------------------------------------


def davis_speeds(rho_l, un_l, p_l, a_l, rho_r, un_r, p_r, a_r):
    """Davis (1988) direct bounds, Toro 3rd ed. eq. 10.48.

    MEASURED NOT TO BOUND the exact rightmost wave for a strong right-moving shock: for Sod this
    gives S_R = 1.1831 while the true shock speed is 1.7521. Retained ONLY as the non-vacuity
    control for the signal-bounding criterion -- it is not the production estimate.
    """
    return min(un_l - a_l, un_r - a_r), max(un_l + a_l, un_r + a_r)


def toro_pvrs_speeds(rho_l, un_l, p_l, a_l, rho_r, un_r, p_r, a_r):
    """Toro 3rd ed. eqs. 10.59-10.61: a pressure-based estimate that accounts for shock strength.

    p* is first estimated by the Primitive Variable Riemann Solver (eq. 9.28), then each side's
    acoustic speed is multiplied by a shock factor q_K >= 1:

        p_pvrs = 0.5(p_L + p_R) - 0.125 (u_nR - u_nL)(rho_L + rho_R)(a_L + a_R)
        p*     = max(0, p_pvrs)
        q_K    = 1                                              if p* <= p_K   (rarefaction)
               = sqrt(1 + (gamma+1)/(2 gamma) (p*/p_K - 1))      if p* >  p_K   (shock)
        S_L    = u_nL - a_L q_L          S_R = u_nR + a_R q_R

    WHY THIS AND NOT DAVIS: q_K >= 1 always, so (S_L - u_nL) = -a_L q_L <= -a_L and
    (S_R - u_nR) = +a_R q_R >= +a_R. The contact denominator bound therefore SURVIVES unchanged
    (and is only ever looser), while the estimate widens to enclose a shock instead of under-
    estimating it. Nothing is given up and the signal-bounding property is gained.
    """
    p_pvrs = (p_l + p_r) / 2 - (un_r - un_l) * (rho_l + rho_r) * (a_l + a_r) / 8
    p_star = p_pvrs if p_pvrs > 0 else mpf(0)

    def q(p_k):
        if p_star <= p_k:
            return mpf(1)
        return sqrt(1 + (GAMMA + 1) / (2 * GAMMA) * (p_star / p_k - 1))

    return un_l - a_l * q(p_l), un_r + a_r * q(p_r)


def hllc(rho_l, vel_l, p_l, rho_r, vel_r, p_r, n, estimator="toro_pvrs"):
    """Returns a dict with the wave speeds, star states, branch and flux."""
    a_l, a_r = sound_speed(rho_l, p_l), sound_speed(rho_r, p_r)
    un_l = vel_l[0] * n[0] + vel_l[1] * n[1] + vel_l[2] * n[2]
    un_r = vel_r[0] * n[0] + vel_r[1] * n[1] + vel_r[2] * n[2]

    speeds = {"davis": davis_speeds, "toro_pvrs": toro_pvrs_speeds}[estimator]
    s_l, s_r = speeds(rho_l, un_l, p_l, a_l, rho_r, un_r, p_r, a_r)

    ql = rho_l * (s_l - un_l)
    qr = rho_r * (s_r - un_r)
    denom = ql - qr
    s_star = (p_r - p_l + ql * un_l - qr * un_r) / denom

    q_l, q_r = conserved(rho_l, vel_l, p_l), conserved(rho_r, vel_r, p_r)
    f_l = euler_flux(rho_l, vel_l, p_l, n)
    f_r = euler_flux(rho_r, vel_r, p_r, n)

    def star(rho_k, vel_k, p_k, un_k, s_k, q_k, f_k):
        rho_s = rho_k * (s_k - un_k) / (s_k - s_star)
        vel_s = [vel_k[i] + (s_star - un_k) * n[i] for i in range(3)]
        rhoe_k = q_k[4]
        # THE PRESSURE TERM'S SIGN IS A PLUS. It follows from the energy Rankine-Hugoniot condition
        # across S_K, and the defining property it enforces is F*_K,energy == S*(rhoE*_K + p*).
        # THIS WAS A MINUS WHEN THIS FILE WAS FROZEN, AND IT WAS WRONG -- the same error as the C++,
        # because both were written from the same misremembered formula, which is exactly why
        # comparing the two (criterion X2) could not catch it. See
        # results/p17/hllc/AMENDMENT_star_energy_sign.md.
        rhoe_s = rho_s * (rhoe_k / rho_k
                          + (s_star - un_k) * (s_star + p_k / (rho_k * (s_k - un_k))))
        q_s = [rho_s, rho_s * vel_s[0], rho_s * vel_s[1], rho_s * vel_s[2], rhoe_s]
        f_s = [f_k[i] + s_k * (q_s[i] - q_k[i]) for i in range(5)]
        return rho_s, q_s, f_s

    rho_sl, q_sl, f_sl = star(rho_l, vel_l, p_l, un_l, s_l, q_l, f_l)
    rho_sr, q_sr, f_sr = star(rho_r, vel_r, p_r, un_r, s_r, q_r, f_r)

    if s_l >= 0:
        branch, flux = "F_L", f_l
    elif s_star >= 0:
        branch, flux = "F_star_L", f_sl
    elif s_r >= 0:
        branch, flux = "F_star_R", f_sr
    else:
        branch, flux = "F_R", f_r

    # THE HLL FLUX -- HLLC with the contact wave DELETED. This is the reference for the non-vacuity
    # control the frozen G6 criterion names: "a deliberately dropped contact-wave term must break (f)
    # while still passing (a)". HLL uses one averaged star state instead of two, so it still reduces
    # to F(Q) for a uniform state (a) but SMEARS a contact, which is the whole reason HLLC exists.
    if s_l >= 0:
        f_hll = f_l
    elif s_r <= 0:
        f_hll = f_r
    else:
        f_hll = [(s_r * f_l[i] - s_l * f_r[i] + s_l * s_r * (q_r[i] - q_l[i])) / (s_r - s_l)
                 for i in range(5)]

    return {
        "sL": s_l, "sStar": s_star, "sR": s_r,
        "fluxHLL": f_hll,
        "aL": a_l, "aR": a_r, "unL": un_l, "unR": un_r,
        "denom": denom,
        # The denominator is provably bounded away from zero for admissible states with these wave
        # speeds. Davis gives s_l <= un_l - a_l and s_r >= un_r + a_r, so
        #     (s_l - un_l) <= -a_l < 0        and      (s_r - un_r) >= +a_r > 0
        # hence   denom = rho_l(s_l-un_l) - rho_r(s_r-un_r) <= -(rho_l a_l + rho_r a_r) < 0.
        #
        # THE BOUND IS TIGHT AND IS ATTAINED: when un_l == un_r and a_l == a_r, Davis's min and max
        # pick each side's own value, so (s_l-un_l) = -a_l and (s_r-un_r) = +a_r EXACTLY and the
        # inequality becomes an equality. A first version of this script tested the bound with a
        # strict `>=` on 60-digit values and reported False for the two supersonic cases -- not
        # because the bound fails but because it is attained there and the last digits of two
        # different summation orders disagree. The RATIO is reported instead of a boolean: it must
        # be >= 1, and it equals 1 exactly in the degenerate-equality case.
        "denomBound": -(rho_l * a_l + rho_r * a_r),
        "denomRatio": fabs(denom) / (rho_l * a_l + rho_r * a_r),
        "rhoStarL": rho_sl, "rhoStarR": rho_sr,
        "branch": branch, "flux": flux,
        "fluxL": f_l, "fluxR": f_r, "fluxStarL": f_sl, "fluxStarR": f_sr,
    }


# ----------------------------------------------------------------------------------
# The reference cases
# ----------------------------------------------------------------------------------

NX = [mpf(1), mpf(0), mpf(0)]
NY = [mpf(0), mpf(1), mpf(0)]
NZ = [mpf(0), mpf(0), mpf(1)]


def V(a, b=0, c=0):
    return [mpf(a), mpf(b), mpf(c)]


# Sod's state is dimensionless in the literature. The project's gas is air with p ~ 1e5, so Sod is
# carried BOTH ways: the literature values (for comparison with published results) and a
# pressure-scaled variant at atmospheric magnitude (to exercise the real operating range).
SOD_L = (mpf("1.0"), V(0), mpf("1.0"))
SOD_R = (mpf("0.125"), V(0), mpf("0.1"))
P_ATM = mpf("101325")
SOD_ATM_L = (mpf("1.0"), V(0), P_ATM)
SOD_ATM_R = (mpf("0.125"), V(0), P_ATM / 10)

CASES = [
    # name,                          left,                          right,                  normal
    ("uniform_x", (mpf("1.2"), V("30", "-12", "7"), P_ATM), (mpf("1.2"), V("30", "-12", "7"), P_ATM), NX),
    ("uniform_oblique", (mpf("1.2"), V("30", "-12", "7"), P_ATM), (mpf("1.2"), V("30", "-12", "7"), P_ATM), None),
    ("sod_literature", SOD_L, SOD_R, NX),
    ("sod_atmospheric", SOD_ATM_L, SOD_ATM_R, NX),
    ("sod_y", (SOD_L[0], V(0), SOD_L[2]), (SOD_R[0], V(0), SOD_R[2]), NY),
    ("sod_z", (SOD_L[0], V(0), SOD_L[2]), (SOD_R[0], V(0), SOD_R[2]), NZ),
    ("stationary_contact", (mpf("1.0"), V(0), P_ATM), (mpf("0.125"), V(0), P_ATM), NX),
    ("moving_contact", (mpf("1.0"), V("50"), P_ATM), (mpf("0.125"), V("50"), P_ATM), NX),
    ("tangential_shear", (mpf("1.0"), V("0", "20", "-5"), P_ATM), (mpf("1.0"), V("0", "-35", "11"), P_ATM), NX),
    ("supersonic_right", (mpf("1.0"), V("900"), P_ATM), (mpf("0.5"), V("900"), P_ATM / 2), NX),
    ("supersonic_left", (mpf("1.0"), V("-900"), P_ATM), (mpf("0.5"), V("-900"), P_ATM / 2), NX),
    ("strong_pressure_ratio", (mpf("10.0"), V(0), P_ATM * 1000), (mpf("0.01"), V(0), P_ATM / 100), NX),
    ("strong_density_ratio", (mpf("1000.0"), V(0), P_ATM), (mpf("0.001"), V(0), P_ATM), NX),
    ("near_sonic_left", None, None, NX),   # filled below
    ("low_pressure", (mpf("1e-4"), V("1"), mpf("1e-2")), (mpf("1e-5"), V("1"), mpf("1e-3")), NX),
]


def _near_sonic():
    """A state where u_n - a is deliberately placed within ~1e-9 of zero, to probe S_L ~ 0."""
    rho = mpf("1.0")
    p = P_ATM
    a = sound_speed(rho, p)
    return (rho, V(a + mpf("1e-9")), p), (rho, V(a + mpf("1e-9")), p)


def oblique_normal():
    """A normalized, deliberately non-axis-aligned normal. Normalized at 60 digits."""
    v = [mpf(2), mpf(-3), mpf(6)]          # |v| = 7 exactly, so the normal is exact in rationals
    m = sqrt(v[0] ** 2 + v[1] ** 2 + v[2] ** 2)
    return [c / m for c in v]


def build_cases():
    out = []
    ob = oblique_normal()
    for name, left, right, n in CASES:
        if name == "near_sonic_left":
            left, right = _near_sonic()
        if n is None:
            n = ob
        out.append((name, left, right, n))
    return out


def fmt(x):
    return mp.nstr(x, 22, strip_zeros=False)


def flux_scales(rho_l, p_l, a_l, rho_r, p_r, a_r):
    """A CHARACTERISTIC magnitude for each flux component, from the states alone.

    WHY THIS EXISTS. A first version of this script normalised each component by
    max(|F_hllc|, |F_exact|). Where the true flux is exactly zero -- every stationary contact, and
    every transverse component of an axis-aligned case -- that divides by whatever tiny value the
    other side happens to hold, so 60-digit round-off of 9.5e-58 was reported as a RELATIVE ERROR OF
    1.0, indistinguishable from a genuinely spurious flux. It reported strong_density_ratio as 100%
    wrong when the flux was zero to 60 digits, and it reported the HLL contact defect as 1.0 as well
    -- the same number for a real 466 kg/(m^2 s) spurious flux and for nothing at all.

    Normalising by a scale built from the STATES cannot collapse, so a zero reference stays readable:
        mass      rho_m a_m
        momentum  rho_m a_m^2 + p_m
        energy    rho_m a_m^3 + p_m a_m
    """
    rho_m = max(rho_l, rho_r)
    a_m = max(a_l, a_r)
    p_m = max(p_l, p_r)
    mom = rho_m * a_m ** 2 + p_m
    return [rho_m * a_m, mom, mom, mom, rho_m * a_m ** 3 + p_m * a_m]


def rel_to_scale(a, b, scale):
    """|a - b| / scale, with the scale guaranteed positive by construction."""
    return fabs(a - b) / scale


def main():
    as_json = "--json" in sys.argv
    cases = build_cases()
    payload = {
        "gas": {"R": fmt(R_GAS), "cp": fmt(CP), "cv": fmt(CV), "gamma": fmt(GAMMA)},
        "dps": mp.dps,
        "cases": [],
    }

    if not as_json:
        print("P17.7 -- INDEPENDENT reference, mpmath at %d decimal digits" % mp.dps)
        print("NO CFDApp code is used. Formulas from Toro 3rd ed. as cited in the module docstring.")
        print("gas: R = %s  cp = %s  cv = %s  gamma = %s" % (fmt(R_GAS), fmt(CP), fmt(CV), fmt(GAMMA)))
        print("")

    for name, (rho_l, vel_l, p_l), (rho_r, vel_r, p_r), n in cases:
        h = hllc(rho_l, vel_l, p_l, rho_r, vel_r, p_r, n)

        # The exact solution is a 1D construct along the normal. It is meaningful only when the
        # tangential velocities match, because a tangential jump is a contact the 1D exact solver
        # does not model. Reported as not-applicable otherwise, rather than silently wrong.
        ut_l = [vel_l[i] - h["unL"] * n[i] for i in range(3)]
        ut_r = [vel_r[i] - h["unR"] * n[i] for i in range(3)]
        tangential_match = all(fabs(ut_l[i] - ut_r[i]) < mpf("1e-50") for i in range(3))

        entry = {
            "name": name,
            "gamma": fmt(GAMMA),
            "left": {"rho": fmt(rho_l), "u": [fmt(c) for c in vel_l], "p": fmt(p_l),
                     "rhoE": fmt(total_energy_density(rho_l, vel_l, p_l)),
                     "a": fmt(sound_speed(rho_l, p_l))},
            "right": {"rho": fmt(rho_r), "u": [fmt(c) for c in vel_r], "p": fmt(p_r),
                      "rhoE": fmt(total_energy_density(rho_r, vel_r, p_r)),
                      "a": fmt(sound_speed(rho_r, p_r))},
            "normal": [fmt(c) for c in n],
            "hllc": {
                "sL": fmt(h["sL"]), "sStar": fmt(h["sStar"]), "sR": fmt(h["sR"]),
                "denom": fmt(h["denom"]), "denomBound": fmt(h["denomBound"]),
                "rhoStarL": fmt(h["rhoStarL"]), "rhoStarR": fmt(h["rhoStarR"]),
                "denomRatio": fmt(h["denomRatio"]),
                "branch": h["branch"],
                "flux": [fmt(c) for c in h["flux"]],
            },
            # the control reference: the same case with the contact wave deleted
            "hll": {"flux": [fmt(c) for c in h["fluxHLL"]]},
        }

        if tangential_match:
            rho0, un0, p00 = exact_sample_at_zero(rho_l, h["unL"], p_l, rho_r, h["unR"], p_r)
            # rebuild the full velocity at the interface: normal from the exact solution,
            # tangential carried over (it is continuous across the 1D wave structure here)
            vel0 = [ut_l[i] + un0 * n[i] for i in range(3)]
            fx = euler_flux(rho0, vel0, p00, n)
            p_star, u_star = exact_star(rho_l, h["unL"], p_l, rho_r, h["unR"], p_r)
            wl, wr = exact_wave_extremes(rho_l, h["unL"], p_l, rho_r, h["unR"], p_r)
            entry["exact"] = {
                "pStar": fmt(p_star), "uStar": fmt(u_star),
                "leftmostWave": fmt(wl), "rightmostWave": fmt(wr),
                "sL_bounds_left": bool(h["sL"] <= wl),
                "sR_bounds_right": bool(h["sR"] >= wr),
                "sStar_vs_uStar_rel": fmt(fabs(h["sStar"] - u_star)
                                          / max(fabs(u_star), mpf("1e-300"))),
                "rhoAtZero": fmt(rho0), "unAtZero": fmt(un0), "pAtZero": fmt(p00),
                "flux": [fmt(c) for c in fx],
            }
            sc = flux_scales(rho_l, p_l, h["aL"], rho_r, p_r, h["aR"])
            entry["fluxScales"] = [fmt(c) for c in sc]
            entry["hllc_vs_exact_rel"] = [fmt(rel_to_scale(h["flux"][i], fx[i], sc[i]))
                                          for i in range(5)]
            entry["hllc_vs_exact_abs"] = [fmt(fabs(h["flux"][i] - fx[i])) for i in range(5)]
        else:
            entry["exact"] = None
            entry["exact_na_reason"] = "tangential velocity differs; the 1D exact solver does not model it"

        payload["cases"].append(entry)

        if not as_json:
            print("=== %s ===" % name)
            print("  n          = (%s, %s, %s)" % tuple(fmt(c) for c in n))
            print("  L  rho=%s  u_n=%s  p=%s  a=%s" % (fmt(rho_l), fmt(h["unL"]), fmt(p_l), fmt(h["aL"])))
            print("  R  rho=%s  u_n=%s  p=%s  a=%s" % (fmt(rho_r), fmt(h["unR"]), fmt(p_r), fmt(h["aR"])))
            print("  S_L        = %s" % fmt(h["sL"]))
            print("  S_star     = %s" % fmt(h["sStar"]))
            print("  S_R        = %s" % fmt(h["sR"]))
            print("  ordering   S_L < S* < S_R : %s" % (h["sL"] < h["sStar"] < h["sR"]))
            print("  denom      = %s" % fmt(h["denom"]))
            print("  |denom| / (rho_L a_L + rho_R a_R) = %s   (must be >= 1; == 1 when the bound"
                  " is attained)" % fmt(h["denomRatio"]))
            print("  rho*_L     = %s   > 0 : %s" % (fmt(h["rhoStarL"]), h["rhoStarL"] > 0))
            print("  rho*_R     = %s   > 0 : %s" % (fmt(h["rhoStarR"]), h["rhoStarR"] > 0))
            print("  branch     = %s" % h["branch"])
            print("  F_HLLC     = [%s]" % ", ".join(fmt(c) for c in h["flux"]))
            if entry["exact"]:
                print("  EXACT p*   = %s" % entry["exact"]["pStar"])
                print("  EXACT u*   = %s" % entry["exact"]["uStar"])
                print("  EXACT leftmost wave  = %s   S_L bounds it : %s"
                      % (entry["exact"]["leftmostWave"], entry["exact"]["sL_bounds_left"]))
                print("  EXACT rightmost wave = %s   S_R bounds it : %s"
                      % (entry["exact"]["rightmostWave"], entry["exact"]["sR_bounds_right"]))
                print("  S* vs exact u*, relative = %s" % entry["exact"]["sStar_vs_uStar_rel"])
                print("  EXACT F    = [%s]" % ", ".join(entry["exact"]["flux"]))
                print("  flux scales= [%s]" % ", ".join(entry["fluxScales"]))
                print("  HLLC vs EXACT, |diff| / scale per component:")
                print("             [%s]" % ", ".join(entry["hllc_vs_exact_rel"]))
                # THE NON-VACUITY EVIDENCE the frozen G6 criterion asks for: the same case with the
                # contact wave deleted (i.e. HLL). Where HLLC is exact, HLL must NOT be.
                hll_rel = [fmt(rel_to_scale(h["fluxHLL"][i], fx[i], sc[i])) for i in range(5)]
                entry["hll_vs_exact_rel"] = hll_rel
                entry["hll_vs_exact_abs"] = [fmt(fabs(h["fluxHLL"][i] - fx[i])) for i in range(5)]
                print("  HLL (contact wave DELETED) vs EXACT, |diff| / scale:")
                print("             [%s]" % ", ".join(hll_rel))
                print("  HLL vs EXACT, ABSOLUTE difference (so a zero reference stays readable):")
                print("             [%s]" % ", ".join(entry["hll_vs_exact_abs"]))
            else:
                print("  EXACT      n/a -- %s" % entry["exact_na_reason"])
            print("")

    # ------------------------------------------------------------------------------------------
    # THE ESTIMATOR COMPARISON. This is what selects the production formulation, and it is decided
    # by measurement against the exact solver rather than by citation.
    # ------------------------------------------------------------------------------------------
    if not as_json:
        print("=" * 100)
        print("SIGNAL-BOUNDING PROPERTY: does the estimator enclose the TRUE wave speeds?")
        print("An estimator that does not lets signal leave the control volume. This is the")
        print("correctness requirement on the estimate -- not accuracy -- so it is gateable.")
        print("=" * 100)
        print("%-24s %-26s %-26s" % ("case", "Davis (10.48)", "Toro PVRS (10.59)"))
        print("%-24s %-13s %-12s %-13s %-12s" % ("", "S_L<=left", "S_R>=right", "S_L<=left", "S_R>=right"))
        fails = {"davis": 0, "toro_pvrs": 0}
        total = 0
        for name, (rho_l, vel_l, p_l), (rho_r, vel_r, p_r), n in cases:
            hd = hllc(rho_l, vel_l, p_l, rho_r, vel_r, p_r, n, "davis")
            ht = hllc(rho_l, vel_l, p_l, rho_r, vel_r, p_r, n, "toro_pvrs")
            ut_l = [vel_l[i] - hd["unL"] * n[i] for i in range(3)]
            ut_r = [vel_r[i] - hd["unR"] * n[i] for i in range(3)]
            if not all(fabs(ut_l[i] - ut_r[i]) < mpf("1e-50") for i in range(3)):
                print("%-24s  (exact solution n/a -- tangential jump)" % name)
                continue
            wl, wr = exact_wave_extremes(rho_l, hd["unL"], p_l, rho_r, hd["unR"], p_r)
            total += 1
            row = [name]
            for key, h in (("davis", hd), ("toro_pvrs", ht)):
                okl, okr = h["sL"] <= wl, h["sR"] >= wr
                if not (okl and okr):
                    fails[key] += 1
                row += ["OK" if okl else "*** FAILS", "OK" if okr else "*** FAILS"]
            print("%-24s %-13s %-12s %-13s %-12s" % tuple(row))
        print("")
        print("cases where the estimator does NOT bound the true signal speeds:")
        print("  Davis (10.48)     : %d of %d" % (fails["davis"], total))
        print("  Toro PVRS (10.59) : %d of %d" % (fails["toro_pvrs"], total))
        print("")
        print("The denominator bound is unaffected by the choice: q_K >= 1 gives")
        print("(S_L - u_nL) = -a_L q_L <= -a_L and (S_R - u_nR) = +a_R q_R >= +a_R, so")
        print("denom <= -(rho_L a_L + rho_R a_R) holds for BOTH estimators.")

    if as_json:
        print(json.dumps(payload, indent=1))


if __name__ == "__main__":
    main()
