#!/usr/bin/env python3
"""P12-MESH-006 Amendment A3: gate evaluator for the fresh G5 / G6 acceptance run.

Written and hashed BEFORE the fresh run (acceptance_gate_A3.md §7, a3/logs/00_freeze.log). It reads
  - the frozen independent expected values a3/data/frozen_expected.json (its sha256 is checked
    against a3/data/frozen_expected.json.sha256 before anything else is read);
  - the fresh production CLI runs $A3_RUNS/{x8,x16,x24,y16,z16} (results/fields.csv with 17
    significant digits, results/metadata.json, the CLI stdout log);
  - the fresh ProjectRunner gtest level runs (unchanged test code, relocated working directory)
    $A3_RUNS/gtest/results/p12-mesh-006/data/duct3d_n{8,16,24}.json, which carry the face-flux
    section mass flows of G5.3.
and evaluates every A3 item (G5.1-A..E, G6.2-A3) plus the unchanged G5.2, G5.3, G5.4 and G6.1.

Usage: a3_gate.py <out_prefix>   (writes <out_prefix>.json and <out_prefix>.txt)
"""
import csv
import hashlib
import json
import math
import os
import pathlib
import sys

import numpy as np

A3 = pathlib.Path(__file__).resolve().parents[1]
FROZEN = A3 / "data" / "frozen_expected.json"
RUNS = pathlib.Path(os.environ.get("A3_RUNS", "~/m6a3")).expanduser()
GTEST_DATA = RUNS / "gtest" / "results" / "p12-mesh-006" / "data"

# --- the A3 criteria (acceptance_gate_A3.md §6) ------------------------------------------------
DISCRETE_VELOCITY_TOL = 1e-5  # G5.1-A, G6.2-A3: max |u - u_disc| / U, and max cross-flow / U
DISCRETE_GRADIENT_TOL = 1e-4  # G5.1-A: |dp/dx - G_disc| / G_disc
ORDER_BAND = (1.8, 2.2)       # G5.1-C: primary metric velocity L-inf, pair 16 -> 24
DPDX_LIMIT_24 = 0.015         # G5.1-D (original G5.1 limit, kept)
UMAX_LIMIT_24 = 0.010         # G5.1-D (original G5.1 limit, kept)
GCI_INDICATOR_BAND = (0.95, 1.05)  # G5.1-E
G6_SYMMETRY_TOL = 1e-6        # G6.1 (unchanged)
MASS_TOL = 1e-6               # G5.3 (unchanged)


def sha256(path):
    return hashlib.sha256(pathlib.Path(path).read_bytes()).hexdigest()


def load_frozen():
    recorded = (A3 / "data" / "frozen_expected.json.sha256").read_text().split()[0]
    actual = sha256(FROZEN)
    if recorded != actual:
        raise SystemExit(f"frozen expected values changed: recorded {recorded}, actual {actual}")
    return json.loads(FROZEN.read_text()), actual


def load_run(name):
    """Canonical (s, c1, c2) fields of a CLI run: s = flow axis, c1 = (axis+1)%3, c2 = (axis+2)%3."""
    case = RUNS / name
    geo = json.loads((case / "geometry.json").read_text())
    mesh = json.loads((case / "mesh.json").read_text())
    lengths = [geo["length"], geo["height"], geo["depth"]]
    cells = [mesh["nx"], mesh["ny"], mesh["nz"]]
    axis = int(np.argmax(lengths))
    a1, a2 = (axis + 1) % 3, (axis + 2) % 3
    n, ns = cells[a1], cells[axis]
    h = 1.0 / n
    us = np.full((ns, n, n), np.nan)
    u1, u2, p = us.copy(), us.copy(), us.copy()
    finite = True
    with open(case / "results" / "fields.csv") as fh:
        rows = csv.reader(fh)
        col = {k: i for i, k in enumerate(next(rows))}
        for r in rows:
            x = [float(r[col[k]]) for k in ("x", "y", "z")]
            v = [float(r[col[k]]) for k in ("velocity_x", "velocity_y", "velocity_z")]
            pr = float(r[col["pressure"]])
            finite = finite and all(math.isfinite(t) for t in v + [pr])
            ijk = [int(math.floor(x[c] / h)) for c in range(3)]
            idx = (ijk[axis], ijk[a1], ijk[a2])
            us[idx], u1[idx], u2[idx], p[idx] = v[axis], v[a1], v[a2], pr
    if np.isnan(us).any():
        raise SystemExit(f"{name}: unmapped cells")
    meta = json.loads((case / "results" / "metadata.json").read_text())
    exported = all((case / "results" / f).is_file() for f in ("fields.csv", "residuals.csv", "metadata.json", "solution.vtk"))
    return {"name": name, "axis": axis, "n": n, "ns": ns, "h": h, "us": us, "u1": u1, "u2": u2, "p": p,
            "meta": meta, "finite": finite, "exported": exported}


def gradient(d):
    s = (np.arange(d["ns"]) + 0.5) * d["h"]
    sel = (s >= 2.5) & (s <= 4.5)
    means = d["p"].reshape(d["ns"], -1).mean(axis=1)
    return -float(np.polyfit(s[sel], means[sel], 1)[0])


def measure(d, frozen):
    n = d["n"]
    g = frozen["grids"][str(n)]
    plane = 4 * n - 1
    u = d["us"][plane]
    u_disc = np.array(g["u_discrete"])
    u_exact = np.array(g["u_exact_point"])
    e = u - u_exact
    m = n // 2
    axis_u = 0.25 * (u[m - 1, m - 1] + u[m, m - 1] + u[m - 1, m] + u[m, m])
    G = gradient(d)
    an = frozen["analytical"]
    solver = d["meta"].get("solver", {})
    return {
        "n": n, "axis": "xyz"[d["axis"]], "status": solver.get("status"), "iterations": solver.get("iterations"),
        "face_flux": solver.get("face_flux"), "finite": d["finite"], "exported": d["exported"],
        "conservation": d["meta"].get("conservation", {}),
        "disc_max_abs_du": float(np.abs(u - u_disc).max()),
        "cross_flow_max": float(max(np.abs(d["u1"][plane]).max(), np.abs(d["u2"][plane]).max())),
        "G": G, "G_disc": g["G_discrete"], "G_rel_diff_disc": abs(G - g["G_discrete"]) / g["G_discrete"],
        "linf": float(np.abs(e).max()), "rms": float(np.sqrt((e * e).mean())), "l1": float(np.abs(e).mean()),
        "dpdx_rel": abs(G - an["G"]) / an["G"], "u_axis": float(axis_u),
        "u_max_rel": abs(axis_u - an["u_max"]) / an["u_max"],
        "predicted": g["predicted_pointwise_errors"],
    }


def gci(phi, ns):
    """Celik et al. (2008), J. Fluids Eng. 130, 078001; phi, ns ordered coarse -> fine."""
    (p3, p2, p1), (n3, n2, n1) = phi, ns
    r21, r32 = n1 / n2, n2 / n3
    e21, e32 = p2 - p1, p3 - p2
    s = math.copysign(1.0, e32 / e21)
    p = math.log(abs(e32 / e21)) / math.log(r21)
    for _ in range(500):
        q = math.log((r21 ** p - s) / (r32 ** p - s))
        new = abs(math.log(abs(e32 / e21)) + q) / math.log(r21)
        if abs(new - p) < 1e-14:
            p = new
            break
        p = new
    ext = (r21 ** p * p1 - p2) / (r21 ** p - 1)
    ea21, ea32 = abs((p1 - p2) / p1), abs((p2 - p3) / p2)
    gf, gc = 1.25 * ea21 / (r21 ** p - 1), 1.25 * ea32 / (r32 ** p - 1)
    return {"grids": list(ns), "phi": list(phi), "p": p, "phi_ext": ext, "e_a21": ea21,
            "GCI_fine": gf, "GCI_coarse": gc, "indicator": gc / (r21 ** p * gf)}


def order(e_coarse, e_fine, n_coarse, n_fine):
    return math.log(e_coarse / e_fine) / math.log(n_fine / n_coarse)  # = ln(Ec/Ef)/ln(hc/hf)


def main():
    frozen, frozen_hash = load_frozen()
    lines, items = [], []

    def gate(label, passed, detail):
        items.append({"item": label, "pass": bool(passed), "detail": detail})
        lines.append(f"{'PASS' if passed else 'FAIL'} {label}: {detail}")

    runs = {name: load_run(name) for name in ("x8", "x16", "x24", "y16", "z16")}
    res = {name: measure(d, frozen) for name, d in runs.items()}
    gt = {n: json.loads((GTEST_DATA / f"duct3d_n{n}.json").read_text()) for n in (8, 16, 24)}
    x = {n: res[f"x{n}"] for n in (8, 16, 24)}

    lines.append(f"P12-MESH-006 Amendment A3 -- fresh acceptance run (frozen expected sha256 {frozen_hash})")
    lines.append("")
    lines.append(" n  | it  | Linf       RMS        L1         | dp/dx err  u_max err  | CFDApp-disc |du|  cross-flow  dG/G_disc")
    for n, r in x.items():
        lines.append(f" {n:2d} | {r['iterations']} | {r['linf']:.4e} {r['rms']:.4e} {r['l1']:.4e} | {r['dpdx_rel']:.4e} "
                     f"{r['u_max_rel']:.4e} | {r['disc_max_abs_du']:.3e}   {r['cross_flow_max']:.3e}   {r['G_rel_diff_disc']:.3e}")
    lines.append("")

    # G5.1-A independent discrete agreement
    for n, r in x.items():
        gate(f"G5.1-A n={n} max|u - u_disc|/U <= {DISCRETE_VELOCITY_TOL:g}", r["disc_max_abs_du"] <= DISCRETE_VELOCITY_TOL,
             f"{r['disc_max_abs_du']:.3e}")
        gate(f"G5.1-A n={n} max cross-flow/U <= {DISCRETE_VELOCITY_TOL:g}", r["cross_flow_max"] <= DISCRETE_VELOCITY_TOL,
             f"{r['cross_flow_max']:.3e}")
        gate(f"G5.1-A n={n} |dp/dx - G_disc|/G_disc <= {DISCRETE_GRADIENT_TOL:g}", r["G_rel_diff_disc"] <= DISCRETE_GRADIENT_TOL,
             f"{r['G_rel_diff_disc']:.3e} (dp/dx {r['G']:.10f}, G_disc {r['G_disc']:.10f})")
    # G5.1-B monotone convergence to the analytical solution
    for key, label in (("linf", "velocity Linf"), ("rms", "velocity RMS"), ("dpdx_rel", "dp/dx"), ("u_max_rel", "u_max")):
        seq = [x[n][key] for n in (8, 16, 24)]
        gate(f"G5.1-B {label} decreasing 8 -> 16 -> 24", seq[0] > seq[1] > seq[2],
             " -> ".join(f"{v:.5e}" for v in seq))
    # G5.1-C observed order (primary: velocity Linf, pair 16 -> 24); others reported
    orders = {}
    for key in ("linf", "rms", "l1", "dpdx_rel", "u_max_rel"):
        orders[key] = {"8->16": order(x[8][key], x[16][key], 8, 16), "16->24": order(x[16][key], x[24][key], 16, 24),
                       "predicted_16->24": order(x[16]["predicted"][key], x[24]["predicted"][key], 16, 24)}
    p = orders["linf"]["16->24"]
    gate(f"G5.1-C observed order of velocity Linf, 16 -> 24, in [{ORDER_BAND[0]}, {ORDER_BAND[1]}]",
         ORDER_BAND[0] <= p <= ORDER_BAND[1], f"p = {p:.4f} (ln(E16/E24)/ln(24/16))")
    lines.append("  reported orders (8->16, 16->24; independent prediction 16->24): " + "; ".join(
        f"{k} {v['8->16']:.4f}, {v['16->24']:.4f} ({v['predicted_16->24']:.4f})" for k, v in orders.items()))
    # G5.1-D fine-grid accuracy at n = 24
    r24, env = x[24], frozen["envelope_n24"]
    gate(f"G5.1-D n=24 |dp/dx err|/G <= {DPDX_LIMIT_24}", r24["dpdx_rel"] <= DPDX_LIMIT_24, f"{r24['dpdx_rel']:.5e}")
    gate(f"G5.1-D n=24 |u_max err|/u_max <= {UMAX_LIMIT_24}", r24["u_max_rel"] <= UMAX_LIMIT_24, f"{r24['u_max_rel']:.5e}")
    envelope = {}
    for key, label in (("linf", "velocity Linf"), ("rms", "velocity RMS")):
        allowed, predicted, measured = env[f"{key}_allowed"], env[f"{key}_predicted"], r24[key]
        envelope[key] = {"predicted": predicted, "allowed": allowed, "measured": measured,
                         "margin_abs": allowed - measured, "margin_rel": (allowed - measured) / allowed,
                         "measured_over_predicted": measured / predicted}
        gate(f"G5.1-D n=24 {label} <= 1.25 x predicted scheme error", measured <= allowed,
             f"measured {measured:.6e}, predicted {predicted:.6e}, allowed {allowed:.6e}, margin {allowed - measured:.3e} "
             f"({100 * (allowed - measured) / allowed:.1f} %)")
    # G5.1-E Richardson / GCI on CFDApp's 8 / 16 / 24 sequence
    gcis = {}
    for q, exact in (("G", frozen["analytical"]["G"]), ("u_axis", frozen["analytical"]["u_max"])):
        g = gci([x[n][q] for n in (8, 16, 24)], (8, 16, 24))
        g["exact"] = exact
        g["phi_ext_rel_error"] = abs(g["phi_ext"] - exact) / exact
        g["fine_rel_error"] = abs(x[24][q] - exact) / exact
        g["exact_inside_GCI_fine_band"] = g["fine_rel_error"] <= g["GCI_fine"]
        gcis[q] = g
        lo, hi = GCI_INDICATOR_BAND
        gate(f"G5.1-E GCI asymptotic indicator ({q}) in [{lo}, {hi}]", lo <= g["indicator"] <= hi,
             f"{g['indicator']:.4f} (p {g['p']:.4f}, phi_ext {g['phi_ext']:.8f}, exact {exact:.8f}, "
             f"GCI_fine {g['GCI_fine']:.3e}, GCI_coarse {g['GCI_coarse']:.3e}, exact inside GCI_fine band: "
             f"{'yes' if g['exact_inside_GCI_fine_band'] else 'no'})")
    # G5.2 (unchanged): RMS and dp/dx error decreasing -- the gtest level metrics, as originally measured
    for key in ("rms", "pressure_gradient_error"):
        seq = [gt[n][key] for n in (8, 16, 24)]
        gate(f"G5.2 (unchanged) {key} decreasing 8 -> 16 -> 24 (ProjectRunner level runs)", seq[0] > seq[1] > seq[2],
             " -> ".join(f"{v:.5e}" for v in seq))
    # G5.3 (unchanged): face-flux section mass flows and global imbalance
    for n in (8, 16, 24):
        gate(f"G5.3 (unchanged) n={n} every section within {MASS_TOL:g} of the inflow",
             gt[n]["max_section_flow_error"] <= MASS_TOL, f"{gt[n]['max_section_flow_error']:.3e}")
        rel = x[n]["conservation"].get("relative_imbalance", float("nan"))
        gate(f"G5.3 (unchanged) n={n} |m_in - m_out|/m_in <= {MASS_TOL:g}",
             gt[n]["global_imbalance"] <= MASS_TOL and rel <= MASS_TOL,
             f"{gt[n]['global_imbalance']:.3e} (level run); CLI metadata relative_imbalance {rel:.3e}")
    # G5.4 (unchanged): Converged from rest, finite, exported, Rhie-Chow -- both production runs
    for n in (8, 16, 24):
        r, g = x[n], gt[n]
        ok = (r["status"] == "Converged" and r["finite"] and r["exported"] and r["face_flux"] == "rhie_chow" and
              g["converged"] and g["finite"] and g["exported"] and g["face_flux"] == "rhie_chow")
        gate(f"G5.4 (unchanged) n={n} Converged from rest, finite, exported, rhie_chow", ok,
             f"CLI {r['status']}, {r['iterations']} it; level run {g['status']}, {g['iterations']} it")
    # consistency (reported): the two independent production runs of each level agree
    lines.append("  consistency (reported): level run Linf (C++, D399 reference) vs CLI Linf (Python, truth): " + "; ".join(
        f"n={n}: {gt[n]['linf']:.8e} / {x[n]['linf']:.8e}" for n in (8, 16, 24)))

    # G6.1 (unchanged): x / y / z n = 16, every cell
    xs = runs["x16"]
    prange = float(xs["p"].max() - xs["p"].min())
    sym = {}
    for other in ("y16", "z16"):
        o = runs[other]
        pair = f"x vs {other[0]}"
        diffs = {"streamwise": float(np.abs(xs["us"] - o["us"]).max()), "cross1": float(np.abs(xs["u1"] - o["u1"]).max()),
                 "cross2": float(np.abs(xs["u2"] - o["u2"]).max()),
                 "pressure_over_range": float(np.abs(xs["p"] - o["p"]).max()) / prange,
                 "G_rel": abs(res["x16"]["G"] - res[other]["G"]) / frozen["analytical"]["G"]}
        sym[pair] = diffs
        for k, v in diffs.items():
            gate(f"G6.1 (unchanged) {pair} {k} <= {G6_SYMMETRY_TOL:g}", v <= G6_SYMMETRY_TOL, f"{v:.3e}")
    for name in ("x16", "y16", "z16"):
        r = res[name]
        gate(f"G6 (unchanged) {name[0]}-duct Converged, finite, rhie_chow",
             r["status"] == "Converged" and r["finite"] and r["face_flux"] == "rhie_chow", f"{r['status']}, {r['iterations']} it")
    # G6.2-A3: the z-directed duct against the independent discrete solution (w streamwise)
    z = res["z16"]
    gate(f"G6.2-A3 z-duct n=16 max|w - u_disc|/U <= {DISCRETE_VELOCITY_TOL:g}", z["disc_max_abs_du"] <= DISCRETE_VELOCITY_TOL,
         f"{z['disc_max_abs_du']:.3e}")
    gate(f"G6.2-A3 z-duct n=16 max cross-flow (u, v)/U <= {DISCRETE_VELOCITY_TOL:g}", z["cross_flow_max"] <= DISCRETE_VELOCITY_TOL,
         f"{z['cross_flow_max']:.3e}")
    y = res["y16"]
    lines.append(f"  reported: y-duct n=16 max|v - u_disc| {y['disc_max_abs_du']:.3e}, cross-flow {y['cross_flow_max']:.3e}, "
                 f"dG/G_disc {y['G_rel_diff_disc']:.3e}; z-duct dG/G_disc {z['G_rel_diff_disc']:.3e}")

    failed = [i for i in items if not i["pass"]]
    lines.append("")
    lines.append(f"DECISION: {'A3 G5 PASS, A3 G6 PASS' if not failed else 'FAILED A3 GATE'} "
                 f"({len(items) - len(failed)}/{len(items)} items pass)")
    out = pathlib.Path(sys.argv[1])
    out.with_suffix(".txt").write_text("\n".join(lines) + "\n")
    out.with_suffix(".json").write_text(json.dumps(
        {"frozen_sha256": frozen_hash, "runs": res, "gtest_levels": gt, "orders": orders, "envelope_n24": envelope,
         "gci": gcis, "symmetry": sym, "items": items, "passed": not failed}, indent=1, default=float) + "\n")
    print("\n".join(lines))
    sys.exit(0 if not failed else 1)


if __name__ == "__main__":
    main()
