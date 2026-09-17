#!/usr/bin/env python3
"""P12-DIFF-002-UC-001 — independent discrete-exact reference for the fully developed channel.

This file contains NO CFDApp code, links nothing, and imports nothing from the project. It builds
the finite-volume equations from the stencil coefficients derived below and solves them in EXACT
RATIONAL ARITHMETIC (fractions.Fraction), so every number it reports is a closed-form rational, not
a floating-point echo of production output.

=====================================================================================
THE PHYSICAL PROBLEM
=====================================================================================
Steady, fully developed, incompressible planar Poiseuille flow between walls at y = 0 and y = H,
driven by a constant streamwise gradient dp/dx, with a PRESCRIBED mean velocity U (the test's inlet
condition is a uniform U, so the flow rate is fixed and dp/dx is the unknown):

    0 = -dp/dx + mu d^2u/dy^2 ,    u(0) = u(H) = 0 ,    (1/H) int_0^H u dy = U

Continuous solution, with G := (dp/dx)/mu:

    u(y) = -(G/2) y (H - y) ,   u_max = -(G/8) H^2 ,   mean = -(G/12) H^2
    => G_exact = -12 U / H^2 ,  dp/dx_exact = -12 mu U / H^2 ,  u_max = (3/2) U

=====================================================================================
THE DISCRETE EQUATIONS
=====================================================================================
ny uniform rows of height h = H/ny; cell centres y_j = (j + 1/2) h, j = 0..ny-1. Per unit depth and
unit streamwise length a cell has volume V = h and each y-face has area A = 1. Integrating the
momentum equation over cell j and writing F_in for the diffusive flux INTO the cell:

    sum_faces F_in = (dp/dx) V

INTERNAL face between j and j+1 (both operators, central diffusion):

    F_in(cell j) = mu A (u_{j+1} - u_j) / h

WALL face of cell 0 (y = 0, u_b = 0) — the two operators differ ONLY here.

(a) HISTORICAL two-point wall flux, the one-sided difference over the half cell h1 = h/2:

        F_in = mu A (u_b - u_0) / h1 = -2 mu A u_0 / h

    Cell 0, after multiplying by h/(mu A):      -3 u_0 + u_1 = G h^2
    Interior 1 <= j <= ny-2:                     u_{j-1} - 2 u_j + u_{j+1} = G h^2
    Cell ny-1 (mirror of cell 0):                u_{ny-2} - 3 u_{ny-1} = G h^2

(b) P12-DIFF-002 three-point one-sided reconstruction. Fitting a quadratic through
    (0, u_b), (h1, u_0), (h2, u_1) with h1 = h/2, h2 = 3h/2 and differentiating at the wall:

        cP = mu A h2 / (h1 (h2 - h1)) = 3 mu A / h
        cF = mu A h1 / (h2 (h2 - h1)) = mu A / (3h)
        cB = mu A (1/h1 + 1/h2)       = 8 mu A / (3h)        (and cP - cF = cB, checked below)

        F_in = -(cP u_0 - cF u_1) + cB u_b

    Cell 0, after multiplying by h/(mu A):      -4 u_0 + (4/3) u_1 = G h^2
    Interior and the mirrored top cell as above.

Both systems are closed with the flow-rate constraint  sum_j u_j h = U H, i.e. sum_j u_j = ny U,
giving ny + 1 equations for (u_0..u_{ny-1}, G).

=====================================================================================
CLOSED FORMS THIS SCRIPT DERIVES AND THEN VERIFIES BY EXACT SOLVE
=====================================================================================
Substituting the ansatz u_j = (G/2)(y_j - H/2)^2 + D (which satisfies every interior equation
identically, since the second difference of a quadratic is exact) into the wall equation gives

    (a) two-point : D = -(G/2)(H/2)^2 - (G/8) h^2      -> u_j = -(G/2) y_j (H - y_j) + (-G) h^2/8
    (b) DIFF-002  : D = -(G/2)(H/2)^2                  -> u_j = -(G/2) y_j (H - y_j)  EXACTLY the
                                                          continuous profile at the cell centres

Imposing sum_j u_j = ny U then fixes G, using sum_j (y_j - H/2)^2 = h^2 ny (ny^2 - 1)/12:

    (a) G_two_point / G_exact = ny^2 / (ny^2 + 2)
    (b) G_diff002   / G_exact = 2 ny^2 / (2 ny^2 + 1)

and the centreline value the test measures -- a linear interpolation to y = H/2, which for even ny
is the average of the two centre-adjacent cells at y = H/2 -+ h/2 -- is

    (a) u_c = (3/2) U * ny^2/(ny^2 + 2)                      [the h^2/8 offset cancels exactly]
    (b) u_c = (3/2) U * 2ny^2/(2ny^2 + 1) * (ny^2 - 1)/ny^2

(a) reproduces the historical constants: ny = 8 -> 3/2 * 64/66 = 16/11 = 1.454545..., and a dp/dx
relative error of 2/(ny^2+2) = 2/66 = 3.0303 %.
"""

from fractions import Fraction as F
import argparse
import math
import sys


# ----------------------------------------------------------------------------------------------
# Exact linear solve (Gauss-Jordan over the rationals). No numpy, no floating point.
# ----------------------------------------------------------------------------------------------
def solve_exact(matrix, rhs):
    n = len(rhs)
    a = [row[:] + [rhs[i]] for i, row in enumerate(matrix)]
    for col in range(n):
        pivot = next((r for r in range(col, n) if a[r][col] != 0), None)
        if pivot is None:
            raise ValueError(f"singular system at column {col}")
        a[col], a[pivot] = a[pivot], a[col]
        inv = F(1) / a[col][col]
        a[col] = [v * inv for v in a[col]]
        for r in range(n):
            if r != col and a[r][col] != 0:
                factor = a[r][col]
                a[r] = [v - factor * w for v, w in zip(a[r], a[col])]
    return [a[i][n] for i in range(n)]


# ----------------------------------------------------------------------------------------------
# Wall-face stencil coefficients, derived from h1/h2 rather than transcribed.
# ----------------------------------------------------------------------------------------------
def wall_terms(operator, h, mu, area=F(1)):
    """Returns (cP, cF, cB) for the wall face: F_in = -(cP u_0 - cF u_1) + cB u_b."""
    h1 = h / 2
    if operator == "two_point":
        c = mu * area / h1
        return c, F(0), c
    if operator == "diff002":
        h2 = 3 * h / 2
        cP = mu * area * h2 / (h1 * (h2 - h1))
        cF = mu * area * h1 / (h2 * (h2 - h1))
        cB = mu * area * (F(1) / h1 + F(1) / h2)
        assert cP - cF == cB, "the reconstruction identity cP - cF = cB must hold exactly"
        return cP, cF, cB
    # Deliberately corrupted controls for the non-vacuity check (step 7).
    if operator == "diff002_far_cell_x2":          # far-cell coefficient doubled
        cP, cF, cB = wall_terms("diff002", h, mu, area)
        return cP, 2 * cF, cB
    if operator == "diff002_far_cell_sign":        # far-cell coefficient sign flipped
        cP, cF, cB = wall_terms("diff002", h, mu, area)
        return cP, -cF, cB
    if operator == "diff002_no_far_cell":          # far-cell term dropped (cP kept)
        cP, cF, cB = wall_terms("diff002", h, mu, area)
        return cP, F(0), cB
    raise ValueError(f"unknown operator {operator}")


def build_and_solve(operator, ny, H=F(1), mu=F(1, 10), U=F(1)):
    """Builds the ny momentum equations + the flow-rate constraint and solves them exactly.

    Unknowns: u_0..u_{ny-1} and G = (dp/dx)/mu.  Returns (u list, G).
    """
    h = H / ny
    area = F(1)
    cP, cF, _cB = wall_terms(operator, h, mu, area)
    internal = mu * area / h                      # internal-face conductance
    n = ny + 1                                    # + G
    A = [[F(0)] * n for _ in range(n)]
    b = [F(0)] * n

    for j in range(ny):
        # sum_faces F_in - (dp/dx) V = 0, with (dp/dx) = mu G and V = h * area
        row = A[j]
        if j == 0:
            row[0] += -cP
            row[1] += cF
        else:
            row[j] += -internal
            row[j - 1] += internal
        if j == ny - 1:
            row[ny - 1] += -cP
            row[ny - 2] += cF
        else:
            row[j] += -internal
            row[j + 1] += internal
        row[ny] = -mu * h * area                  # the -(dp/dx) V term, dp/dx = mu G
        b[j] = F(0)

    # Flow rate: sum_j u_j h = U H  ->  sum_j u_j = ny U
    for j in range(ny):
        A[ny][j] = F(1)
    b[ny] = ny * U

    sol = solve_exact(A, b)
    return sol[:ny], sol[ny]


def centreline(u, ny, H=F(1)):
    """The test's measurement: linear interpolation of the cell-centre profile to y = H/2."""
    h = H / ny
    ys = [(F(2 * j + 1, 2)) * h for j in range(ny)]
    target = H / 2
    if ny % 2 == 0:
        return (u[ny // 2 - 1] + u[ny // 2]) / 2   # y = H/2 sits exactly between two centres
    for j in range(ny - 1):
        if ys[j] <= target <= ys[j + 1]:
            t = (target - ys[j]) / (ys[j + 1] - ys[j])
            return u[j] + t * (u[j + 1] - u[j])
    raise ValueError("centre not bracketed")


def continuous(y, H=F(1), U=F(1)):
    return 6 * U * (y / H) * (1 - y / H)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--json", default="")
    args = ap.parse_args()

    H, mu, U = F(1), F(1, 10), F(1)
    G_exact = -12 * U / (H * H)
    dpdx_exact = mu * G_exact

    print(__doc__)
    print("=" * 100)
    print(f"H = {H}, mu = {mu}, U = {U}  (Re = rho U H / mu = 10 with rho = 1)")
    print(f"continuous: G_exact = {G_exact}, dp/dx_exact = {dpdx_exact} = {float(dpdx_exact)}")
    print(f"continuous: u_max = {F(3,2)*U} = {float(F(3,2)*U)}")
    print("=" * 100)

    records = {}
    for operator, label in (("two_point", "HISTORICAL two-point wall flux"),
                            ("diff002", "P12-DIFF-002 three-point reconstruction")):
        print(f"\n### {label}   [{operator}]")
        print(f"{'ny':>4} {'G_d/G_exact (exact)':>26} {'dp/dx_d':>14} "
              f"{'rel err vs exact':>18} {'u_centre (exact)':>26} {'u_centre':>12}")
        for ny in (4, 6, 8, 16, 32, 64):
            u, G = build_and_solve(operator, ny, H, mu, U)
            ratio = G / G_exact
            uc = centreline(u, ny, H)
            rel = abs(mu * G - dpdx_exact) / abs(dpdx_exact)
            print(f"{ny:>4} {str(ratio):>26} {float(mu*G):>14.9f} "
                  f"{str(rel):>18} {str(uc):>26} {float(uc):>12.9f}")
            records[(operator, ny)] = (u, G, uc, ratio, rel)

        # Verify the derived closed forms symbolically against the exact solve.
        print("  closed-form check (derived by hand in this file's header):")
        for ny in (4, 6, 8, 16, 32, 64):
            u, G, uc, ratio, _rel = records[(operator, ny)]
            if operator == "two_point":
                pred_ratio = F(ny * ny, ny * ny + 2)
                pred_uc = F(3, 2) * U * F(ny * ny, ny * ny + 2)
            else:
                pred_ratio = F(2 * ny * ny, 2 * ny * ny + 1)
                pred_uc = F(3, 2) * U * F(2 * ny * ny, 2 * ny * ny + 1) * F(ny * ny - 1, ny * ny)
            ok_r = "OK" if ratio == pred_ratio else "MISMATCH"
            ok_c = "OK" if uc == pred_uc else "MISMATCH"
            print(f"    ny={ny:>3}  G ratio {ok_r} ({pred_ratio})   u_centre {ok_c} ({pred_uc})")
            assert ratio == pred_ratio and uc == pred_uc, "closed form disagrees with exact solve"

        # And the profile identity: DIFF-002 must equal the continuous profile at cell centres.
        print("  profile identity:")
        for ny in (8, 32):
            u, G, _uc, _r, _e = records[(operator, ny)]
            h = H / ny
            worst = max(abs(u[j] - (-(G / 2) * ((2 * j + 1) * h / 2) * (H - (2 * j + 1) * h / 2)))
                        for j in range(ny))
            offset = max(abs(u[j] - (-(G / 2) * ((2 * j + 1) * h / 2) * (H - (2 * j + 1) * h / 2))
                             - (-G) * h * h / 8) for j in range(ny))
            print(f"    ny={ny:>3}  max |u_j - continuum(G_d)| = {worst}"
                  f"   max |u_j - continuum(G_d) - (-G)h^2/8| = {offset}")

    # ------------------------------------------------------------------------------------------
    # The historical constants the current U-C tests assert, reproduced from the derivation.
    # ------------------------------------------------------------------------------------------
    print("\n" + "=" * 100)
    print("HISTORICAL CONSTANTS IN THE CURRENT U-C ASSERTIONS, from the two-point derivation")
    print("=" * 100)
    ny = 8
    u8, G8, uc8, r8, e8 = records[("two_point", ny)]
    print(f"  centreline discrete exact      = {uc8} = {float(uc8):.10f}   (test: 1.4545454545)")
    print(f"  = 3/2 * ny^2/(ny^2+2)          = {F(3,2)*F(64,66)}  = 16/11 = {float(F(16,11)):.10f}")
    print(f"  1.5 - centreline               = {F(3,2)-uc8} = {float(F(3,2)-uc8):.10f}"
          f"   (test: 1.5*2/66 = {float(F(3,2)*F(2,66)):.10f})")
    print(f"  dp/dx relative error vs exact  = {e8} = {float(e8):.10f}   (test: 2/66 = "
          f"{float(F(2,66)):.10f})")
    print(f"  drop / drop_exact              = {r8} = {float(r8):.10f}   (test: 64/66 = "
          f"{float(F(64,66)):.10f})")

    print("\n" + "=" * 100)
    print("THE NEW DIFF-002 DISCRETE-EXACT REFERENCE (ny = 8, the validation mesh)")
    print("=" * 100)
    u8d, G8d, uc8d, r8d, e8d = records[("diff002", 8)]
    print(f"  G_d / G_exact                  = {r8d} = {float(r8d):.12f}")
    print(f"  dp/dx_discrete                 = {mu*G8d} = {float(mu*G8d):.12f}")
    print(f"  dp/dx relative error vs exact  = {e8d} = {float(e8d):.12f}")
    print(f"  centreline discrete exact      = {uc8d} = {float(uc8d):.12f}")
    print(f"  1.5 - centreline               = {F(3,2)-uc8d} = {float(F(3,2)-uc8d):.12f}")
    print("  cell-centre profile (exact rationals, and as doubles):")
    h = H / 8
    for j in range(8):
        y = (2 * j + 1) * h / 2
        print(f"    j={j}  y={str(y):>6}  u_j = {str(u8d[j]):>28} = {float(u8d[j]):.12f}"
              f"   continuous = {float(continuous(y, H, U)):.12f}")

    # ------------------------------------------------------------------------------------------
    # Step 6: accuracy against the CONTINUUM, both operators, several resolutions.
    # ------------------------------------------------------------------------------------------
    print("\n" + "=" * 100)
    print("ACCURACY AGAINST THE CONTINUOUS SOLUTION (independent of CFDApp)")
    print("=" * 100)
    print(f"{'ny':>4} | {'two-point |dpdx err|':>21} {'DIFF-002 |dpdx err|':>21} {'ratio':>8}"
          f" | {'two-point |uc err|':>20} {'DIFF-002 |uc err|':>20} {'ratio':>8}")
    prev = {}
    for ny in (4, 8, 16, 32, 64, 128):
        out = {}
        for operator in ("two_point", "diff002"):
            u, G = build_and_solve(operator, ny, H, mu, U)
            uc = centreline(u, ny, H)
            out[operator] = (abs(mu * G - dpdx_exact) / abs(dpdx_exact),
                             abs(uc - F(3, 2) * U) / (F(3, 2) * U))
        rp = float(out["two_point"][0] / out["diff002"][0])
        rc = float(out["two_point"][1] / out["diff002"][1])
        print(f"{ny:>4} | {float(out['two_point'][0]):>21.6e} {float(out['diff002'][0]):>21.6e}"
              f" {rp:>8.3f} | {float(out['two_point'][1]):>20.6e}"
              f" {float(out['diff002'][1]):>20.6e} {rc:>8.3f}")
        if prev:
            for operator in ("two_point", "diff002"):
                for k, name in ((0, "dpdx"), (1, "u_c")):
                    p = math.log(float(prev[operator][k] / out[operator][k])) / math.log(2)
                    print(f"       order {operator:>9} {name}: {p:.4f}")
        prev = out

    print("\n" + "=" * 100)
    print("STEP 7 NON-VACUITY: can the reference tell the operators apart?")
    print("=" * 100)
    ref = records[("diff002", 8)]
    print(f"  reference (correct DIFF-002): u_centre = {float(ref[2]):.12f}, "
          f"dp/dx = {float(mu*ref[1]):.12f}")
    for operator, what in (("two_point", "historical two-point operator"),
                           ("diff002_far_cell_x2", "far-cell coefficient doubled"),
                           ("diff002_far_cell_sign", "far-cell coefficient sign flipped"),
                           ("diff002_no_far_cell", "far-cell term dropped")):
        u, G = build_and_solve(operator, 8, H, mu, U)
        uc = centreline(u, 8, H)
        duc = abs(uc - ref[2])
        dg = abs(mu * G - mu * ref[1]) / abs(mu * ref[1])
        verdict = "DISTINGUISHED" if duc > F(1, 10**6) else "*** NOT DISTINGUISHED ***"
        print(f"  {what:<34} u_centre {float(uc):.12f}  |d u_c| {float(duc):.3e}"
              f"  |d dp/dx|/dp/dx {float(dg):.3e}  -> {verdict}")

    if args.json:
        import json
        ny = 8
        u, G = build_and_solve("diff002", ny, H, mu, U)
        json.dump({"ny": ny,
                   "H": float(H), "mu": float(mu), "U": float(U),
                   "dpdx_exact": float(dpdx_exact),
                   "dpdx_discrete": float(mu * G),
                   "dpdx_ratio_num": (2 * ny * ny), "dpdx_ratio_den": (2 * ny * ny + 1),
                   "centreline_discrete": float(centreline(u, ny, H)),
                   "profile": [float(v) for v in u],
                   "y": [float((2 * j + 1) * (H / ny) / 2) for j in range(ny)]},
                  open(args.json, "w"), indent=2)
        print(f"\nwrote {args.json}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
