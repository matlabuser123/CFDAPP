#!/usr/bin/env python3
"""G5 investigation, items 5-8: the independent discrete fully developed duct solution.

Fully developed flow reduces the momentum balance of every cell of a cross-section to the
cell-centred finite-volume Poisson problem that CFDApp's momentum diffusion defines on a uniform
Cartesian mesh (MESH-006 summary §17). It uses two-point face gradients mu (u_N - u_P)/h on
interior faces, mu (0 - u_P)/(h/2) on wall faces (half-cell wall distance), and the pressure
source integrated exactly (G V). The flow rate is fixed: mean cell velocity = U (the section mass
flow sum rho u_P h^2 = rho U a^2).

    sum_nb (u_nb - u_P) - 2 u_P [per wall face] = -(G/mu) h^2,   mean(u) = U.

It is solved exactly, to round-off: the 1D operator T (-2 on the diagonal, -3 in the wall cells,
1 off-diagonal) is diagonalized, and the 2D operator T x I + I x T is inverted mode by mode. A
sparse direct solve (scipy) cross-checks it. No CFDApp code or output is used.

Metrics against the analytical truth (tools/duct_reference.py), for both reference quantities:
  point   - the exact u at the cell centres (the gate's metric);
  average - the exact cell average of u.
"""
import json
import sys

import numpy as np
import scipy.sparse as sp
import scipy.sparse.linalg as spla

sys.path.insert(0, __file__.rsplit("/", 1)[0])
import duct_reference as ref  # noqa: E402


def operator_1d(n):
    t = np.diag(-2.0 * np.ones(n)) + np.diag(np.ones(n - 1), 1) + np.diag(np.ones(n - 1), -1)
    t[0, 0] = t[-1, -1] = -3.0
    return t


def solve_eigen(n):
    """phi solving A phi = -h^2 (G/mu = 1), via T = Q L Q^T."""
    h = ref.A / n
    lam, q = np.linalg.eigh(operator_1d(n))
    f = -h * h * np.ones((n, n))
    fh = q.T @ f @ q
    phi = q @ (fh / (lam[:, None] + lam[None, :])) @ q.T
    return phi


def solve_sparse(n):
    h = ref.A / n
    t = sp.csr_matrix(operator_1d(n))
    eye = sp.identity(n, format="csr")
    a = sp.kron(t, eye) + sp.kron(eye, t)
    return spla.spsolve(a.tocsc(), -h * h * np.ones(n * n)).reshape(n, n)


def discrete(n):
    phi = solve_eigen(n)
    g_over_mu = ref.U / phi.mean()
    return phi * g_over_mu, g_over_mu * ref.MU  # u (n x n, index [j_y, k_z]), G


def truncation(n, u_exact_cells):
    """Local truncation error per unit volume: (A u_exact + (G/mu) h^2) / h^2 * mu, per cell."""
    h = ref.A / n
    t = operator_1d(n)
    au = t @ u_exact_cells + u_exact_cells @ t.T
    return (au + (ref.G_EXACT / ref.MU) * h * h) / (h * h) * ref.MU


def metrics(u, reference, n):
    e = u - reference
    a = np.abs(e)
    j, k = np.unravel_index(np.argmax(a), a.shape)
    ring = np.zeros_like(a, dtype=bool)
    ring[0, :] = ring[-1, :] = ring[:, 0] = ring[:, -1] = True
    return {
        "linf": float(a.max()),
        "rms": float(np.sqrt((e * e).mean())),  # uniform cells: = area-weighted L2
        "l1": float(a.mean()),
        "mean_error": float(e.mean()),
        "linf_cell": [int(j), int(k)],
        "linf_cells_from_wall": int(min(j, k, n - 1 - j, n - 1 - k)),
        "linf_signed": float(e[j, k]),
        "linf_wall_ring": float(a[ring].max()),
        "linf_interior": float(a[~ring].max()) if n > 2 else 0.0,
    }


def run(ns, check_sparse_up_to=64):
    rows = {}
    for n in ns:
        h = ref.A / n
        c = (np.arange(n) + 0.5) * h
        u, g = discrete(n)
        point = ref.u_single(c, c)
        avg = ref.cell_averages(n)
        m = n // 2
        axis = 0.25 * (u[m - 1, m - 1] + u[m, m - 1] + u[m - 1, m] + u[m, m])
        row = {
            "n": n,
            "G": g,
            "G_rel_error": abs(g - ref.G_EXACT) / ref.G_EXACT,
            "u_axis": float(axis),
            "u_axis_rel_error": abs(axis - ref.UMAX_S) / ref.UMAX_S,
            "mean_velocity": float(u.mean()),
            "point": metrics(u, point, n),
            "average": metrics(u, avg, n),
        }
        if n <= check_sparse_up_to:
            us, _ = solve_sparse(n), None
            us = us * (ref.U / us.mean())
            row["eigen_vs_sparse_max_abs"] = float(np.max(np.abs(us - u)))
        tau_p = truncation(n, point)
        ring = np.zeros((n, n), dtype=bool)
        ring[0, :] = ring[-1, :] = ring[:, 0] = ring[:, -1] = True
        row["truncation_point"] = {"interior_max": float(np.abs(tau_p[~ring]).max()),
                                   "wall_ring_max": float(np.abs(tau_p[ring]).max())}
        rows[n] = row
    return rows


def main():
    ns = [int(a) for a in sys.argv[2:]] or [8, 12, 16, 24, 32, 48, 64, 96, 128, 192, 256]
    rows = run(ns)
    print(" n  | G rel err  | u_axis err | point: Linf   RMS        L1         (Linf cell, from wall) | average: Linf  RMS        L1        | eigen-sparse | tau interior  tau wall")
    for n, r in rows.items():
        p, a = r["point"], r["average"]
        print(f"{n:3d} | {r['G_rel_error']:.4e} | {r['u_axis_rel_error']:.4e} | {p['linf']:.4e} {p['rms']:.4e} {p['l1']:.4e} "
              f"({p['linf_cell'][0]},{p['linf_cell'][1]}; {p['linf_cells_from_wall']}) | {a['linf']:.4e} {a['rms']:.4e} {a['l1']:.4e} | "
              f"{r.get('eigen_vs_sparse_max_abs', float('nan')):.2e}     | {r['truncation_point']['interior_max']:.3e}  {r['truncation_point']['wall_ring_max']:.3e}")
    if len(sys.argv) > 1 and sys.argv[1] != "-":
        with open(sys.argv[1], "w") as fh:
            json.dump(rows, fh, indent=2)


if __name__ == "__main__":
    main()
