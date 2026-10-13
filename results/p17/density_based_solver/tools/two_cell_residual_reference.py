"""P17.8 criterion R2 -- an INDEPENDENT two-cell finite-volume residual.

No CFDApp code is imported. Every formula below is written from the published relations, and the
star state is written from the Rankine-Hugoniot derivation in
results/p17/hllc/AMENDMENT_star_energy_sign.md -- NOT copied from HllcFlux.hpp.

THE LESSON FROM THAT AMENDMENT IS APPLIED HERE. P17.7's reference agreed with the C++ to 3.25e-16
while both were wrong, because both were written from the same remembered formula. So this script
does not merely compute a number: it ASSERTS THE DEFINING PROPERTY that neither artefact can satisfy
by accident,

    F*_K,energy == S* (rhoE*_K + p*)        with  p* = p_K + rho_K (S_K - u_nK)(S* - u_nK)

and refuses to emit a reference value if that property does not hold. A sign error cannot survive it.

Geometry, from MeshGeometry::createRectilinear2D with nx=2, ny=1, Lx=2.0, Ly=1.0:
    two cells, each 1 x 1 with unit depth -- volume 1, every face area 1
    cell 0 faces: left wall n=(-1,0,0) | internal n=(+1,0,0) | bottom n=(0,-1,0) | top n=(0,+1,0)
    cell 1 faces: internal (as NEIGHBOUR, so -F) | right wall n=(+1,0,0) | bottom | top
"""

from mpmath import mp, mpf, sqrt, fabs

mp.dps = 50

R = mpf("287.05")
CP = mpf("1005")
CV = CP - R
GAMMA = CP / CV

# ---------------------------------------------------------------------------------------------
# primitive state -- the zero-reference convention the tests use: h = cp T, e = cv T
# ---------------------------------------------------------------------------------------------


def state(rho, vel, T):
    rho, T = mpf(rho), mpf(T)
    u = [mpf(c) for c in vel]
    p = rho * R * T
    a = sqrt(GAMMA * p / rho)
    ke = mpf("0.5") * rho * (u[0] ** 2 + u[1] ** 2 + u[2] ** 2)
    rhoE = p / (GAMMA - 1) + ke
    return {"rho": rho, "u": u, "p": p, "a": a, "rhoE": rhoE, "T": T}


def dot(a, b):
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]


def euler_flux(s, n):
    """The physical flux F(Q).n, per unit area."""
    un = dot(s["u"], n)
    return {
        "mass": s["rho"] * un,
        "mom": [s["rho"] * un * s["u"][i] + s["p"] * n[i] for i in range(3)],
        "energy": un * (s["rhoE"] + s["p"]),
    }


def mirror(s, n):
    """The inviscid-wall exterior state: reflect the normal velocity, keep rho, p, T."""
    un = dot(s["u"], n)
    u = [s["u"][i] - 2 * un * n[i] for i in range(3)]
    ke_in = mpf("0.5") * s["rho"] * dot(s["u"], s["u"])
    ke_out = mpf("0.5") * s["rho"] * dot(u, u)
    out = dict(s)
    out["u"] = u
    out["rhoE"] = s["rhoE"] - ke_in + ke_out
    return out


# ---------------------------------------------------------------------------------------------
# HLLC -- Toro 10.59-10.61 wave speeds, 10.37 contact, 10.39 star state
# ---------------------------------------------------------------------------------------------


def shock_factor(p_star, p_k):
    if p_star <= p_k:
        return mpf(1)
    return sqrt(1 + (GAMMA + 1) / (2 * GAMMA) * (p_star / p_k - 1))


def hllc(l, r, n, check):
    unL, unR = dot(l["u"], n), dot(r["u"], n)
    p_pvrs = (l["p"] + r["p"]) / 2 - mpf("0.125") * (unR - unL) * (l["rho"] + r["rho"]) * (
        l["a"] + r["a"]
    )
    p_star_est = p_pvrs if p_pvrs > 0 else mpf(0)
    sL = unL - l["a"] * shock_factor(p_star_est, l["p"])
    sR = unR + r["a"] * shock_factor(p_star_est, r["p"])

    qL = l["rho"] * (sL - unL)
    qR = r["rho"] * (sR - unR)
    s_star = (r["p"] - l["p"] + qL * unL - qR * unR) / (qL - qR)

    fL, fR = euler_flux(l, n), euler_flux(r, n)
    if sL >= 0:
        return fL, "LeftSupersonic"
    if sR <= 0:
        return fR, "RightSupersonic"

    def star(s, un, sK, q):
        rho_star = s["rho"] * (sK - un) / (sK - s_star)
        # THE PLUS SIGN, derived from Rankine-Hugoniot -- see the amendment.
        e_star = s["rhoE"] / s["rho"] + (s_star - un) * (s_star + s["p"] / q)
        u_star = [s["u"][i] + (s_star - un) * n[i] for i in range(3)]
        p_star = s["p"] + s["rho"] * (sK - un) * (s_star - un)
        return rho_star, e_star, u_star, p_star

    if s_star >= 0:
        s, un, sK, q, f = l, unL, sL, qL, fL
        branch = "LeftStar"
    else:
        s, un, sK, q, f = r, unR, sR, qR, fR
        branch = "RightStar"

    rho_star, e_star, u_star, p_star = star(s, un, sK, q)
    flux = {
        "mass": f["mass"] + sK * (rho_star - s["rho"]),
        "mom": [
            f["mom"][i] + sK * (rho_star * u_star[i] - s["rho"] * s["u"][i]) for i in range(3)
        ],
        "energy": f["energy"] + sK * (rho_star * e_star - s["rhoE"]),
    }

    # THE DEFINING PROPERTY. A sign error in e_star breaks this and nothing else needs to notice.
    lhs = flux["energy"]
    rhs = s_star * (rho_star * e_star + p_star)
    scale = fabs(rhs) + fabs(f["energy"]) + mpf("1e-300")
    if fabs(lhs - rhs) / scale > mpf("1e-40"):
        raise AssertionError(
            "star energy flux violates F*_energy == S*(rhoE* + p*): "
            f"{mp.nstr(lhs, 20)} vs {mp.nstr(rhs, 20)}"
        )
    check.append((branch, sL, s_star, sR))
    return flux, branch


# ---------------------------------------------------------------------------------------------
# the two-cell residual
# ---------------------------------------------------------------------------------------------

CELL0 = state("1.4", ("80", "0", "0"), "320")
CELL1 = state("0.9", ("-30", "0", "0"), "280")
AREA = mpf(1)

WALLS = {
    "left": [mpf(-1), mpf(0), mpf(0)],
    "right": [mpf(1), mpf(0), mpf(0)],
    "bottom": [mpf(0), mpf(-1), mpf(0)],
    "top": [mpf(0), mpf(1), mpf(0)],
}


def zero():
    # `scale` accumulates the sum of ABSOLUTE contributions per component. A residual is a sum of
    # ~4 face terms that largely cancel, so the round-off floor of the sum is set by the size of the
    # TERMS, not by the size of the result. That sum is the derived scale R2's 8 eps bound multiplies
    # -- using |R| itself would demand a relative accuracy the cancellation makes impossible.
    return {"mass": mpf(0), "mom": [mpf(0)] * 3, "energy": mpf(0), "scale": [mpf(0)] * 5}


def add(acc, f, sign):
    acc["mass"] += sign * f["mass"] * AREA
    for i in range(3):
        acc["mom"][i] += sign * f["mom"][i] * AREA
    acc["energy"] += sign * f["energy"] * AREA
    acc["scale"][0] += fabs(f["mass"] * AREA)
    for i in range(3):
        acc["scale"][1 + i] += fabs(f["mom"][i] * AREA)
    acc["scale"][4] += fabs(f["energy"] * AREA)
    return acc


checks = []
n_int = [mpf(1), mpf(0), mpf(0)]
f_internal, branch_internal = hllc(CELL0, CELL1, n_int, checks)

R0, R1 = zero(), zero()
add(R0, f_internal, 1)  # owner: +F
add(R1, f_internal, -1)  # neighbour: -F, the SAME value negated

for name, n in (("left", WALLS["left"]), ("bottom", WALLS["bottom"]), ("top", WALLS["top"])):
    f, _ = hllc(CELL0, mirror(CELL0, n), n, checks)
    add(R0, f, 1)
for name, n in (("right", WALLS["right"]), ("bottom", WALLS["bottom"]), ("top", WALLS["top"])):
    f, _ = hllc(CELL1, mirror(CELL1, n), n, checks)
    add(R1, f, 1)

print("P17.8 R2 -- independent two-cell residual (mpmath, 50 digits, no CFDApp code)")
print(f"gamma = {mp.nstr(GAMMA, 20)}")
print(f"internal face branch: {branch_internal}")
for b, sL, ss, sR in checks:
    print(f"  branch {b:<16} S_L {mp.nstr(sL, 12):>16}  S* {mp.nstr(ss, 12):>16}  S_R {mp.nstr(sR, 12):>16}")
print("the defining property F*_energy == S*(rhoE* + p*) held for every star branch above")
print()
for tag, Rc in (("cell 0", R0), ("cell 1", R1)):
    print(f"{tag}")
    print(f"  R_rho   {mp.nstr(Rc['mass'], 17)}")
    print(f"  R_rhoU  {mp.nstr(Rc['mom'][0], 17)}")
    print(f"  R_rhoV  {mp.nstr(Rc['mom'][1], 17)}")
    print(f"  R_rhoW  {mp.nstr(Rc['mom'][2], 17)}")
    print(f"  R_rhoE  {mp.nstr(Rc['energy'], 17)}")
print()
print("C++ literals (double), for the test:")
for tag, Rc in (("0", R0), ("1", R1)):
    vals = [Rc["mass"], Rc["mom"][0], Rc["mom"][1], Rc["mom"][2], Rc["energy"]]
    joined = ", ".join(mp.nstr(v, 17) for v in vals)
    print(f"  // cell {tag}: rho, rhoU, rhoV, rhoW, rhoE")
    print(f"  {{{joined}}},")
print("accumulation scale sum_f |F_f| A, per component -- what R2's 8 eps bound multiplies:")
for tag, Rc in (("0", R0), ("1", R1)):
    joined = ", ".join(mp.nstr(v, 17) for v in Rc["scale"])
    print(f"  // cell {tag} scale: rho, rhoU, rhoV, rhoW, rhoE")
    print(f"  {{{joined}}},")
print()
EPS = mpf("2.220446049250313e-16")
for tag, Rc in (("0", R0), ("1", R1)):
    b = [8 * EPS * v for v in Rc["scale"]]
    print(f"  cell {tag} 8 eps bounds: " + ", ".join(mp.nstr(v, 6) for v in b))
