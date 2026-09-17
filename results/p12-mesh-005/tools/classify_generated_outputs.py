"""P12-MESH-005 gate G5: classify every generated output that differs from the pre-MESH-005 working tree.

Usage (from the repository root):  python3 classify_generated_outputs.py <snapshot-root> [--restore]

Files: every file of the current tree listed by `git ls-files -co --exclude-standard` (the same listing the
snapshot was copied from) under results/, cases/*/results/ or tests/data/cases/*/results/, except
results/p12-mesh-005/ (this phase's own evidence). Each is compared with <snapshot-root>/<same path>:

  IDENTICAL     byte-identical;
  RUNTIME-ONLY  every difference is a timing value:
                  .json  leaf by leaf, leaves whose key path names a time excluded;
                  .md    table rows cell by cell, columns whose header names a time excluded; any other
                         differing line is a VALUES difference;
                  .csv   cell by cell, columns whose header names a time excluded;
                  other  never (any difference is VALUES);
  VALUES        anything else -- printed with the first differing entries (before -> after);
  NEW           absent from the snapshot.
A timing key/column is one whose name contains one of TIMING_WORDS (case-insensitive; see the comment
there: physics keys such as wall_shear_* never match). Nothing else is ever excluded. With --restore, RUNTIME-ONLY files are copied back from the snapshot (the pre-MESH-005
state); VALUES and NEW files are never touched.
"""
import json
import os
import shutil
import subprocess
import sys

# Deliberately narrow: the outputs name timings "runtime", "runtime_seconds", "seconds" and the Markdown
# column "runtime (s)". A bare "wall" or "time" is NOT a timing word here (wall_shear_*, *_wall_heat_*,
# time_step are physics) -- such keys are always compared.
TIMING_WORDS = ("runtime", "seconds", "elapsed", "wall_time", "walltime", "wall_clock", "duration",
                "timestamp", "generated_at")
EXCLUDED_PREFIX = "results/p12-mesh-005/"


def is_timing(name):
    n = name.lower()
    return any(w in n for w in TIMING_WORDS)


def leaves(node, path=""):
    if isinstance(node, dict):
        for k, v in node.items():
            yield from leaves(v, path + "/" + str(k))
    elif isinstance(node, list):
        for i, v in enumerate(node):
            yield from leaves(v, path + "[" + str(i) + "]")
    else:
        yield path, node


def json_diffs(a, b):
    x = dict(leaves(json.loads(a)))
    y = dict(leaves(json.loads(b)))
    out = []
    for k in sorted(set(x) | set(y)):
        if is_timing(k):
            continue
        if x.get(k) != y.get(k):
            out.append("%s: %r -> %r" % (k, x.get(k), y.get(k)))
    return out


def split_row(line):
    return [c.strip() for c in line.strip().strip("|").split("|")]


def md_diffs(a, b):
    la, lb = a.splitlines(), b.splitlines()
    if len(la) != len(lb):
        return ["line count %d -> %d" % (len(la), len(lb))]
    out = []
    header = None  # cells of the current table's header row
    for i, (p, q) in enumerate(zip(la, lb)):
        is_row = p.lstrip().startswith("|")
        if is_row and i + 1 < len(la) and set(la[i + 1].replace("|", "").strip()) <= set("-: ") \
                and la[i + 1].lstrip().startswith("|") and "-" in la[i + 1]:
            header = split_row(p)
        if not is_row:
            header = None
        if p == q:
            continue
        if is_row and header is not None and q.lstrip().startswith("|"):
            cp, cq = split_row(p), split_row(q)
            if len(cp) != len(cq) or len(cp) != len(header):
                out.append("row shape changed: %s -> %s" % (p[:80], q[:80]))
                continue
            bad = [(header[j], cp[j], cq[j]) for j in range(len(cp)) if cp[j] != cq[j] and not is_timing(header[j])]
            for h, u, v in bad:
                out.append("table column '%s': %s -> %s (row: %s)" % (h, u, v, p[:60]))
        else:
            out.append("line %d: %s -> %s" % (i + 1, p[:90], q[:90]))
    return out


def csv_diffs(a, b):
    la, lb = a.splitlines(), b.splitlines()
    if len(la) != len(lb) or not la:
        return ["line count %d -> %d" % (len(la), len(lb))]
    header = la[0].split(",")
    if lb[0] != la[0]:
        return ["header changed"]
    out = []
    for i, (p, q) in enumerate(zip(la[1:], lb[1:]), start=2):
        if p == q:
            continue
        cp, cq = p.split(","), q.split(",")
        if len(cp) != len(cq):
            out.append("line %d: shape changed" % i)
            continue
        for j in range(len(cp)):
            if cp[j] != cq[j] and not (j < len(header) and is_timing(header[j])):
                out.append("line %d column '%s': %s -> %s" % (i, header[j] if j < len(header) else j, cp[j], cq[j]))
    return out


def main():
    base = sys.argv[1]
    restore = "--restore" in sys.argv[2:]
    listing = subprocess.run(["git", "ls-files", "-co", "--exclude-standard"], capture_output=True, text=True,
                             check=True).stdout.splitlines()
    files = []
    for f in listing:
        if f.startswith(EXCLUDED_PREFIX):
            continue
        parts = f.split("/")
        if f.startswith("results/") or (f.startswith("cases/") and len(parts) > 2 and parts[2] == "results") or \
                (f.startswith("tests/data/cases/") and len(parts) > 4 and parts[4] == "results"):
            files.append(f)
    counts = {"IDENTICAL": 0, "RUNTIME-ONLY": 0, "VALUES": 0, "NEW": 0}
    runtime_only = []
    for f in sorted(files):
        if not os.path.isfile(f):
            continue
        g = os.path.join(base, f)
        if not os.path.exists(g):
            counts["NEW"] += 1
            print("NEW           " + f)
            continue
        a = open(g, "rb").read()
        b = open(f, "rb").read()
        if a == b:
            counts["IDENTICAL"] += 1
            continue
        ta, tb = a.decode(errors="replace"), b.decode(errors="replace")
        try:
            if f.endswith(".json"):
                other = json_diffs(ta, tb)
            elif f.endswith(".md"):
                other = md_diffs(ta, tb)
            elif f.endswith(".csv"):
                other = csv_diffs(ta, tb)
            else:
                other = ["(not a .json/.md/.csv file: any difference counts)"]
        except ValueError as e:
            other = ["(parse error: %s)" % e]
        if other:
            counts["VALUES"] += 1
            print("VALUES        %s (%d non-timing differences)" % (f, len(other)))
            for o in other[:8]:
                print("      " + o[:200])
        else:
            counts["RUNTIME-ONLY"] += 1
            runtime_only.append(f)
            print("RUNTIME-ONLY  " + f)
    print("checked %d files; " % len(files) + ", ".join("%s %d" % kv for kv in counts.items()))
    if restore:
        for f in runtime_only:
            shutil.copy2(os.path.join(base, f), f)
        print("restored from the snapshot (RUNTIME-ONLY): %d files" % len(runtime_only))
        for f in runtime_only:
            print("  " + f)


if __name__ == "__main__":
    main()
