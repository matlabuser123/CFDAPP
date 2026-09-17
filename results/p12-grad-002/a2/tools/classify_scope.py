#!/usr/bin/env python3
"""P12-GRAD-002 A2 step 5: classify the generated outputs of a full regression.

usage: classify_scope.py <reference-root> <candidate-root> <file-list>

For every path in <file-list> (the repository's generated-output scopes: results/validation,
cases/*/results, tests/data/cases/*/results), compare <candidate-root>/path with
<reference-root>/path using MESH-006's classifier rules (imported unchanged):
IDENTICAL, RUNTIME-ONLY (timing fields only), VALUES (listed with the first entries), NEW, GONE.
Exit status 0 if there is no VALUES/NEW/GONE file.
"""
import importlib.util
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
spec = importlib.util.spec_from_file_location(
    "m6", os.path.join(HERE, "..", "..", "..", "p12-mesh-006", "a3", "tools", "classify_generated_outputs.py"))
m6 = importlib.util.module_from_spec(spec)
spec.loader.exec_module(m6)


def diffs(path, a, b):
    if path.endswith(".json"):
        return m6.json_diffs(a, b)
    if path.endswith(".md"):
        return m6.md_diffs(a, b)
    if path.endswith(".csv"):
        return m6.csv_diffs(a, b)
    return ["(not a .json/.md/.csv file: any difference counts)"]


def main():
    ref, cand, listing = sys.argv[1], sys.argv[2], sys.argv[3]
    files = [l.strip() for l in open(listing) if l.strip()]
    counts = {"IDENTICAL": 0, "RUNTIME-ONLY": 0, "VALUES": 0, "NEW": 0, "GONE": 0}
    for f in sorted(set(files)):
        a, b = os.path.join(ref, f), os.path.join(cand, f)
        if not os.path.exists(a) and os.path.exists(b):
            counts["NEW"] += 1
            print(f"NEW           {f}")
            continue
        if os.path.exists(a) and not os.path.exists(b):
            counts["GONE"] += 1
            print(f"GONE          {f}")
            continue
        x, y = open(a, "rb").read(), open(b, "rb").read()
        if x == y:
            counts["IDENTICAL"] += 1
            continue
        try:
            d = diffs(f, x.decode(errors="replace"), y.decode(errors="replace"))
        except ValueError as e:
            d = [f"(parse error: {e})"]
        if d:
            counts["VALUES"] += 1
            print(f"VALUES        {f} ({len(d)} non-timing differences)")
            for o in d[:6]:
                print("      " + o[:200])
        else:
            counts["RUNTIME-ONLY"] += 1
            print(f"RUNTIME-ONLY  {f}")
    print(f"checked {len(set(files))} files; " + ", ".join(f"{k} {v}" for k, v in counts.items()))
    return 0 if counts["VALUES"] == counts["NEW"] == counts["GONE"] == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
