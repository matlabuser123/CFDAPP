#!/usr/bin/env python3
"""P12-GRAD-002 A2, C11(b): every generated output of the two isolated trees (cur = current
sources, nograd = the same sources with the pre-GRAD-002 Gradient.cpp), after each ran the full
suite from the same generated-output state.

usage: compare_outputs.py <cur-root> <nograd-root>

Per file (union of both trees' results/validation, cases/*/results, tests/data/cases/*/results):
  IDENTICAL     byte-identical
  RUNTIME-ONLY  every difference is a timing value (MESH-006's classifier rules, imported unchanged)
  VALUES        otherwise; every differing entry is listed with its relative change
                rel = |a - b| / max(|a|, |b|) (numeric entries; a non-numeric difference is
                reported as such)
  ONLY-CUR / ONLY-NOGRAD
The C11(b) verdict is NOT computed here: it needs each file's case geometry (aligned or not), which
the report states per file.
"""
import importlib.util
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
CLASSIFIER = os.path.join(HERE, "..", "..", "..", "p12-mesh-006", "a3", "tools",
                          "classify_generated_outputs.py")
spec = importlib.util.spec_from_file_location("m6classify", CLASSIFIER)
m6 = importlib.util.module_from_spec(spec)
spec.loader.exec_module(m6)

NUM = re.compile(r"[-+]?(?:\d+\.\d*|\.\d+|\d+)(?:[eE][-+]?\d+)?")


def listing(root):
    out = set()
    for top in ("results/validation", "cases", "tests/data/cases"):
        base = os.path.join(root, top)
        for d, _, files in os.walk(base):
            rel = os.path.relpath(d, root).replace(os.sep, "/")
            parts = rel.split("/")
            if top == "cases" and not (len(parts) >= 3 and parts[2] == "results"):
                continue
            if top == "tests/data/cases" and not (len(parts) >= 5 and parts[4] == "results"):
                continue
            for f in files:
                out.add(rel + "/" + f)
    return out


def rel_changes(entries):
    """entries: strings 'label: A -> B'; returns (max rel, label of max, non-numeric count)."""
    worst, where, nonnum = 0.0, "", 0
    for e in entries:
        m = re.match(r"(.*): (.*) -> (.*)$", e)
        if not m:
            nonnum += 1
            continue
        a, b = NUM.findall(m.group(2)), NUM.findall(m.group(3))
        if not a or len(a) != len(b):
            nonnum += 1
            continue
        for x, y in zip(a, b):
            x, y = float(x), float(y)
            r = 0.0 if x == y else abs(x - y) / max(abs(x), abs(y))
            if r > worst:
                worst, where = r, m.group(1)[:70] + " (" + m.group(2)[:24] + " -> " + m.group(3)[:24] + ")"
    return worst, where, nonnum


def main():
    cur, nog = sys.argv[1], sys.argv[2]
    a, b = listing(cur), listing(nog)
    counts = {}
    for f in sorted(a | b):
        if f not in b or f not in a:
            kind = "ONLY-CUR" if f not in b else "ONLY-NOGRAD"
            counts[kind] = counts.get(kind, 0) + 1
            print(f"{kind:13s} {f}")
            continue
        x = open(os.path.join(nog, f), "rb").read()
        y = open(os.path.join(cur, f), "rb").read()
        if x == y:
            counts["IDENTICAL"] = counts.get("IDENTICAL", 0) + 1
            continue
        tx, ty = x.decode(errors="replace"), y.decode(errors="replace")
        try:
            if f.endswith(".json"):
                diffs = m6.json_diffs(tx, ty)
            elif f.endswith(".md"):
                diffs = m6.md_diffs(tx, ty)
            elif f.endswith(".csv"):
                diffs = m6.csv_diffs(tx, ty)
            else:
                lx, ly = tx.splitlines(), ty.splitlines()
                diffs = [f"line {i + 1}: {p} -> {q}" for i, (p, q) in enumerate(zip(lx, ly)) if p != q]
                if len(lx) != len(ly):
                    diffs.append(f"line count {len(lx)} -> {len(ly)}")
        except ValueError as e:
            diffs = [f"(parse error: {e})"]
        if not diffs:
            counts["RUNTIME-ONLY"] = counts.get("RUNTIME-ONLY", 0) + 1
            print(f"RUNTIME-ONLY  {f}")
            continue
        counts["VALUES"] = counts.get("VALUES", 0) + 1
        worst, where, nonnum = rel_changes(diffs)
        print(f"VALUES        {f}: {len(diffs)} entries (nograd -> cur), max rel {worst:.3e} at {where}"
              + (f", {nonnum} non-numeric" if nonnum else ""))
        for d in diffs[:6]:
            print("                " + d[:180])
    print("summary: " + ", ".join(f"{k} {v}" for k, v in sorted(counts.items())))


if __name__ == "__main__":
    main()
