#!/usr/bin/env python3
"""G5 investigation, item 3: the analytical square-duct reference, checked independently.

Problem (the gate's G5): side a = 1, walls at y = 0, a and z = 0, a; rho = 1, mu = 0.1, mean
velocity U = 1 (flow rate Q = U a^2); Re = rho U a / mu = 10 (hydraulic diameter D_h = 4A/P = a).
Fully developed flow solves mu (u_yy + u_zz) = dp/dx = -G, u = 0 on the walls, with G fixed by Q.

Two independent closed forms of the same solution:
  (D) double sine series (the gate's reference, tests/integration/case/test_3d_production_cases.cpp):
      u = 16 G a^2/(mu pi^4) sum_{m,n odd} sin(m pi y/a) sin(n pi z/a) / (m n (m^2 + n^2)),
      Q = 64 G a^4/(mu pi^6) sum_{m,n odd} 1/(m^2 n^2 (m^2 + n^2)); truncated at m, n <= 399.
  (S) single series (particular + harmonic):
      u = G/(2 mu) y (a - y) - 4 G a^2/(mu pi^3) sum_{n odd} sin(n pi y/a) cosh(n pi (z - a/2)/a)
                                                             / (n^3 cosh(n pi/2)),
      Q = G a^4/(12 mu) [1 - (192/pi^5) sum_{n odd} tanh(n pi/2)/n^5].
      Its terms decay like exp(-n pi d)/n^3, d = distance from the nearer z-wall. So it converges
      exponentially in the interior but only like 1/n^3 at the z-walls. It is evaluated with
      n <= 20001 (the "truth" here), and its truncation is checked against n <= 2001.
Also: the exact cell averages over every cell of an n x n grid (analytic integrals of (S)), and
face averages (x-normal faces: the same as the cell averages of the cross-section).
"""
import json
import math
import sys

import mpmath as mp
import numpy as np

A, U, MU, RHO = 1.0, 1.0, 0.1, 1.0
DSERIES_MAX = 399
SSERIES_MAX = 20001  # the truth (see the docstring); the truncation check uses 2001

# --- constants in high precision (mpmath) ------------------------------------------------------
mp.mp.dps = 40
K_S = (mp.mpf(1) / 12) * (1 - (192 / mp.pi ** 5) * mp.nsum(
    lambda k: mp.tanh((2 * k + 1) * mp.pi / 2) / (2 * k + 1) ** 5, [0, mp.inf]))
G_EXACT = float(U * MU / (K_S * A ** 2))  # Q = U a^2 = K G a^4 / mu
UMAX_S = float((mp.mpf(G_EXACT) / (2 * MU)) * (A * A / 4) - (4 * mp.mpf(G_EXACT) * A * A / (MU * mp.pi ** 3))
               * mp.nsum(lambda k: (-1) ** k / ((2 * k + 1) ** 3 * mp.cosh((2 * k + 1) * mp.pi / 2)), [0, mp.inf]))


def k_double(nmax=DSERIES_MAX):
    odd = np.arange(1, nmax + 1, 2, dtype=float)
    m, n = odd[:, None], odd[None, :]
    return 64.0 * float(np.sum(1.0 / (m * m * n * n * (m * m + n * n)))) / math.pi ** 6


# --- point values -------------------------------------------------------------------------------
def u_double(y, z, g=G_EXACT, nmax=DSERIES_MAX):
    """Double series on the tensor grid y (ny,), z (nz,) -> (ny, nz)."""
    odd = np.arange(1, nmax + 1, 2, dtype=float)
    w = 1.0 / (odd[:, None] * odd[None, :] * (odd[:, None] ** 2 + odd[None, :] ** 2))
    sy = np.sin(np.pi * np.outer(y, odd) / A)
    sz = np.sin(np.pi * np.outer(z, odd) / A)
    return 16.0 * g * A * A / (MU * math.pi ** 4) * sy @ w @ sz.T


def u_single(y, z, g=G_EXACT, nmax=SSERIES_MAX):
    """Single series on the tensor grid y (ny,), z (nz,) -> (ny, nz)."""
    y, z = np.asarray(y, float), np.asarray(z, float)
    odd = np.arange(1, nmax + 1, 2, dtype=float)
    part = (g / (2 * MU)) * np.outer(y * (A - y), np.ones_like(z))
    sy = np.sin(np.pi * np.outer(y, odd) / A)  # (ny, K)
    # cosh(n pi (z - a/2)/a) / cosh(n pi / 2), evaluated stably as exp differences
    t = np.abs(z - A / 2)[:, None] / A  # (nz, 1)
    ratio = np.exp(np.pi * odd[None, :] * (t - 0.5)) * (1 + np.exp(-2 * np.pi * odd[None, :] * t)) / (
        1 + np.exp(-np.pi * odd[None, :]))
    coef = -4 * g * A * A / (MU * math.pi ** 3) / odd ** 3
    return part + (sy * coef[None, :]) @ ratio.T


# --- cell averages (exact integrals of the single series) ----------------------------------------
def cell_averages(n, g=G_EXACT, nmax=SSERIES_MAX):
    """Exact average of u over every cell of the n x n cross-section grid -> (n, n)."""
    h = A / n
    e = np.linspace(0.0, A, n + 1)
    y0, y1 = e[:-1], e[1:]
    # particular part G/(2 mu) y (a - y): its y-average times 1 (z-average of a constant)
    py = ((A * y1 ** 2 / 2 - y1 ** 3 / 3) - (A * y0 ** 2 / 2 - y0 ** 3 / 3)) / h
    part = (g / (2 * MU)) * np.outer(py, np.ones(n))
    odd = np.arange(1, nmax + 1, 2, dtype=float)
    k = np.pi * odd / A
    sy = (np.cos(np.outer(y0, k)) - np.cos(np.outer(y1, k))) / (np.outer(np.ones(n), k) * h)  # (n, K)
    # average of cosh(k (z - a/2)) / cosh(k a/2) over [z0, z1]: [S(z1) - S(z0)] / (k h) with
    # S(z) = sinh(k (z - c)) / cosh(k c) = (exp(k (x - c)) - exp(-k (x + c))) / (1 + exp(-2 k c)),
    # x = z - c in [-c, c]: every exponential <= 1 (overflow-free for any k).
    c = A / 2

    def s_ratio(zz):
        x = np.outer(zz - c, np.ones_like(k))
        return (np.exp(k * (x - c)) - np.exp(-k * (x + c))) / (1 + np.exp(-2 * k * c))[None, :]

    sz = (s_ratio(e[1:]) - s_ratio(e[:-1])) / (np.outer(np.ones(n), k) * h)
    coef = -4 * g * A * A / (MU * math.pi ** 3) / odd ** 3
    return part + (sy * coef[None, :]) @ sz.T


def main():
    out = {}
    lines = []
    say = lines.append
    k_d = k_double()
    say(f"K single-series (mpmath, 40 digits)       = {mp.nstr(K_S, 20)}")
    say(f"K double-series, m,n <= 399 (the gate)    = {k_d:.15f}   rel. diff {abs(k_d - float(K_S)) / float(K_S):.3e}")
    say(f"K double-series, m,n <= 1999              = {k_double(1999):.15f}")
    say(f"G = -dp/dx = U mu / (K a^2)               = {G_EXACT:.12f}")
    say(f"Fanning f Re = G D_h^2 / (2 mu U) = 1/(2K) = {1 / (2 * float(K_S)):.6f}   (Shah & London: 14.227)")
    say(f"Darcy  f Re = 4 x Fanning                  = {2 / float(K_S):.5f}   (56.91)")
    say(f"u_max = u(a/2, a/2), single series         = {UMAX_S:.12f}   u_max/U (Shah & London: 2.0962)")
    um_d = float(u_double(np.array([0.5]), np.array([0.5]))[0, 0])
    say(f"u_max, double series (399)                 = {um_d:.12f}   rel. diff {abs(um_d - UMAX_S) / UMAX_S:.3e}")
    say(f"Re = rho U a / mu = {RHO * U * A / MU:g}; D_h = 4A/P = {4 * A * A / (4 * A):g} a; Re_Dh = {RHO * U * A / MU:g}")

    # Mean velocity / flow rate by Gauss-Legendre quadrature of the single series (independent of Q's closed form).
    xg, wg = np.polynomial.legendre.leggauss(400)
    xq, wq = 0.5 * (xg + 1), 0.5 * wg
    mean_q = float(wq @ u_single(xq, xq) @ wq)
    say(f"mean velocity by 400x400 Gauss quadrature  = {mean_q:.14f}   (U = 1; flow rate Q = rho U a^2 = {mean_q:.14f})")

    # PDE and wall conditions of the single series (4th-order central differences, random points).
    rng = np.random.default_rng(20260915)
    worst_pde, step = 0.0, 1e-3
    for _ in range(40):
        y, z = rng.uniform(0.05, 0.95, 2)
        f = lambda yy, zz: float(u_single(np.array([yy]), np.array([zz]))[0, 0])
        d2 = lambda g, v: (-g(v + 2 * step) + 16 * g(v + step) - 30 * g(v) + 16 * g(v - step) - g(v - 2 * step)) / (12 * step * step)
        lap = d2(lambda t: f(t, z), y) + d2(lambda t: f(y, t), z)
        worst_pde = max(worst_pde, abs(lap + G_EXACT / MU) / (G_EXACT / MU))
    walls = max(abs(float(u_single(np.array([w0]), np.array([s]))[0, 0])) for w0 in (0.0, 1.0) for s in np.linspace(0, 1, 11))
    walls = max(walls, max(abs(float(u_single(np.array([s]), np.array([w0]))[0, 0])) for w0 in (0.0, 1.0) for s in np.linspace(0, 1, 11)))
    sym = float(np.max(np.abs(u_single(np.linspace(0, 1, 13), np.linspace(0, 1, 13)) - u_single(np.linspace(0, 1, 13), np.linspace(0, 1, 13)).T)))
    say(f"single series: max |lap u + G/mu|/(G/mu) at 40 random points = {worst_pde:.3e};  max |u| on walls = {walls:.3e};  max |u(y,z) - u(z,y)| = {sym:.3e}")

    # Per-grid: the gate's reference (double series at cell centres) vs the single series; cell averages.
    say("")
    say(" n  | max|D399 - S| at centres | max|S2001 - S| | mean of centre values - 1 | (h^2/24) G/mu | mean of cell averages - 1 | max|avg(S) - avg(D1999)| | max(centre - average)  min(centre - average)")
    grids = {}
    for n in (8, 12, 16, 24, 32, 48, 64, 96, 128, 192, 256):
        h = A / n
        c = (np.arange(n) + 0.5) * h
        us = u_single(c, c)
        ud = u_double(c, c)
        us2001 = u_single(c, c, nmax=2001)
        avg = cell_averages(n)
        # cross-check the cell averages against the double series integrated analytically (m, n <= 1999)
        odd = np.arange(1, 2000, 2, dtype=float)
        e = np.linspace(0, A, n + 1)
        iy = (np.cos(np.pi * np.outer(e[:-1], odd)) - np.cos(np.pi * np.outer(e[1:], odd))) / (np.pi * odd[None, :] * h)
        w = 1.0 / (odd[:, None] * odd[None, :] * (odd[:, None] ** 2 + odd[None, :] ** 2))
        avg_d = 16.0 * G_EXACT / (MU * math.pi ** 4) * iy @ w @ iy.T
        grids[n] = {
            "max_abs_double399_minus_single_at_centres": float(np.max(np.abs(ud - us))),
            "max_abs_single2001_minus_single_at_centres": float(np.max(np.abs(us2001 - us))),
            "mean_centre_values_minus_1": float(us.mean() - 1.0),
            "h2_over_24_G_over_mu": h * h / 24 * G_EXACT / MU,
            "mean_cell_averages_minus_1": float(avg.mean() - 1.0),
            "max_abs_avg_single_minus_avg_double1999": float(np.max(np.abs(avg - avg_d))),
            "centre_minus_average_max": float(np.max(us - avg)),
            "centre_minus_average_min": float(np.min(us - avg)),
        }
        gr = grids[n]
        say(f"{n:3d} | {gr['max_abs_double399_minus_single_at_centres']:.3e}                | {gr['max_abs_single2001_minus_single_at_centres']:.3e}      | {gr['mean_centre_values_minus_1']:+.6e}             | {gr['h2_over_24_G_over_mu']:.6e} | {gr['mean_cell_averages_minus_1']:+.3e}                | {gr['max_abs_avg_single_minus_avg_double1999']:.3e}                 | {gr['centre_minus_average_max']:+.6e}  {gr['centre_minus_average_min']:+.6e}")
    out.update({"K_single_series": mp.nstr(K_S, 20), "K_double_399": k_d, "G": G_EXACT, "u_max": UMAX_S,
                "u_max_double_399": um_d, "fanning_fRe": 1 / (2 * float(K_S)), "mean_velocity_quadrature": mean_q,
                "pde_residual_rel": worst_pde, "wall_max_abs": walls, "grids": grids})
    print("\n".join(lines))
    if len(sys.argv) > 1:
        with open(sys.argv[1], "w") as fh:
            json.dump(out, fh, indent=2)


if __name__ == "__main__":
    main()
