#!/usr/bin/env python3
"""P12-GRAD-002 Amendment A3 -- C10-A3(d): non-vacuity of the C10-A2(b) instrument, by construction.

usage: selftest_c10.py <NEW dump> <OLD dump>

For every SKEWED mesh|field of the two a2_c10 dumps (compare_c10.py's parser and evaluation are
imported unchanged):
  control  the unmutated comparison: 0 depth differences beyond layer K+1 and 0 bound violations
           (this is C10-A2(b) itself);
  deep     if the mesh has cells of layer >= K+2: move the first such cell's NEW gradient by one ulp;
           the depth rule must report exactly 1 differing deep cell. Meshes without such cells:
           N/A (reported; C10-A2(b)(i) is vacuous there);
  bound    move the first layer-2 cell's NEW gradient (x component) by 3 * (its own coupling bound +
           floor), computed from the unmutated data. Because |dg4| <= bound + floor before the
           mutation (control), afterwards |dg4| >= 3 (bound + floor) - (bound + floor)
           = 2 (bound + floor) > bound + floor: the evaluation must report exactly 1 violation.
The two mutations are applied to separate copies, so each is attributed to its own check.
Exit status 0 only if every applicable check behaves as stated.
"""
import copy
import importlib.util
import math
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
spec = importlib.util.spec_from_file_location(
    "c10", os.path.join(HERE, "..", "..", "a2", "tools", "compare_c10.py"))
c10 = importlib.util.module_from_spec(spec)
spec.loader.exec_module(c10)
K = c10.K


def evaluate(a, b):
    """(deep differing, deep cells, bound violations) exactly as compare_c10.main computes them."""
    floor = c10.EPS * 8.0 * max(a["phiMax"], b["phiMax"]) * max(a["cond"], b["cond"])
    deep_diff = deep = viol = 0
    for p, cn in a["cells"].items():
        co = b["cells"][p]
        if cn["layer"] == 1:
            continue
        d4 = c10.norm(c10.sub(cn["g4"], co["g4"]))
        if cn["layer"] > K + 1:
            deep += 1
            deep_diff += cn["g4hex"] != co["g4hex"]
        rhs = rhs_of(a, b, p, floor)
        viol += d4 > rhs
    return deep_diff, deep, viol, floor


def rhs_of(a, b, p, floor):
    cn, co = a["cells"][p], b["cells"][p]
    dg3p = c10.norm(c10.sub(cn["g3"], co["g3"]))
    rhs = 0.0
    for q, w, _t in a["faces"].get(p, []):
        dg3q = c10.norm(c10.sub(a["cells"][q]["g3"], b["cells"][q]["g3"]))
        rhs += w * max(dg3p, dg3q)
    return rhs / cn["vol"] + floor


def main():
    new, old = c10.parse(sys.argv[1]), c10.parse(sys.argv[2])
    bad = 0
    for key in new:
        if key not in old or new[key]["skewed"] == 0:
            continue
        a, b = new[key], old[key]
        d0, deep, v0, floor = evaluate(a, b)
        control_ok = d0 == 0 and v0 == 0
        # deep mutation
        deep_cells = sorted(p for p, c in a["cells"].items() if c["layer"] > K + 1)
        if deep_cells:
            am = copy.deepcopy(a)
            c = am["cells"][deep_cells[0]]
            c["g4"][0] = math.nextafter(c["g4"][0], math.inf)
            c["g4hex"] = [float.hex(v) for v in c["g4"]]
            dd, _, _, _ = evaluate(am, b)
            deep_res = "flagged" if dd == 1 else f"NOT FLAGGED ({dd})"
            deep_ok = dd == 1
        else:
            deep_res, deep_ok = "N/A (no layer >= %d cells)" % (K + 2), True
        # bound mutation
        layer2 = sorted(p for p, c in a["cells"].items() if c["layer"] == 2)
        if layer2:
            p = layer2[0]
            shift = 3.0 * rhs_of(a, b, p, floor)
            am = copy.deepcopy(a)
            c = am["cells"][p]
            c["g4"][0] += shift
            c["g4hex"] = [float.hex(v) for v in c["g4"]]
            _, _, vv, _ = evaluate(am, b)
            bound_res = f"shift {shift:.3e}: " + ("flagged" if vv == 1 else f"NOT FLAGGED ({vv})")
            bound_ok = vv == 1
        else:
            bound_res, bound_ok = "N/A (no layer-2 cells)", False
        ok = control_ok and deep_ok and bound_ok
        bad += not ok
        print(f"{key:48s} control {'ok' if control_ok else 'FAILED'} | deep: {deep_res} | bound: {bound_res}"
              f" -> {'PASS' if ok else 'FAIL'}")
    print(f"C10-A3(d): {'PASS' if bad == 0 else 'FAIL'} ({bad} failing mesh|field rows)")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
