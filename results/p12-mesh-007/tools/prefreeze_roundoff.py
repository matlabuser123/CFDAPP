"""P12-MESH-007 pre-freeze round-off experiment (NOT CFDApp code, NOT a gate result).

Purpose: before the acceptance gate is frozen, measure in IEEE double precision (numpy, independent of
CFDApp) the round-off level of the geometry formulas the MESH-007 design uses, on the meshes and motions
the gate prescribes, and compare it with the round-off bounds the gate states. MESH-006 lesson: derive every
threshold from a computed reference of the same discretization, not by analogy.

Formulas (the architecture document, results/p12-mesh-007/architecture.md):
  2D cell area/centroid: shoelace on the 4 corners, absolute coordinates;
  2D swept area of an edge p->q moving linearly: S(mid) . (dp + dq)/2, S = the edge rotated;
  2D independent swept area: signed shoelace of the swept quadrilateral (p0, p1, q1, q0);
  3D cell volume: trilinear hexahedron, 2x2x2 Gauss of det J;
  3D swept volume of a bilinear face moving linearly: 2x2x2 Gauss in (s, r, t) of xdot . (x_s x x_r);
  3D independent swept volume: 3x3x3 Gauss of det J of the swept (trilinear) region.
Output: the maximum measured value of every quantity and the ratio to the gate bound.
"""
import math
import sys

import numpy as np

EPS = np.finfo(float).eps


# --------------------------------------------------------------------------------------------- 2D
def cartesian2d(nx, ny, lx=1.0, ly=1.0):
    xs = np.array([i * (lx / nx) for i in range(nx + 1)])
    ys = np.array([j * (ly / ny) for j in range(ny + 1)])
    X, Y = np.meshgrid(xs, ys)  # [j, i]
    return np.stack([X, Y], axis=-1)


def shoelace(c):  # c: [..., 4, 2]
    a = c
    b = np.roll(c, -1, axis=-2)
    cross = a[..., 0] * b[..., 1] - b[..., 0] * a[..., 1]
    area2 = cross.sum(axis=-1)
    cx = ((a[..., 0] + b[..., 0]) * cross).sum(axis=-1)
    cy = ((a[..., 1] + b[..., 1]) * cross).sum(axis=-1)
    area = 0.5 * area2
    return area, np.stack([cx / (6.0 * area), cy / (6.0 * area)], axis=-1)


def corners2d(v):
    return np.stack([v[:-1, :-1], v[:-1, 1:], v[1:, 1:], v[1:, :-1]], axis=-2)  # [j, i, 4, 2]


def rot_cw(e):  # (e.y, -e.x): area vector of an edge p->q pointing to its right
    return np.stack([e[..., 1], -e[..., 0]], axis=-1)


def edges2d(v):
    """All cell edges as (p, q) such that rot_cw(q - p) is the OUTWARD normal of the cell, per cell."""
    c = corners2d(v)  # CCW corners 0..3
    # edge k: corner k -> corner k+1 (CCW) has outward normal rot_cw(edge)
    p = c
    q = np.roll(c, -1, axis=-2)
    return p, q


def gcl2d(v0, v1):
    a0, _ = shoelace(corners2d(v0))
    a1, _ = shoelace(corners2d(v1))
    p0, q0 = edges2d(v0)
    p1, q1 = edges2d(v1)
    pm = 0.5 * (p0 + p1)
    qm = 0.5 * (q0 + q1)
    S = rot_cw(qm - pm)
    d = 0.5 * ((p1 - p0) + (q1 - q0))
    swept = (S * d).sum(axis=-1)  # [j, i, 4] outward swept area per cell edge
    res = a1 - a0 - swept.sum(axis=-1)
    # independent: signed shoelace of the swept quadrilateral (p0, p1, q1, q0); for an edge moving along
    # its outward normal this polygon is counter-clockwise, i.e. positive = outward swept area.
    quad = np.stack([p0, p1, q1, q0], axis=-2)
    ind, _ = shoelace(quad)
    return res, a0, a1, swept, ind


def run2d(name, v_ref, disp, dt, steps, bounds):
    X = 0.0
    out = {"gcl": 0.0, "swept_vs_indep": 0.0, "vel_err": 0.0}
    vprev = v_ref.copy()
    for n in range(steps):
        t1 = (n + 1) * dt
        t0 = n * dt
        v0 = v_ref + disp(v_ref, t0)
        v1 = v_ref + disp(v_ref, t1)
        X = max(X, np.abs(v0).max(), np.abs(v1).max())
        res, a0, a1, swept, ind = gcl2d(v0, v1)
        out["gcl"] = max(out["gcl"], np.abs(res).max())
        out["swept_vs_indep"] = max(out["swept_vs_indep"], np.abs(swept - ind).max())
        vel = (v1 - v0) / dt
        exact_mean = (disp(v_ref, t1) - disp(v_ref, t0)) / dt
        out["vel_err"] = max(out["vel_err"], np.abs(vel - exact_mean).max())
        if (a1 <= 0).any():
            print(f"{name}: NON-POSITIVE AREA at step {n+1}")
    b = bounds(X, dt)
    print(f"2D {name:28s} X={X:.3f}  GCL {out['gcl']:.2e} (bound {b['gcl']:.2e}, ratio {out['gcl']/b['gcl']:.3f})"
          f"  swept-vs-indep {out['swept_vs_indep']:.2e} (bound {b['gcl']:.2e})"
          f"  vel {out['vel_err']:.2e} (bound {b['vel']:.2e}, ratio {out['vel_err']/b['vel']:.3f})")
    return out, X


def bounds2d(X, dt):
    return {"gcl": 256 * EPS * X * X, "vel": 8 * EPS * X / dt}


def affine2d(G, c, b):
    G = np.array(G, float)
    c = np.array(c, float)
    b = np.array(b, float)
    return lambda X, t: t * ((X - c) @ G.T + b)


def sinus2d(A, w, amp_dir=(1.0, 1.0)):
    d = np.array(amp_dir, float)
    return lambda X, t: (A * math.sin(w * t) * np.sin(math.pi * X[..., 0]) * np.sin(math.pi * X[..., 1]))[..., None] * d


def affine_geom_check2d(v_ref, G, c, b, dt, steps):
    """Kernel geometry of the moved mesh vs the affine image of the reference geometry."""
    G = np.array(G, float)
    c = np.array(c, float)
    b = np.array(b, float)
    a_ref, c_ref = shoelace(corners2d(v_ref))
    worst_v = worst_c = 0.0
    X = 0.0
    for n in range(1, steps + 1):
        t = n * dt
        v = v_ref + t * ((v_ref - c) @ G.T + b)
        X = max(X, np.abs(v).max())
        A = np.eye(2) + t * G
        a, cc = shoelace(corners2d(v))
        worst_v = max(worst_v, np.abs(a - np.linalg.det(A) * a_ref).max())
        img = (c_ref - c) @ A.T + c + t * b
        worst_c = max(worst_c, (np.abs(cc - img).max(axis=-1) * a).max())  # |dc| * V
    return worst_v, worst_c, X


# --------------------------------------------------------------------------------------------- 3D
g2 = [0.5 - 0.5 / math.sqrt(3.0), 0.5 + 0.5 / math.sqrt(3.0)]
w2 = [0.5, 0.5]
g3 = [0.5 - 0.5 * math.sqrt(0.6), 0.5, 0.5 + 0.5 * math.sqrt(0.6)]
w3 = [5 / 18, 8 / 18, 5 / 18]


def cartesian3d(n):
    xs = np.array([i * (1.0 / n) for i in range(n + 1)])
    Z, Y, Xg = np.meshgrid(xs, xs, xs, indexing="ij")  # [k, j, i]
    return np.stack([Xg, Y, Z], axis=-1)


def hex_corners(v):  # [k, j, i, 8, 3] order (0,0,0),(1,0,0),(1,1,0),(0,1,0),(0,0,1),(1,0,1),(1,1,1),(0,1,1)
    return np.stack([v[:-1, :-1, :-1], v[:-1, :-1, 1:], v[:-1, 1:, 1:], v[:-1, 1:, :-1],
                     v[1:, :-1, :-1], v[1:, :-1, 1:], v[1:, 1:, 1:], v[1:, 1:, :-1]], axis=-2)


def trilinear_volume(c, g, w):
    x = [c[..., a, :] for a in range(8)]
    vol = 0.0
    for i, xi in enumerate(g):
        for j, eta in enumerate(g):
            for k, zeta in enumerate(g):
                # d/dxi
                dxi = ((x[1] - x[0]) * (1 - eta) * (1 - zeta) + (x[2] - x[3]) * eta * (1 - zeta)
                       + (x[5] - x[4]) * (1 - eta) * zeta + (x[6] - x[7]) * eta * zeta)
                deta = ((x[3] - x[0]) * (1 - xi) * (1 - zeta) + (x[2] - x[1]) * xi * (1 - zeta)
                        + (x[7] - x[4]) * (1 - xi) * zeta + (x[6] - x[5]) * xi * zeta)
                dzeta = ((x[4] - x[0]) * (1 - xi) * (1 - eta) + (x[5] - x[1]) * xi * (1 - eta)
                         + (x[6] - x[2]) * xi * eta + (x[7] - x[3]) * (1 - xi) * eta)
                detJ = (dxi * np.cross(deta, dzeta)).sum(axis=-1)
                vol = vol + w[i] * w[j] * w[k] * detJ
    return vol


# six faces of a hex with outward orientation, vertex order p0..p3 s.t. 1/2 (p2-p0)x(p3-p1) is outward
FACES = [(0, 4, 7, 3),  # x-min (west): outward -x
         (1, 2, 6, 5),  # x-max (east): +x
         (0, 1, 5, 4),  # y-min (south): -y
         (3, 7, 6, 2),  # y-max (north): +y
         (0, 3, 2, 1),  # z-min (bottom): -z
         (4, 5, 6, 7)]  # z-max (top): +z


def face_area(p):
    return 0.5 * np.cross(p[2] - p[0], p[3] - p[1])


def swept_face(p0, p1, g, w):
    """2x2x2 Gauss in (s, r, t) of xdot . (x_s x x_r) for the bilinear face p (4 vertices) moving linearly."""
    d = [p1[a] - p0[a] for a in range(4)]
    tot = 0.0
    for it, t in enumerate(g):
        pt = [p0[a] + t * d[a] for a in range(4)]
        for i_s, s in enumerate(g):
            for i_r, r in enumerate(g):
                xs = (1 - r) * (pt[1] - pt[0]) + r * (pt[2] - pt[3])
                xr = (1 - s) * (pt[3] - pt[0]) + s * (pt[2] - pt[1])
                xd = (1 - s) * (1 - r) * d[0] + s * (1 - r) * d[1] + s * r * d[2] + (1 - s) * r * d[3]
                tot = tot + w[it] * w[i_s] * w[i_r] * (xd * np.cross(xs, xr)).sum(axis=-1)
    return tot


def swept_face_indep(p0, p1):
    """Independent: the swept region is the trilinear hex (face at t0 as bottom, t1 as top); 3x3x3 Gauss."""
    c = np.stack([p0[0], p0[1], p0[2], p0[3], p1[0], p1[1], p1[2], p1[3]], axis=-2)
    return trilinear_volume(c, g3, w3)


def run3d(name, v_ref, disp, dt, steps):
    X = 0.0
    h = 1.0 / (v_ref.shape[0] - 1)
    worst = {"gcl": 0.0, "indep": 0.0, "vel": 0.0}
    for n in range(steps):
        t0, t1 = n * dt, (n + 1) * dt
        v0 = v_ref + disp(v_ref, t0)
        v1 = v_ref + disp(v_ref, t1)
        X = max(X, np.abs(v0).max(), np.abs(v1).max())
        c0 = hex_corners(v0)
        c1 = hex_corners(v1)
        V0 = trilinear_volume(c0, g2, w2)
        V1 = trilinear_volume(c1, g2, w2)
        sw = 0.0
        for f in FACES:
            p0 = [c0[..., a, :] for a in f]
            p1 = [c1[..., a, :] for a in f]
            s = swept_face(p0, p1, g2, w2)
            si = swept_face_indep(p0, p1)
            worst["indep"] = max(worst["indep"], np.abs(s - si).max())
            sw = sw + s
        worst["gcl"] = max(worst["gcl"], np.abs(V1 - V0 - sw).max())
        vel = (v1 - v0) / dt
        exact_mean = (disp(v_ref, t1) - disp(v_ref, t0)) / dt
        worst["vel"] = max(worst["vel"], np.abs(vel - exact_mean).max())
        if (V1 <= 0).any():
            print(f"{name}: NON-POSITIVE VOLUME at step {n+1}")
    hc = h * 2.0  # generous longest-edge estimate for the deformed cells
    b_gcl = 1024 * EPS * X * hc * hc
    b_vel = 8 * EPS * X / dt
    print(f"3D {name:28s} X={X:.3f}  GCL {worst['gcl']:.2e} (bound {b_gcl:.2e}, ratio {worst['gcl']/b_gcl:.3f})"
          f"  swept-vs-indep {worst['indep']:.2e} (bound {b_gcl:.2e}, ratio {worst['indep']/b_gcl:.3f})"
          f"  vel {worst['vel']:.2e} (bound {b_vel:.2e}, ratio {worst['vel']/b_vel:.3f})")
    return worst


def affine3d(G, c, b):
    G = np.array(G, float)
    c = np.array(c, float)
    b = np.array(b, float)
    return lambda X, t: t * ((X - c) @ G.T + b)


def sinus3d(A, w, d):
    d = np.array(d, float)
    return lambda X, t: (A * math.sin(w * t) * np.sin(math.pi * X[..., 0]) * np.sin(math.pi * X[..., 1])
                         * np.sin(math.pi * X[..., 2]))[..., None] * d


def main():
    dt, steps = 0.02, 20
    w = 2 * math.pi / (steps * dt)
    v = cartesian2d(16, 16)
    print("# P12-MESH-007 pre-freeze round-off experiment (numpy, independent of CFDApp); eps =", EPS)
    run2d("M1 sinusoidal (A=0.05)", v, sinus2d(0.05, w), dt, steps, bounds2d)
    run2d("M2 translation", v, affine2d([[0, 0], [0, 0]], [0.5, 0.5], [0.3, -0.2]), dt, steps, bounds2d)
    run2d("M3 expansion (0.5)", v, affine2d([[0.5, 0], [0, 0.5]], [0.5, 0.5], [0, 0]), dt, steps, bounds2d)
    run2d("M4 shear (1.0)", v, affine2d([[0, 1.0], [0, 0]], [0.5, 0.5], [0, 0]), dt, steps, bounds2d)
    # distorted structured quad, boundary-preserving distortion
    vd = v.copy()
    X_, Y_ = v[..., 0], v[..., 1]
    vd[..., 0] = X_ + 0.03 * np.sin(math.pi * X_) * np.sin(2 * math.pi * Y_)
    vd[..., 1] = Y_ + 0.03 * np.sin(2 * math.pi * X_) * np.sin(math.pi * Y_)
    run2d("M5 distorted + sinusoidal", vd, sinus2d(0.05, w), dt, steps, bounds2d)
    # piston (32x8 on [0,2]x[0,1]) and translating Couette/cavity
    vp = cartesian2d(32, 8, 2.0, 1.0)
    run2d("G7.1 piston", vp, affine2d([[-0.25, 0], [0, 0]], [0, 0], [0, 0]), 0.05, 20, bounds2d)
    run2d("G6.3 translating cavity", v, affine2d([[0, 0], [0, 0]], [0, 0], [0.5, 0.25]), 0.01, 20, bounds2d)
    for nm, G, b in [("translation", [[0, 0], [0, 0]], [0.3, -0.2]), ("expansion", [[0.5, 0], [0, 0.5]], [0, 0]),
                     ("shear", [[0, 1.0], [0, 0]], [0, 0])]:
        wv, wc, X = affine_geom_check2d(v, G, [0.5, 0.5], b, dt, steps)
        print(f"2D affine geometry {nm:12s} |dV| {wv:.2e} (bound 16epsX^2 {16*EPS*X*X:.2e}, ratio {wv/(16*EPS*X*X):.3f})"
              f"  |dc|*V {wc:.2e} (bound 32epsX^3 {32*EPS*X**3:.2e}, ratio {wc/(32*EPS*X**3):.3f})")
    v3 = cartesian3d(8)
    run3d("S1 sinusoidal 3D (A=0.05)", v3, sinus3d(0.05, w, (1.0, 0.5, -0.75)), dt, steps)
    run3d("translation 3D", v3, affine3d(np.zeros((3, 3)), [0.5] * 3, [0.3, -0.2, 0.1]), dt, steps)
    run3d("expansion 3D (0.5)", v3, affine3d(0.5 * np.eye(3), [0.5] * 3, [0, 0, 0]), dt, steps)
    run3d("shear 3D", v3, affine3d([[0, 0.5, 0.3], [0, 0, 0.4], [0, 0, 0]], [0.5] * 3, [0, 0, 0]), dt, steps)


if __name__ == "__main__":
    sys.exit(main())
