#!/usr/bin/env python3
"""P12-GRAD-002 A2 -- evaluate C10 / C11(a) from two a2_c10 dumps (NEW library, OLD library).

For every mesh|field present in both dumps:
  interior cells (layer >= 2):
    aligned mesh (no skewed interior face): count of cells whose production gradient is not
      BITWISE identical (C10 first clause: must be 0);
    skewed mesh: max |dg4| / scale (the frozen C10 second clause compares this with 1e-13);
      the cells beyond the propagation depth (layer > K + 1 = 5) must be bitwise identical;
      the sweep-coupling bound  |dg4_P| <= (1/V_P) sum_f w_f max(|dg3_P|, |dg3_N|) + floor
      must hold for every interior cell (w_f = |S_f||skew_f|, t_f in [0,1] checked).
  all cells, aligned-exact meshes (label 'aligned ...'): max |dg4| / scale (C11(a): <= 1e-13),
    and the A1 floor eps * 8 * max|phi| * max(sum|S|/V) / scale beside it.
scale = max |g4_old| over the mesh (the field's gradient scale).
"""
import math
import sys
from collections import defaultdict

EPS = 2.220446049250313e-16
K = 4


def parse(path):
    data = {}
    cur = None
    with open(path) as fh:
        for line in fh:
            parts = line.split()
            if not parts:
                continue
            if parts[0] == "MESH":
                rest = line[5:].rstrip("\n")
                # label may contain spaces: "MESH <label>|<field> n dim skewed phiMax cond"
                fields = rest.rsplit(" ", 5)
                key = fields[0]
                cur = {
                    "n": int(fields[1]), "dim": int(fields[2]), "skewed": int(fields[3]),
                    "phiMax": float.fromhex(fields[4]), "cond": float.fromhex(fields[5]),
                    "cells": {}, "faces": defaultdict(list),
                }
                data[key] = cur
            elif parts[0] == "C":
                p = int(parts[1])
                v = [float.fromhex(x) for x in parts[3:]]
                cur["cells"][p] = {"layer": int(parts[2]), "vol": v[0], "g3": v[1:4], "g4": v[4:7],
                                   "g4hex": parts[7:10]}
            elif parts[0] == "F":
                cur["faces"][int(parts[1])].append(
                    (int(parts[2]), float.fromhex(parts[3]), float.fromhex(parts[4])))
    return data


def norm(a):
    return math.sqrt(sum(x * x for x in a))


def sub(a, b):
    return [x - y for x, y in zip(a, b)]


def mutate(data):
    """Instrument self-test: in every skewed mesh, perturb one cell beyond the propagation depth by
    one ulp and one layer-2 cell by 1e-3 of the gradient scale; both must be flagged."""
    for m in data.values():
        if m["skewed"] == 0:
            continue
        deep = [p for p, c in m["cells"].items() if c["layer"] > K + 1]
        shallow = [p for p, c in m["cells"].items() if c["layer"] == 2]
        scale = max(norm(c["g4"]) for c in m["cells"].values())
        if deep:
            c = m["cells"][deep[0]]
            c["g4"][0] = math.nextafter(c["g4"][0], math.inf)
            c["g4hex"] = [float.hex(v) for v in c["g4"]]
        if shallow:
            c = m["cells"][shallow[0]]
            c["g4"][0] += 1e-3 * scale
            c["g4hex"] = [float.hex(v) for v in c["g4"]]


def main():
    new, old = parse(sys.argv[1]), parse(sys.argv[2])
    if len(sys.argv) > 3 and sys.argv[3] == "--mutate-new":
        mutate(new)
        print("# INSTRUMENT SELF-TEST: NEW mutated (deep cell +1 ulp, layer-2 cell +1e-3 scale)")
    print(f"# C10/C11(a): NEW={sys.argv[1]}  OLD={sys.argv[2]}")
    bad = 0
    for key in new:
        if key not in old:
            print(f"SKIP {key}: absent from OLD")
            continue
        a, b = new[key], old[key]
        if a["n"] != b["n"] or a["skewed"] != b["skewed"]:
            print(f"MISMATCH {key}: mesh differs between libraries (n {a['n']}/{b['n']}, skewed {a['skewed']}/{b['skewed']})")
            bad += 1
            continue
        scale = max(norm(c["g4"]) for c in b["cells"].values())
        floor = EPS * 8.0 * max(a["phiMax"], b["phiMax"]) * max(a["cond"], b["cond"])
        interior_diff = 0
        interior_max = 0.0
        boundary_max = 0.0
        all_max = 0.0
        deep_diff = 0
        deep_cells = 0
        per_layer = defaultdict(float)
        bound_viol = 0
        bound_worst = 0.0
        t_bad = 0
        for p, cn in a["cells"].items():
            co = b["cells"][p]
            d4 = norm(sub(cn["g4"], co["g4"]))
            all_max = max(all_max, d4)
            per_layer[cn["layer"]] = max(per_layer[cn["layer"]], d4)
            if cn["layer"] == 1:
                boundary_max = max(boundary_max, d4)
                continue
            if cn["g4hex"] != co["g4hex"]:
                interior_diff += 1
            interior_max = max(interior_max, d4)
            if cn["layer"] > K + 1:
                deep_cells += 1
                if cn["g4hex"] != co["g4hex"]:
                    deep_diff += 1
            rhs = 0.0
            dg3p = norm(sub(cn["g3"], co["g3"]))
            for q, w, t in a["faces"].get(p, []):
                if not (0.0 <= t <= 1.0):
                    t_bad += 1
                dg3q = norm(sub(a["cells"][q]["g3"], b["cells"][q]["g3"]))
                rhs += w * max(dg3p, dg3q)
            rhs = rhs / cn["vol"] + floor
            bound_worst = max(bound_worst, d4 / rhs)
            if d4 > rhs:
                bound_viol += 1
        aligned = a["skewed"] == 0
        line = (f"{key:48s} {'ALIGNED' if aligned else 'skewed ':7s} skewedFaces {a['skewed']:5d} | "
                f"interior: non-bitwise {interior_diff:5d} max {interior_max / scale:.3e} | "
                f"boundary max {boundary_max / scale:.3e}")
        if aligned:
            verdict = "C10a PASS" if interior_diff == 0 else "C10a FAIL"
            if interior_diff:
                bad += 1
        else:
            layers = " ".join(f"L{k}:{per_layer[k] / scale:.1e}" for k in sorted(per_layer) if k <= 7)
            verdict = (f"C10b(1e-13) {'PASS' if interior_max / scale <= 1e-13 else 'FAIL'} | depth>{K + 1}: "
                       f"{deep_diff}/{deep_cells} differ | coupling bound: worst {bound_worst:.3f} "
                       f"violations {bound_viol} t-out-of-range {t_bad} | {layers}")
        print(line + " | " + verdict)
        if key.startswith("aligned"):
            print(f"{'':48s} C11a all cells max {all_max / scale:.3e} (bound 1e-13: "
                  f"{'PASS' if all_max / scale <= 1e-13 else 'FAIL'}) floor/scale {floor / scale:.3e}")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
