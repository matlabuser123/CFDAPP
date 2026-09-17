#!/usr/bin/env python3
"""P12-GRAD-002 A2, C11(b): the frozen bound on ALIGNED-case field exports, cur vs nograd.

usage: c11b_fields.py <cur-root> <nograd-root> <compare_outputs.py log>

Every file the comparison classified VALUES is assigned a category by path:
  A  non-orthogonal / skewed / moving-mesh case: quantified by compare_outputs.py, explained in the
     summary; no bound here;
  B  aligned case.
For every B file that is a FIELD EXPORT (fields.csv, solution.vtk, and the profile/centerline CSVs:
any .csv except residuals.csv), each numeric column / VTK data array must satisfy
    max_i |cur_i - nograd_i|  <=  max(1e-6 * max_i |nograd_i|, u_i)
where u_i is one unit in the last printed digit of the entry (a printed value cannot resolve less).
Other B files (validation summaries, residual histories, metadata) are solver diagnostics and are
only listed. An uncategorized VALUES file fails.
"""
import os
import re
import sys
from decimal import Decimal, getcontext

getcontext().prec = 50

A_PATTERNS = [
    r"distorted", r"curved_channel", r"multiblock", r"structured_quad", r"sector", r"(^|[/_])ale([/_.]|$)",
]
B_PATTERNS = [
    r"^results/validation/", r"^tests/data/cases/", r"^cases/",
]
NUM = re.compile(r"^[-+]?(?:\d+\.\d*|\.\d+|\d+)(?:[eE][-+]?\d+)?$")


def category(path):
    low = path.lower()
    if any(re.search(p, low) for p in A_PATTERNS):
        return "A"
    if any(re.search(p, path) for p in B_PATTERNS):
        return "B"
    return "?"


def unit(token):
    """One unit in the last printed digit of a numeric token."""
    mant = token.lower().split("e")
    exp = int(mant[1]) if len(mant) > 1 else 0
    digits = mant[0].lstrip("+-")
    decimals = len(digits.split(".")[1]) if "." in digits else 0
    return 10.0 ** (exp - decimals)


def csv_columns(text):
    lines = [l for l in text.splitlines() if l.strip()]
    header = lines[0].split(",")
    cols = {h: [] for h in header}
    for l in lines[1:]:
        for h, v in zip(header, l.split(",")):
            v = v.strip()
            cols[h].append(v if NUM.match(v) else None)
    return cols


def vtk_arrays(text):
    arrays, name, cur = {}, None, None
    for l in text.splitlines():
        parts = l.split()
        if not parts:
            continue
        if parts[0] in ("SCALARS", "VECTORS", "POINTS", "FIELD") or (len(parts) >= 4 and parts[0].isalpha()
                                                                       and NUM.match(parts[1] or "") is None
                                                                       and parts[0] not in ("LOOKUP_TABLE",)):
            name = " ".join(parts[:2])
            cur = arrays.setdefault(name, [])
            continue
        if parts[0] in ("LOOKUP_TABLE", "CELL_DATA", "POINT_DATA", "CELLS", "CELL_TYPES", "DATASET"):
            if parts[0] in ("CELLS", "CELL_TYPES"):
                name = parts[0]
                cur = arrays.setdefault(name, [])
            continue
        if cur is not None and all(NUM.match(p) for p in parts):
            cur.extend(parts)
    return arrays


def check(a_vals, b_vals):
    """Returns (worst ratio, scale, worst delta, n) over matched numeric entries. The difference of
    two printed values is taken in exact decimal arithmetic: a one-unit difference in the last
    printed digit is exactly one unit, not 1 + binary round-off."""
    nums = [(Decimal(x), Decimal(y), Decimal(unit_str(x)), Decimal(unit_str(y)))
            for x, y in zip(a_vals, b_vals) if x is not None and y is not None]
    if not nums:
        return 0.0, 0.0, 0.0, 0
    scale = max(abs(y) for _, y, _, _ in nums)
    worst, wd = Decimal(0), Decimal(0)
    for x, y, ux, uy in nums:
        d = abs(x - y)
        allowed = max(Decimal("1e-6") * scale, max(ux, uy))
        if d > 0:
            worst = max(worst, d / allowed)
            wd = max(wd, d)
    return float(worst), float(scale), float(wd), len(nums)


def unit_str(token):
    mant = token.lower().split("e")
    exp = int(mant[1]) if len(mant) > 1 else 0
    digits = mant[0].lstrip("+-")
    decimals = len(digits.split(".")[1]) if "." in digits else 0
    return f"1e{exp - decimals}"


def self_test():
    """Non-vacuity: a column changed by 1e-5 of its scale must fail; one printed unit must pass."""
    base = ["1.000000", "-0.500000", "0.250000"]
    over = ["1.000000", "-0.500010", "0.250000"]
    unit_only = ["1.000000", "-0.500001", "0.250000"]
    r_over = check(over, base)[0]
    r_unit = check(unit_only, base)[0]
    ok = r_over > 1.0 and r_unit <= 1.0
    print(f"self-test: 1e-5 change ratio {r_over:.3f} (must be > 1), one printed unit {r_unit:.3f}"
          f" (must be <= 1) -> {'OK' if ok else 'INSTRUMENT BROKEN'}")
    return ok


def main():
    if not self_test():
        return 2
    cur, nog, log = sys.argv[1], sys.argv[2], sys.argv[3]
    files = [l.split()[1].rstrip(":") for l in open(log) if l.startswith("VALUES")]
    bad = 0
    for f in files:
        cat = category(f)
        base = os.path.basename(f)
        export = (f.endswith(".csv") and base != "residuals.csv") or f.endswith(".vtk")
        if cat == "?":
            bad += 1
            print(f"??  {f}: UNCATEGORIZED")
            continue
        if cat == "A" or not export:
            print(f"{cat}   {'diagnostic' if cat == 'B' else 'quantified'}  {f}")
            continue
        tc = open(os.path.join(cur, f), errors="replace").read()
        tn = open(os.path.join(nog, f), errors="replace").read()
        groups = (csv_columns(tc), csv_columns(tn)) if f.endswith(".csv") else (vtk_arrays(tc), vtk_arrays(tn))
        worst_all = 0.0
        detail = []
        for key in groups[1]:
            if key not in groups[0]:
                continue
            r, scale, wd, n = check(groups[0][key], groups[1][key])
            worst_all = max(worst_all, r)
            if wd > 0:
                detail.append(f"{key}: max|d| {wd:.3e} scale {scale:.3e} rel {wd / scale if scale else 0:.3e}")
        verdict = "PASS" if worst_all <= 1.0 else "FAIL"
        if worst_all > 1.0:
            bad += 1
        print(f"B   FIELD {verdict}  {f}  worst |d|/allowed {worst_all:.3e}")
        for d in detail[:6]:
            print("        " + d)
    print(f"C11(b) aligned field exports: {'PASS' if bad == 0 else 'FAIL'} ({bad} failing/uncategorized)")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
