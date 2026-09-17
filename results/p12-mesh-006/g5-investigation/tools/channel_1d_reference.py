#!/usr/bin/env python3
"""G5 investigation: the premise of the G5.1 threshold derivation, checked independently.

acceptance_gate.md G5.1 derived its limits from the 2D production Poiseuille dp/dx error (0.585 % with
18 cells across, P12-MESH-001, a distorted structured_quad mesh) by second-order scaling. It
estimated about 0.74 % at 16 cells and 0.33 % at 24 cells, and allowed about 4x that.

The fully developed 2D channel under CFDApp's Cartesian discretization is the 1D problem
mu u'' = -G on [0, H], u = 0 at the walls, mean(u) = U. It has two-point face gradients, a
half-cell wall distance and the exact source integral, and the exact solution
u = (G/2mu) y (H - y), G = 12 mu U / H^2. This prints the channel's G and velocity errors
next to the square duct's, from the independent 2D solve tools/duct_discrete.py, at the same
cell counts. Neither calculation uses CFDApp.
"""
import pathlib
import sys

import numpy as np

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import duct_discrete as disc  # noqa: E402

MU, U, H = 0.1, 1.0, 1.0


def channel(n):
    h = H / n
    t = disc.operator_1d(n)
    phi = np.linalg.solve(t, -h * h * np.ones(n))  # G/mu = 1
    g = MU * U / phi.mean()
    u = phi * (U / phi.mean())
    y = (np.arange(n) + 0.5) * h
    exact = (12 * MU * U / H ** 2) / (2 * MU) * y * (H - y)
    g_exact = 12 * MU * U / H ** 2
    return abs(g - g_exact) / g_exact, float(np.max(np.abs(u - exact))), float(np.sqrt(np.mean((u - exact) ** 2)))


print(" cells | channel (1D): |G err|/G  Linf/U     RMS/U     | square duct (2D): |G err|/G  Linf/U     RMS/U     | duct/channel: G  Linf")
for n in (16, 18, 24, 32):
    cg, cl, cr = channel(n)
    d = disc.run([n], check_sparse_up_to=0)[n]
    dg, dl, dr = d["G_rel_error"], d["point"]["linf"], d["point"]["rms"]
    print(f"  {n:3d}  | {cg:.4e}  {cl:.4e}  {cr:.4e}  | {dg:.4e}  {dl:.4e}  {dr:.4e}  | {dg / cg:.2f}  {dl / cl:.2f}")
