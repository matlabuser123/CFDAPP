#!/usr/bin/env python3
"""G5 investigation, items 4, 5, 9: CFDApp's production CLI output vs the gate's C++ numbers, the
analytical truth and the independent discrete solution. Independent Python norms throughout.

Usage: compare_cfdapp.py <out.json> <name> [<name> ...]
Each name is a CLI run under $HOME/m6g5/<name> (tools/run_cli_ducts.sh). The name is x<n>, y<n>
or z<n> for the flow axis and n; the gate data for the x-ducts n = 8, 16, 24 is
results/p12-mesh-006/data/duct3d_n<n>.json.

From results/fields.csv (17 significant digits) each run is mapped to canonical (s, c1, c2)
coordinates: s along the flow axis, c1 = (axis + 1) % 3, c2 = (axis + 2) % 3, as in the gate's
runDuct(). Cell indices come from floor(coordinate / h), independent of the cell ids.
"""
import csv
import json
import os
import pathlib
import sys

import numpy as np

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import duct_discrete as disc  # noqa: E402
import duct_reference as ref  # noqa: E402

HERE = pathlib.Path(__file__).resolve().parent
GATE_DATA = HERE.parents[1] / "data"
RUNS = pathlib.Path(os.path.expanduser("~/m6g5"))


def case_info(name):
    """Flow axis, cross-section cells n and flow-axis cells ns of a run, from its own case files."""
    geo = json.loads((RUNS / name / "geometry.json").read_text())
    mesh = json.loads((RUNS / name / "mesh.json").read_text())
    lengths = [geo["length"], geo["height"], geo["depth"]]
    cells = [mesh["nx"], mesh["ny"], mesh["nz"]]
    axis = int(np.argmax(lengths))
    return axis, cells[(axis + 1) % 3], cells[axis]


def load(name):
    axis, n, ns = case_info(name)
    h = 1.0 / n
    a1, a2 = (axis + 1) % 3, (axis + 2) % 3
    us = np.full((ns, n, n), np.nan)
    u1, u2, p = us.copy(), us.copy(), us.copy()
    coords = {"min": [9.0] * 3, "max": [-9.0] * 3}
    with open(RUNS / name / "results" / "fields.csv") as fh:
        rows = csv.reader(fh)
        header = next(rows)
        col = {k: i for i, k in enumerate(header)}
        for r in rows:
            x = [float(r[col["x"]]), float(r[col["y"]]), float(r[col["z"]])]
            v = [float(r[col["velocity_x"]]), float(r[col["velocity_y"]]), float(r[col["velocity_z"]])]
            ijk = [int(np.floor(x[c] / h)) for c in range(3)]
            for c in range(3):
                coords["min"][c] = min(coords["min"][c], x[c])
                coords["max"][c] = max(coords["max"][c], x[c])
            idx = (ijk[axis], ijk[a1], ijk[a2])
            us[idx], u1[idx], u2[idx], p[idx] = v[axis], v[a1], v[a2], float(r[col["pressure"]])
    assert not np.isnan(us).any(), f"{name}: unmapped cells"
    meta = json.loads((RUNS / name / "results" / "metadata.json").read_text())
    return {"name": name, "axis": axis, "n": n, "ns": ns, "h": h, "us": us, "u1": u1, "u2": u2, "p": p,
            "coords": coords, "meta": meta}


def plane_metrics(d, is_):
    n, h = d["n"], d["h"]
    c = (np.arange(n) + 0.5) * h
    u = d["us"][is_]
    point = ref.u_single(c, c)
    avg = ref.cell_averages(n)
    m = n // 2
    axis_u = 0.25 * (u[m - 1, m - 1] + u[m, m - 1] + u[m - 1, m] + u[m, m])
    return {
        "point": disc.metrics(u, point, n),
        "average": disc.metrics(u, avg, n),
        "u_axis": float(axis_u),
        "u_axis_rel_error": abs(axis_u - ref.UMAX_S) / ref.UMAX_S,
        "cross_flow_max": float(max(np.abs(d["u1"][is_]).max(), np.abs(d["u2"][is_]).max())),
        "mean_velocity": float(u.mean()),
    }


def gradient(d):
    n, h = d["n"], d["h"]
    s = (np.arange(d["ns"]) + 0.5) * h
    sel = (s >= 2.5) & (s <= 4.5)
    means = d["p"].reshape(d["ns"], -1).mean(axis=1)
    slope = np.polyfit(s[sel], means[sel], 1)[0]
    return -slope


def analyse(name):
    d = load(name)
    n = d["n"]
    plane = 4 * n - 1  # x = 4a - h/2 (Amendment A2.2)
    u_disc, g_disc = disc.discrete(n)
    res = {"name": name, "n": n, "axis": "xyz"[d["axis"]],
           "iterations": d["meta"].get("solver", {}).get("iterations", d["meta"].get("iterations")),
           "status": d["meta"].get("solver", {}).get("status", d["meta"].get("status")),
           "face_flux": d["meta"].get("solver", {}).get("face_flux"),
           "centroid_extent": d["coords"],
           "measurement_plane": plane_metrics(d, plane),
           "downstream_plane": plane_metrics(d, plane + 1)}
    g = gradient(d)
    res["G"] = g
    res["G_rel_error"] = abs(g - ref.G_EXACT) / ref.G_EXACT
    plane_means = d["us"].reshape(d["ns"], -1).mean(axis=1)
    s = (np.arange(d["ns"]) + 0.5) * d["h"]
    developed = plane_means[(s >= 3.0) & (s <= 4.5)]
    res["mean_cell_velocity"] = {
        "measurement_plane": float(plane_means[plane]),
        "developed_planes_3_to_4.5_min": float(developed.min()),
        "developed_planes_3_to_4.5_max": float(developed.max()),
        "first_plane": float(plane_means[0]),
        "last_plane": float(plane_means[-1]),
        "note": "mean of cell-centre streamwise velocity per cell plane; the section MASS flow is the face-flux sum "
                "(gate G5.3, metadata conservation)",
    }
    # the same norms against the gate's own reference: double series m, n <= 399 with G from the
    # truncated K of the same series (as DuctExact in the C++ test) -- isolates the norm arithmetic
    c = (np.arange(n) + 0.5) * d["h"]
    g399 = ref.U * ref.MU / ref.k_double()
    res["measurement_plane_vs_D399"] = disc.metrics(d["us"][plane], ref.u_double(c, c, g=g399), n)
    res["conservation"] = d["meta"].get("conservation", {})
    # vs the independent discrete solution (same n)
    res["vs_discrete"] = {
        "max_abs_du_measurement_plane": float(np.abs(d["us"][plane] - u_disc).max()),
        "max_abs_du_downstream_plane": float(np.abs(d["us"][plane + 1] - u_disc).max()),
        "max_abs_du_developed_planes_3_to_4.5": float(max(
            np.abs(d["us"][i] - u_disc).max() for i in range(d["ns"]) if 3.0 <= (i + 0.5) * d["h"] <= 4.5)),
        "G_rel_diff": abs(g - g_disc) / g_disc,
        "u_axis_rel_diff": abs(res["measurement_plane"]["u_axis"] - 0.25 * (
            u_disc[n // 2 - 1, n // 2 - 1] + u_disc[n // 2, n // 2 - 1] + u_disc[n // 2 - 1, n // 2] + u_disc[n // 2, n // 2]))
        / ref.UMAX_S,
    }
    # vs the gate's C++ measurement (x-ducts at the gate grids)
    gate = GATE_DATA / f"duct3d_n{n}.json"
    if name == f"x{n}" and gate.exists():
        gj = json.loads(gate.read_text())
        mp = res["measurement_plane"]
        d399 = res["measurement_plane_vs_D399"]
        res["vs_gate_cpp"] = {
            "iterations_gate": gj["iterations"],
            "linf_gate": gj["linf"], "linf_python": mp["point"]["linf"],
            "linf_rel_diff": abs(gj["linf"] - mp["point"]["linf"]) / gj["linf"],
            "rms_gate": gj["rms"], "rms_python": mp["point"]["rms"],
            "rms_rel_diff": abs(gj["rms"] - mp["point"]["rms"]) / gj["rms"],
            "linf_python_D399": d399["linf"], "linf_rel_diff_D399": abs(gj["linf"] - d399["linf"]) / gj["linf"],
            "rms_python_D399": d399["rms"], "rms_rel_diff_D399": abs(gj["rms"] - d399["rms"]) / gj["rms"],
            "downstream_linf_rel_diff": abs(gj["downstream_plane_linf"] - res["downstream_plane"]["point"]["linf"])
            / gj["downstream_plane_linf"],
            "dpdx_gate": gj["pressure_gradient"], "dpdx_python": g,
            "dpdx_rel_diff": abs(gj["pressure_gradient"] - g) / gj["pressure_gradient"],
            "umax_gate": gj["u_max"], "umax_python": mp["u_axis"],
            "umax_rel_diff": abs(gj["u_max"] - mp["u_axis"]) / gj["u_max"],
        }
    return res, d


def main():
    out = pathlib.Path(sys.argv[1])
    results, fields = {}, {}
    for name in sys.argv[2:]:
        results[name], fields[name] = analyse(name)
    # directional symmetry on every complete x/y/z set
    for n in sorted({r["n"] for r in results.values()}):
        names = [f"{a}{n}" for a in "xyz"]
        if all(k in fields for k in names):
            x = fields[names[0]]
            prange = float(x["p"].max() - x["p"].min())
            sym = {}
            for other in names[1:]:
                o = fields[other]
                sym[f"x vs {other[0]}"] = {
                    "streamwise": float(np.abs(x["us"] - o["us"]).max()),
                    "cross1": float(np.abs(x["u1"] - o["u1"]).max()),
                    "cross2": float(np.abs(x["u2"] - o["u2"]).max()),
                    "pressure_over_range": float(np.abs(x["p"] - o["p"]).max()) / prange,
                    "G_rel": abs(results[names[0]]["G"] - results[other]["G"]) / ref.G_EXACT,
                }
            results[f"symmetry_n{n}"] = sym
    # audit variants (a name with a suffix, e.g. x8relax) against their base run (x8)
    for name in list(fields):
        base = name[0] + "".join(ch for ch in name[1:] if ch.isdigit())
        if base != name and base in fields:
            a, b = fields[name], fields[base]
            n = a["n"]
            pa, pb = 4 * n - 1, 4 * n - 1
            results[f"audit_{name}_vs_{base}"] = {
                "max_abs_du_measurement_plane": float(np.abs(a["us"][pa] - b["us"][pb]).max()),
                "max_abs_dcross_measurement_plane": float(max(np.abs(a["u1"][pa] - b["u1"][pb]).max(),
                                                              np.abs(a["u2"][pa] - b["u2"][pb]).max())),
                "G_rel_diff": abs(results[name]["G"] - results[base]["G"]) / ref.G_EXACT,
                "linf_point": [results[name]["measurement_plane"]["point"]["linf"],
                               results[base]["measurement_plane"]["point"]["linf"]],
                "iterations": [results[name]["iterations"], results[base]["iterations"]],
            }
    out.write_text(json.dumps(results, indent=2, default=float) + "\n")
    for name, r in results.items():
        if name.startswith("symmetry") or name.startswith("audit"):
            print(name, json.dumps(r))
            continue
        mp, vd = r["measurement_plane"], r["vs_discrete"]
        line = (f"{name:4s} it {r['iterations']} | point Linf {mp['point']['linf']:.6e} RMS {mp['point']['rms']:.6e} "
                f"| avg Linf {mp['average']['linf']:.6e} RMS {mp['average']['rms']:.6e} | G err {r['G_rel_error']:.5e} "
                f"u_axis err {mp['u_axis_rel_error']:.5e} | vs discrete: |du| {vd['max_abs_du_measurement_plane']:.2e} "
                f"G {vd['G_rel_diff']:.2e}")
        if "vs_gate_cpp" in r:
            gc = r["vs_gate_cpp"]
            line += (f" | vs gate C++: it {gc['iterations_gate']}, Linf {gc['linf_rel_diff']:.1e} (D399 ref: "
                     f"{gc['linf_rel_diff_D399']:.1e}), RMS {gc['rms_rel_diff']:.1e} (D399: {gc['rms_rel_diff_D399']:.1e}), "
                     f"dp/dx {gc['dpdx_rel_diff']:.1e}, umax {gc['umax_rel_diff']:.1e}")
        print(line)


if __name__ == "__main__":
    main()
