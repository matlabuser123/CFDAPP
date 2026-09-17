#!/usr/bin/env python3
"""P12-MESH-007 gate G2.3: the moved-mesh geometry of the SN2 (2D) and SN3 (3D) runs, recomputed
by an implementation independent of CFDApp's C++ code, compared with the kernel's dumped geometry
within the G2.2 bounds (acceptance_gate.md).

Input: the JSON files written by the DISABLED test
MeshMotionEvidence.DumpSinusoidalGeometryForIndependentCheck (M7_DATA_DIR): topology (cell
corners, face vertices, owner, neighbour) and, per step, the vertices and the kernel's cell volumes
and centroids and face centroids and area vectors.

Independent definitions, taken from the architecture document (section 3), not from the code:
  2D  cell: shoelace area and centroid of the four corners (counter-clockwise);
      face: midpoint of the edge; area vector = the edge rotated by 90 degrees, oriented from the
            owner to the neighbour (or out of the domain), the orientation decided HERE from the
            independently computed centroids;
  3D  cell: trilinear hexahedron; volume = integral of det J, centroid = integral of x det J / V,
            2x2x2 Gauss on the unit cube (exact: degree <= 3 per variable);
      face: vector area = integral of x_u x x_v over the bilinear patch (2x2 Gauss, exact),
            oriented as in 2D; centroid = the projected-area-weighted centroid of the four
            triangles about the vertex average (the architecture's definition).
Bounds (G2.2): X = the largest absolute vertex coordinate of the step, h_c = the cell's longest
edge, h_f = the face's longest diagonal:
  2D  |dV| <= 16 eps X^2;  |dx_c| V <= 32 eps X^3;  |dx_f| <= 8 eps X;  |dS| <= 8 eps X
  3D  |dV| <= 64 eps X h_c^2;  |dx_c| <= 64 eps X;  |dx_f| <= 16 eps X;  |dS| <= 16 eps X h_f
usage: independent_geometry.py <data-dir> [--self-test]
  --self-test  also re-evaluates every file with the kernel's first cell volume perturbed by 1e-10
               (relative); that perturbation exceeds every volume bound and must be reported FAIL.
"""
import json
import math
import sys

import numpy as np

EPS = np.finfo(float).eps
G = np.array([0.5 - 0.5 / math.sqrt(3.0), 0.5 + 0.5 / math.sqrt(3.0)])  # Gauss on [0, 1]
W = np.array([0.5, 0.5])
# (u, v, w) of the eight corners in MeshGeometry::StructuredTopology order
CORNER_UVW = np.array([[0, 0, 0], [1, 0, 0], [1, 1, 0], [0, 1, 0],
                       [0, 0, 1], [1, 0, 1], [1, 1, 1], [0, 1, 1]], dtype=float)


def shoelace(c):
    a, b = c, np.roll(c, -1, axis=0)
    cross = a[:, 0] * b[:, 1] - b[:, 0] * a[:, 1]
    area = 0.5 * cross.sum()
    cx = ((a[:, 0] + b[:, 0]) * cross).sum() / (6.0 * area)
    cy = ((a[:, 1] + b[:, 1]) * cross).sum() / (6.0 * area)
    return area, np.array([cx, cy, 0.0])


def trilinear(c):
    """volume and centroid of the trilinear hexahedron with corners c[8, 3]."""
    vol, mom = 0.0, np.zeros(3)
    for iu, u in enumerate(G):
        for iv, v in enumerate(G):
            for iw, w in enumerate(G):
                s = np.array([u, v, w])
                wt = W[iu] * W[iv] * W[iw]
                shape = np.prod(np.where(CORNER_UVW == 1, s, 1 - s), axis=1)
                x = shape @ c
                jac = np.zeros((3, 3))
                for d in range(3):
                    # derivative of each shape function with respect to s_d
                    deriv = np.ones(8)
                    for e in range(3):
                        if e == d:
                            deriv *= np.where(CORNER_UVW[:, e] == 1, 1.0, -1.0)
                        else:
                            deriv *= np.where(CORNER_UVW[:, e] == 1, s[e], 1 - s[e])
                    jac[:, d] = deriv @ c
                det = np.linalg.det(jac)
                vol += wt * det
                mom += wt * det * x
    return vol, mom / vol


def bilinear_area(p):
    """integral of x_u x x_v over x(u,v) = (1-u)(1-v)p0 + u(1-v)p1 + uv p2 + (1-u)v p3."""
    s = np.zeros(3)
    for iu, u in enumerate(G):
        for iv, v in enumerate(G):
            xu = (p[1] - p[0]) * (1 - v) + (p[2] - p[3]) * v
            xv = (p[3] - p[0]) * (1 - u) + (p[2] - p[1]) * u
            s += W[iu] * W[iv] * np.cross(xu, xv)
    return s


def triangle_centroid(p, n):
    m = p.mean(axis=0)
    num, den = np.zeros(3), 0.0
    for k in range(4):
        a, b = p[k], p[(k + 1) % 4]
        w = 0.5 * np.dot(np.cross(a - m, b - m), n)
        num += w * (m + a + b) / 3.0
        den += w
    return num / den


def main():
    import glob
    worst = {}
    self_test = "--self-test" in sys.argv[2:]
    files = sorted(glob.glob(sys.argv[1] + "/geometry_*.json"))
    if not files:
        print("no dump files"); return 1
    ok = True
    for path in files:
        d = json.load(open(path))
        dim = d["dimension"]
        cells = np.array(d["cells"], dtype=int)
        faces = np.array(d["faces"], dtype=int)
        owner = np.array(d["owner"], dtype=int)
        nb = np.array(d["neighbor"], dtype=int)
        ratios = {"volume": 0.0, "centroid": 0.0, "faceCentroid": 0.0, "areaVector": 0.0}
        for step in d["steps"]:
            v = np.array(step["vertices"], dtype=float).reshape(-1, 3)
            X = float(np.abs(v).max())
            kvol = np.array(step["volume"], dtype=float)
            if self_test:
                kvol[0] *= 1.0 + 1e-10
            kcen = np.array(step["centroid"], dtype=float).reshape(-1, 3)
            kfc = np.array(step["faceCentroid"], dtype=float).reshape(-1, 3)
            kS = np.array(step["areaVector"], dtype=float).reshape(-1, 3)
            vol = np.zeros(len(cells))
            cen = np.zeros((len(cells), 3))
            hc = np.zeros(len(cells))
            for c, corner in enumerate(cells):
                if dim == 2:
                    pts = v[corner[:4]]
                    vol[c], cen[c] = shoelace(pts[:, :2])
                    hc[c] = max(np.linalg.norm(pts[k] - pts[(k + 1) % 4]) for k in range(4))
                else:
                    pts = v[corner[:8]]
                    vol[c], cen[c] = trilinear(pts)
                    edges = [(0, 1), (1, 2), (2, 3), (3, 0), (4, 5), (5, 6), (6, 7), (7, 4),
                             (0, 4), (1, 5), (2, 6), (3, 7)]
                    hc[c] = max(np.linalg.norm(pts[a] - pts[b]) for a, b in edges)
            fc = np.zeros((len(faces), 3))
            S = np.zeros((len(faces), 3))
            hf = np.zeros(len(faces))
            for f, fv in enumerate(faces):
                if dim == 2:
                    p, q = v[fv[0]], v[fv[1]]
                    fc[f] = 0.5 * (p + q)
                    e = q - p
                    s = np.array([e[1], -e[0], 0.0])
                    hf[f] = np.linalg.norm(e)
                else:
                    p = v[fv[:4]]
                    s = bilinear_area(p)
                    fc[f] = triangle_centroid(p, s / np.linalg.norm(s))
                    hf[f] = max(np.linalg.norm(p[2] - p[0]), np.linalg.norm(p[3] - p[1]))
                ref = cen[nb[f]] if nb[f] >= 0 else fc[f]
                if np.dot(s, ref - cen[owner[f]]) < 0:
                    s = -s
                S[f] = s
            if dim == 2:
                bv = 16 * EPS * X * X
                bc = 32 * EPS * X ** 3
                bf = 8 * EPS * X
                bs = 8 * EPS * X
                rv = np.abs(vol - kvol) / bv
                rc = np.linalg.norm(cen - kcen, axis=1) * vol / bc
                rf = np.linalg.norm(fc - kfc, axis=1) / bf
                rs = np.linalg.norm(S - kS, axis=1) / bs
            else:
                rv = np.abs(vol - kvol) / (64 * EPS * X * hc * hc)
                rc = np.linalg.norm(cen - kcen, axis=1) / (64 * EPS * X)
                rf = np.linalg.norm(fc - kfc, axis=1) / (16 * EPS * X)
                rs = np.linalg.norm(S - kS, axis=1) / (16 * EPS * X * hf)
            for key, r in (("volume", rv), ("centroid", rc), ("faceCentroid", rf), ("areaVector", rs)):
                ratios[key] = max(ratios[key], float(r.max()))
        verdict = all(r <= 1.0 for r in ratios.values())
        ok = ok and verdict
        print(f"{'SELF-TEST ' if self_test else ''}G2.3 {d['case']:8s} steps {len(d['steps'])}: "
              + ", ".join(f"{k} {r:.3e} of bound" for k, r in ratios.items())
              + f" -> {'PASS' if verdict else 'FAIL'}")
        worst[d["case"]] = ratios
    print("G2.3 overall: " + ("PASS" if ok else "FAIL"))
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
