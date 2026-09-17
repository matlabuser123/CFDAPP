#!/usr/bin/env python3
"""G5 investigation, items 5-8, 10: refinement tables, observed orders, Richardson/GCI,
required resolution, grid-location decomposition and truncation error.

Usage: analysis.py <cfdapp_comparison.json> <out.json> <out.md>

GCI: Celik, Ghia, Roache, Freitas, Coleman & Raad (2008), "Procedure for estimation and reporting of
uncertainty due to discretization in CFD applications", J. Fluids Eng. 130, 078001. Three grids
h1 < h2 < h3, r21 = h2/h1, r32 = h3/h2, apparent order p from the fixed-point iteration
p = |ln|e32/e21| + q(p)| / ln r21 with q(p) = ln((r21^p - s)/(r32^p - s)), s = sign(e32/e21);
phi_ext = (r21^p phi1 - phi2)/(r21^p - 1); GCI_fine = 1.25 |(phi1 - phi2)/phi1| / (r21^p - 1),
GCI_coarse = 1.25 |(phi2 - phi3)/phi2| / (r32^p - 1). The asymptotic-range indicator is
GCI_coarse / (r21^p GCI_fine), which is about 1 in the asymptotic range.
"""
import json
import math
import pathlib
import sys

import numpy as np

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import duct_discrete as disc  # noqa: E402
import duct_reference as ref  # noqa: E402

LIMITS = {"linf_020": ("linf", 0.020), "linf_010": ("linf", 0.010), "rms_005": ("rms", 0.005)}


def order(e1, e2, n1, n2):
    """Observed order between a coarse (n1) and a fine (n2) grid: ln(e1/e2)/ln(n2/n1)."""
    return math.log(e1 / e2) / math.log(n2 / n1)


def gci(phi, ns):
    """Celik et al. (2008). phi, ns ordered coarse -> fine (3 values)."""
    (p3, p2, p1), (n3, n2, n1) = phi, ns
    r21, r32 = n1 / n2, n2 / n3
    e21, e32 = p2 - p1, p3 - p2
    s = math.copysign(1.0, e32 / e21)
    p = math.log(abs(e32 / e21)) / math.log(r21)
    for _ in range(200):
        q = math.log((r21 ** p - s) / (r32 ** p - s))
        p_new = abs(math.log(abs(e32 / e21)) + q) / math.log(r21)
        if abs(p_new - p) < 1e-14:
            p = p_new
            break
        p = p_new
    ext = (r21 ** p * p1 - p2) / (r21 ** p - 1)
    ea21, ea32 = abs((p1 - p2) / p1), abs((p2 - p3) / p2)
    gci_f = 1.25 * ea21 / (r21 ** p - 1)
    gci_c = 1.25 * ea32 / (r32 ** p - 1)
    return {"grids": list(ns), "phi": list(phi), "r21": r21, "r32": r32, "p": p, "phi_ext": ext,
            "e_a21": ea21, "e_ext21": abs((ext - p1) / ext), "GCI_fine": gci_f, "GCI_coarse": gci_c,
            "asymptotic_indicator": gci_c / (r21 ** p * gci_f), "oscillatory": s < 0}


def main():
    cmp_json, out_json, out_md = sys.argv[1:4]
    cfd = json.loads(pathlib.Path(cmp_json).read_text())
    ns = [8, 12, 16, 24, 32, 48, 64, 96, 128, 192, 256]
    rows = disc.run(ns, check_sparse_up_to=0)
    md, res = [], {"discrete": rows}
    w = md.append

    # --- 5. CFDApp vs analytical, independent discrete vs analytical, CFDApp vs discrete ---------
    w("### CFDApp vs analytical, discrete vs analytical, CFDApp vs discrete (measurement plane x = 4a - h/2)\n")
    w("| n | source | L∞ (point) | RMS = L2 (point) | L1 (point) | u_max err | dp/dx (G) err | mean u (plane) | mass flow in / out |")
    w("| --- | --- | --- | --- | --- | --- | --- | --- | --- |")
    cfd_ns = sorted(int(k[1:]) for k in cfd if k.startswith("x") and k[1:].isdigit())
    for n in ns:
        r = rows[n]
        if n in cfd_ns:
            c = cfd[f"x{n}"]
            mp = c["measurement_plane"]
            cons = c["conservation"]
            w(f"| {n} | CFDApp | {mp['point']['linf']:.5e} | {mp['point']['rms']:.5e} | {mp['point']['l1']:.5e} | "
              f"{mp['u_axis_rel_error']:.5e} | {c['G_rel_error']:.5e} | {c['mean_cell_velocity']['measurement_plane']:.10f} | "
              f"{cons['inflow']:.12f} / {cons['outflow']:.12f} |")
        w(f"| {n} | discrete | {r['point']['linf']:.5e} | {r['point']['rms']:.5e} | {r['point']['l1']:.5e} | "
          f"{r['u_axis_rel_error']:.5e} | {r['G_rel_error']:.5e} | {r['mean_velocity']:.10f} | 1 (imposed) |")
        if n in cfd_ns:
            vd = cfd[f"x{n}"]["vs_discrete"]
            w(f"| {n} | CFDApp − discrete | max\\|Δu\\| {vd['max_abs_du_measurement_plane']:.2e} (downstream plane "
              f"{vd['max_abs_du_downstream_plane']:.2e}; planes x∈[3,4.5] {vd['max_abs_du_developed_planes_3_to_4.5']:.2e}) | | | "
              f"Δu_max {vd['u_axis_rel_diff']:.2e} | ΔG {vd['G_rel_diff']:.2e} | | |")
    w("")

    # --- 6. observed orders ---------------------------------------------------------------------
    def pair_orders(get, seq):
        return [(seq[i], seq[i + 1], order(get(seq[i]), get(seq[i + 1]), seq[i], seq[i + 1])) for i in range(len(seq) - 1)]

    metrics = {
        "Linf point": lambda r: r["point"]["linf"], "RMS point": lambda r: r["point"]["rms"],
        "L1 point": lambda r: r["point"]["l1"], "Linf average": lambda r: r["average"]["linf"],
        "RMS average": lambda r: r["average"]["rms"], "G error": lambda r: r["G_rel_error"],
        "u_max error": lambda r: r["u_axis_rel_error"],
    }
    res["orders_discrete"] = {k: pair_orders(lambda n, f=f: f(rows[n]), ns) for k, f in metrics.items()}
    cfd_metrics = {
        "Linf point": lambda c: c["measurement_plane"]["point"]["linf"],
        "RMS point": lambda c: c["measurement_plane"]["point"]["rms"],
        "L1 point": lambda c: c["measurement_plane"]["point"]["l1"],
        "Linf average": lambda c: c["measurement_plane"]["average"]["linf"],
        "RMS average": lambda c: c["measurement_plane"]["average"]["rms"],
        "G error": lambda c: c["G_rel_error"], "u_max error": lambda c: c["measurement_plane"]["u_axis_rel_error"],
    }
    res["orders_cfdapp"] = {k: pair_orders(lambda n, f=f: f(cfd[f"x{n}"]), cfd_ns) for k, f in cfd_metrics.items()}
    w("### Observed order p = ln(E_coarse/E_fine) / ln(n_fine/n_coarse)\n")
    head = " | ".join(f"{a}→{b}" for a, b, _ in res["orders_discrete"]["Linf point"])
    w(f"| discrete | {head} |")
    w("| --- |" + " --- |" * len(res["orders_discrete"]["Linf point"]))
    for k, v in res["orders_discrete"].items():
        w(f"| {k} | " + " | ".join(f"{p:.3f}" for _, _, p in v) + " |")
    w("")
    head = " | ".join(f"{a}→{b}" for a, b, _ in res["orders_cfdapp"]["Linf point"])
    w(f"| CFDApp | {head} |")
    w("| --- |" + " --- |" * len(res["orders_cfdapp"]["Linf point"]))
    for k, v in res["orders_cfdapp"].items():
        w(f"| {k} | " + " | ".join(f"{p:.3f}" for _, _, p in v) + " |")
    w("")

    # --- 7. Richardson / GCI --------------------------------------------------------------------
    studies = {}
    for label, seq in (("CFDApp 8/16/24 (gate grids)", (8, 16, 24)), ("CFDApp 8/16/32", (8, 16, 32)),
                       ("CFDApp 16/24/32", (16, 24, 32)), ("discrete 8/16/24", (8, 16, 24)),
                       ("discrete 16/32/64", (16, 32, 64)), ("discrete 32/64/128", (32, 64, 128)),
                       ("discrete 64/128/256", (64, 128, 256))):
        src = "cfd" if label.startswith("CFDApp") else "disc"
        if src == "cfd" and not all(n in cfd_ns for n in seq):
            continue
        for q in ("G", "u_axis"):
            if src == "cfd":
                phi = [cfd[f"x{n}"]["G"] if q == "G" else cfd[f"x{n}"]["measurement_plane"]["u_axis"] for n in seq]
            else:
                phi = [rows[n]["G"] if q == "G" else rows[n]["u_axis"] for n in seq]
            g = gci(phi, seq)
            exact = ref.G_EXACT if q == "G" else ref.UMAX_S
            g["exact"] = exact
            g["phi_ext_true_rel_error"] = abs(g["phi_ext"] - exact) / exact
            g["fine_true_rel_error"] = abs(phi[-1] - exact) / exact
            g["exact_within_GCI_fine_band"] = g["fine_true_rel_error"] <= g["GCI_fine"]
            studies[f"{label} {q}"] = g
    res["gci"] = studies
    w("### Richardson / GCI (Celik et al. 2008; F_s = 1.25)\n")
    w("| study | quantity | p | φ_ext | true err of φ_ext | fine-grid true err | GCI_fine | GCI_coarse | asymptotic indicator | exact inside GCI_fine |")
    w("| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |")
    for key, g in studies.items():
        label, q = key.rsplit(" ", 1)
        w(f"| {label} | {q} | {g['p']:.3f} | {g['phi_ext']:.8f} | {g['phi_ext_true_rel_error']:.2e} | "
          f"{g['fine_true_rel_error']:.3e} | {g['GCI_fine']:.3e} | {g['GCI_coarse']:.3e} | {g['asymptotic_indicator']:.4f} | "
          f"{'yes' if g['exact_within_GCI_fine_band'] else 'no'} |")
    w("")

    # --- 8. required resolution (exact discrete errors at every n) ------------------------------
    scan = disc.run(list(range(8, 49)), check_sparse_up_to=0)
    res["scan"] = {n: {"point": {"linf": r["point"]["linf"], "rms": r["point"]["rms"]},
                       "average": {"linf": r["average"]["linf"], "rms": r["average"]["rms"]}} for n, r in scan.items()}
    need = {}
    for metric in ("point", "average"):
        for key, (norm, lim) in LIMITS.items():
            ok_all = [n for n, r in scan.items() if r[metric][norm] <= lim]
            ok_even = [n for n in ok_all if n % 2 == 0]
            need[f"{metric} {key}"] = {"smallest_n": min(ok_all), "smallest_even_n": min(ok_even),
                                       "value_at_smallest_even": scan[min(ok_even)][metric][norm]}
    # power-law fit on the asymptotic range (discrete, point metric, n >= 64)
    fit = {}
    for norm in ("linf", "rms"):
        x = np.log([1.0 / n for n in (64, 96, 128, 192, 256)])
        y = np.log([rows[n]["point"][norm] for n in (64, 96, 128, 192, 256)])
        p, logc = np.polyfit(x, y, 1)
        fit[norm] = {"p": float(p), "C": float(math.exp(logc))}
        for key, (nn, lim) in LIMITS.items():
            if nn == norm:
                fit[norm][f"n_for_{key}"] = float((math.exp(logc) / lim) ** (1.0 / p))
    res["required_resolution"] = need
    res["asymptotic_fit_point"] = fit
    w("### Resolution required for the original limits (exact discrete errors, every n from 8 to 48)\n")
    w("| limit | metric | smallest n | smallest even n (the gate's u_max needs even n) | value there |")
    w("| --- | --- | --- | --- | --- |")
    for key, v in need.items():
        metric, lim = key.split(" ", 1)
        w(f"| {lim} | {metric} | {v['smallest_n']} | {v['smallest_even_n']} | {v['value_at_smallest_even']:.5e} |")
    w("")
    w(f"Asymptotic power-law fit E = C h^p (discrete, point metric, n = 64…256): "
      f"L∞: p = {fit['linf']['p']:.4f}, C = {fit['linf']['C']:.4f}, n(L∞ ≤ 0.020) = {fit['linf']['n_for_linf_020']:.2f}, "
      f"n(L∞ ≤ 0.010) = {fit['linf']['n_for_linf_010']:.2f}; RMS: p = {fit['rms']['p']:.4f}, C = {fit['rms']['C']:.4f}, "
      f"n(RMS ≤ 0.005) = {fit['rms']['n_for_rms_005']:.2f}.\n")

    # --- 8. grid-location decomposition ----------------------------------------------------------
    w("### Grid location: discrete solution vs analytical point value vs analytical cell average\n")
    w("| n | L∞ point | L∞ average | RMS point | RMS average | mean(u − point) | mean(u − average) | (h²/24)G/μ | L∞ location (cell; cells from wall) | L∞ wall ring (point) | L∞ interior (point) |")
    w("| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |")
    for n in ns:
        r = rows[n]
        p, a = r["point"], r["average"]
        h = 1.0 / n
        w(f"| {n} | {p['linf']:.5e} | {a['linf']:.5e} | {p['rms']:.5e} | {a['rms']:.5e} | {p['mean_error']:+.4e} | "
          f"{a['mean_error']:+.1e} | {h * h / 24 * ref.G_EXACT / ref.MU:.4e} | ({p['linf_cell'][0]},{p['linf_cell'][1]}); "
          f"{p['linf_cells_from_wall']} | {p['linf_wall_ring']:.4e} | {p['linf_interior']:.4e} |")
    w("")

    # --- 10. truncation error: wall ring, corner-adjacent, central half -------------------------
    trunc = {}
    for n in (16, 32, 64, 128, 256):
        h = 1.0 / n
        c = (np.arange(n) + 0.5) * h
        tau = disc.truncation(n, ref.u_single(c, c))
        ring = np.zeros((n, n), dtype=bool)
        ring[0, :] = ring[-1, :] = ring[:, 0] = ring[:, -1] = True
        central = np.zeros((n, n), dtype=bool)
        central[n // 4: 3 * n // 4, n // 4: 3 * n // 4] = True
        second = np.zeros((n, n), dtype=bool)
        second[1:-1, 1:-1] = True
        second[2:-2, 2:-2] = False
        edge_mid = abs(tau[0, n // 2])
        trunc[n] = {"wall_ring_max": float(np.abs(tau[ring]).max()), "wall_mid_edge": float(edge_mid),
                    "second_ring_max": float(np.abs(tau[second]).max()),
                    "central_half_max": float(np.abs(tau[central]).max()),
                    "G_over_4": ref.G_EXACT / 4}
    res["truncation_point"] = trunc
    w("### Local truncation error τ of the discrete scheme with the exact solution inserted (per unit volume)\n")
    w("| n | wall ring max | wall cell at mid-edge | second ring max | central half max |")
    w("| --- | --- | --- | --- | --- |")
    for n, t in trunc.items():
        w(f"| {n} | {t['wall_ring_max']:.4e} | {t['wall_mid_edge']:.4e} | {t['second_ring_max']:.4e} | {t['central_half_max']:.4e} |")
    w("")

    # --- 4. norm cross-check: independent Python vs the gate's C++ -------------------------------
    w("### Norm cross-check: independent Python norms of the CLI solution vs the gate's C++ numbers\n")
    w("| n | iterations CLI / gate | L∞ Python (truth) | L∞ gate C++ | rel. diff, same reference (D399) | RMS Python (truth) | RMS gate C++ | rel. diff, same reference | dp/dx rel. diff | u_max rel. diff |")
    w("| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |")
    for n in cfd_ns:
        gc = cfd[f"x{n}"].get("vs_gate_cpp")
        if gc:
            w(f"| {n} | {cfd[f'x{n}']['iterations']} / {gc['iterations_gate']} | {gc['linf_python']:.8e} | {gc['linf_gate']:.8e} | "
              f"{gc['linf_rel_diff_D399']:.1e} | {gc['rms_python']:.8e} | {gc['rms_gate']:.8e} | {gc['rms_rel_diff_D399']:.1e} | "
              f"{gc['dpdx_rel_diff']:.1e} | {gc['umax_rel_diff']:.1e} |")
    w("")

    # --- 9. directional symmetry from the CLI outputs --------------------------------------------
    for key in [k for k in cfd if k.startswith("symmetry_n")]:
        w(f"### Directional symmetry, {key[len('symmetry_'):]} (independent Python, every cell of the three CLI runs)\n")
        w("| pair | max \\|Δ streamwise\\|/U | max \\|Δ cross 1\\|/U | max \\|Δ cross 2\\|/U | max \\|Δp\\|/(p_max − p_min) | \\|ΔG\\|/G |")
        w("| --- | --- | --- | --- | --- | --- |")
        for pair, v in cfd[key].items():
            w(f"| {pair} | {v['streamwise']:.3e} | {v['cross1']:.3e} | {v['cross2']:.3e} | {v['pressure_over_range']:.3e} | {v['G_rel']:.3e} |")
        w("")
        n = int(key[len("symmetry_n"):])
        w("| duct | L∞ (point) | RMS (point) | dp/ds error | u_max error | CFDApp − discrete max\\|Δu\\| |")
        w("| --- | --- | --- | --- | --- | --- |")
        for a in "xyz":
            c = cfd.get(f"{a}{n}")
            if c:
                mp = c["measurement_plane"]
                w(f"| {a} | {mp['point']['linf']:.8e} | {mp['point']['rms']:.8e} | {c['G_rel_error']:.8e} | "
                  f"{mp['u_axis_rel_error']:.8e} | {c['vs_discrete']['max_abs_du_measurement_plane']:.2e} |")
        w("")

    # --- 10. implementation-audit experiments ---------------------------------------------------
    audits = {k: v for k, v in cfd.items() if k.startswith("audit_")}
    if audits:
        w("### Implementation-audit experiments (CLI; measurement plane vs the base run)\n")
        w("| variant vs base | iterations | max \\|Δu\\|/U | max \\|Δ cross-flow\\|/U | \\|ΔG\\|/G | L∞ variant / base |")
        w("| --- | --- | --- | --- | --- | --- |")
        for k, v in audits.items():
            w(f"| {k[len('audit_'):].replace('_vs_', ' vs ')} | {v['iterations'][0]} / {v['iterations'][1]} | "
              f"{v['max_abs_du_measurement_plane']:.2e} | {v['max_abs_dcross_measurement_plane']:.2e} | {v['G_rel_diff']:.2e} | "
              f"{v['linf_point'][0]:.10f} / {v['linf_point'][1]:.10f} |")
        w("")

    pathlib.Path(out_json).write_text(json.dumps(res, indent=2, default=float) + "\n")
    pathlib.Path(out_md).write_text("\n".join(md) + "\n")
    print("\n".join(md))


if __name__ == "__main__":
    main()
