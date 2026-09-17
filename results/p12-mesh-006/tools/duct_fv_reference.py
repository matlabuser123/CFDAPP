#!/usr/bin/env python3
"""P12-MESH-006 diagnostic (independent of CFDApp): the discretization error of the cell-centred
finite-volume scheme on the fully developed square-duct problem.

Fully developed duct flow reduces the axial momentum equation to the 2D Poisson problem
mu (u_yy + u_zz) = dp/dx = -G on the cross-section [0, 1]^2 with u = 0 on the walls and the flow
rate fixed (mean u = U = 1). Discretized exactly as CFDApp's momentum diffusion on a uniform
Cartesian mesh: two-point face gradients, (u_N - u_P)/h on interior faces, (0 - u_P)/(h/2) on
wall faces (half-cell wall distance). With G fixed by the flow rate, this gives the discrete
fully developed solution. The printout is the G5 metrics of that solution against the exact
series: cross-section Linf and RMS, |G error|/G, and |u_max error|/u_max (mean of the four
axis-adjacent cells, Amendment A2.3).

If CFDApp's converged 3D duct reproduces these numbers, its G5 error is this scheme's
discretization error of the fully developed flow, and nothing else.
"""
import math
import sys

import numpy as np

KMAX = 399  # m, n odd <= 399, as acceptance_gate.md G5


def series_k():
    s = 0.0
    for m in range(1, KMAX + 1, 2):
        for n in range(1, KMAX + 1, 2):
            s += 1.0 / (m * m * n * n * (m * m + n * n))
    return 64.0 * s / math.pi ** 6


K = series_k()
G_OVER_MU = 1.0 / K  # U = 1, a = 1: Q = U a^2 = K G a^4 / mu
ODD = np.arange(1, KMAX + 1, 2, dtype=float)


def exact_u(y, z):
    sy = np.sin(ODD * math.pi * y)
    sz = np.sin(ODD * math.pi * z)
    m = ODD[:, None]
    n = ODD[None, :]
    return 16.0 * G_OVER_MU / math.pi ** 4 * float(np.sum(np.outer(sy, sz) / (m * n * (m * m + n * n))))


def fv_solution(n):
    h = 1.0 / n
    size = n * n
    a = np.zeros((size, size))
    for j in range(n):
        for i in range(n):
            p = i + n * j
            for di, dj in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                ii, jj = i + di, j + dj
                if 0 <= ii < n and 0 <= jj < n:
                    q = ii + n * jj
                    a[p, p] -= 1.0
                    a[p, q] += 1.0
                else:
                    a[p, p] -= 2.0  # wall at h/2: (0 - u_P) / (h/2) * h
    phi = np.linalg.solve(a, -np.full(size, h * h))  # G/mu = 1
    g = 1.0 / phi.mean()  # fix the flow rate: mean u = 1
    return phi * g, g


def metrics(n):
    u, g = fv_solution(n)
    h = 1.0 / n
    err = []
    for j in range(n):
        for i in range(n):
            err.append(abs(u[i + n * j] - exact_u((i + 0.5) * h, (j + 0.5) * h)))
    err = np.array(err)
    c = n // 2
    umax = 0.25 * (u[(c - 1) + n * (c - 1)] + u[c + n * (c - 1)] + u[(c - 1) + n * c] + u[c + n * c])
    exact_max = exact_u(0.5, 0.5)
    return err.max(), math.sqrt((err ** 2).mean()), abs(g - G_OVER_MU) / G_OVER_MU, abs(umax - exact_max) / exact_max


def main():
    print(f"exact: K = {K:.7f}, G/mu = {G_OVER_MU:.6f}, u_max = {exact_u(0.5, 0.5):.6f}")
    print(" n |    Linf        RMS      |G err|/G   |u_max err|/u_max")
    for n in [int(a) for a in sys.argv[1:]] or [8, 16, 24]:
        linf, rms, gerr, uerr = metrics(n)
        print(f"{n:2d} | {linf:.4e}  {rms:.4e}  {gerr:.4e}  {uerr:.4e}")


if __name__ == "__main__":
    main()
