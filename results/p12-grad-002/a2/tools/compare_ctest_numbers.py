#!/usr/bin/env python3
"""P12-GRAD-002 A2, C8: every number the test suite prints, cur vs nograd.

usage: compare_ctest_numbers.py <cur ctest -V output> <nograd ctest -V output> [name-regex]

`ctest -V -j N` prefixes each output line with the test number ("123: ..."). Lines are grouped per
test; gtest's own timing "(N ms)" and ctest's timing lines are removed. For every test:
  status in each tree (Passed / Failed / Not Run)
  SAME            identical output
  CHANGED         the differing lines, each numeric token's relative change
                  rel = |a - b| / max(|a|, |b|); the report gives the count, the largest rel and
                  the first differing lines (nograd -> cur)
A test whose output lines differ in number or text (not only in numbers) is reported as TEXT.
"""
import re
import sys
from collections import defaultdict

LINE = re.compile(r"^(\d+): (.*)$")
START = re.compile(r"^\s*Start\s+(\d+): (\S+)")
RESULT = re.compile(r"^\s*\d+/\d+ Test\s+#(\d+): (\S+) \.+\s*(\*{0,3}\S+(?: \S+)?)")
NUM = re.compile(r"[-+]?(?:\d+\.\d*|\.\d+|\d+)(?:[eE][-+]?\d+)?")
TREE = re.compile(r"/g2/(?:cur|nograd)/")
TIMING = re.compile(r"\d+ ms\b|Test time|sec\b|seconds|elapsed|wall[ _-]?clock|runtime", re.I)


UNIT_TIME = re.compile(r"[-+]?\d+(?:\.\d+)?(?:[eE][-+]?\d+)?\s*(?:s|ms|us)\b")
TIME_HEADER = re.compile(r"time|\(s\)|runtime|seconds|wall", re.I)


def mask_timings(lines):
    """Replace wall-clock values by 'T': numbers carrying a time unit, and the cells of Markdown
    table rows whose column header names a time (the header is the row above a |---| line)."""
    out, header = [], None
    for i, line in enumerate(lines):
        line = UNIT_TIME.sub("T", line)
        if line.lstrip().startswith("|"):
            cells = line.strip().strip("|").split("|")
            nxt = lines[i + 1] if i + 1 < len(lines) else ""
            if nxt.lstrip().startswith("|") and set(nxt.replace("|", "").strip()) <= set("-: ") and "-" in nxt:
                header = [c.strip() for c in cells]
            elif header is not None and len(cells) == len(header):
                cells = ["T" if TIME_HEADER.search(h) else c for c, h in zip(cells, header)]
                line = "|" + "|".join(cells) + "|"
        else:
            header = None
        out.append(line)
    return out


def parse(path):
    names, status, out = {}, {}, defaultdict(list)
    with open(path, errors="replace") as fh:
        for raw in fh:
            raw = raw.rstrip("\n")
            m = START.match(raw)
            if m:
                names[int(m.group(1))] = m.group(2)
                continue
            m = RESULT.match(raw)
            if m:
                names[int(m.group(1))] = m.group(2)
                status[int(m.group(1))] = m.group(3).replace("*", "").strip()
                continue
            m = LINE.match(raw)
            if m:
                # the two isolated trees differ in their path only (/g2/cur/ vs /g2/nograd/)
                text = TREE.sub("/g2/TREE/", m.group(2))
                # (a Markdown table row is kept: mask_timings needs its header, and masks by column)
                if TIMING.search(text) and not text.lstrip().startswith("|"):
                    continue
                out[int(m.group(1))].append(text)
    by_name = {}
    for num, name in names.items():
        by_name[name] = (status.get(num, "?"), mask_timings(out.get(num, [])))
    return by_name


def skeleton(line):
    return NUM.sub("#", line)


def main():
    cur, nog = parse(sys.argv[1]), parse(sys.argv[2])
    pattern = re.compile(sys.argv[3]) if len(sys.argv) > 3 else None
    totals = defaultdict(int)
    for name in sorted(set(cur) | set(nog)):
        if pattern and not pattern.search(name):
            continue
        sc, lc = cur.get(name, ("absent", []))
        sn, ln = nog.get(name, ("absent", []))
        if lc == ln:
            totals["SAME"] += 1
            if sc != sn:
                print(f"STATUS  {name}: nograd {sn} -> cur {sc} (identical output)")
            continue
        if len(lc) != len(ln) or any(skeleton(p) != skeleton(q) for p, q in zip(ln, lc)):
            totals["TEXT"] += 1
            print(f"TEXT    {name}: nograd {sn} -> cur {sc}; {len(ln)} -> {len(lc)} lines")
            shown = 0
            for p, q in zip(ln, lc):
                if p != q and shown < 4:
                    print(f"          - {p[:170]}\n          + {q[:170]}")
                    shown += 1
            continue
        worst, count, first = 0.0, 0, []
        for p, q in zip(ln, lc):
            if p == q:
                continue
            count += 1
            for x, y in zip(NUM.findall(p), NUM.findall(q)):
                x, y = float(x), float(y)
                if x != y:
                    worst = max(worst, abs(x - y) / max(abs(x), abs(y)))
            if len(first) < 4:
                first.append((p, q))
        totals["CHANGED"] += 1
        print(f"CHANGED {name}: nograd {sn} -> cur {sc}; {count} lines, max rel {worst:.3e}")
        for p, q in first:
            print(f"          - {p[:170]}\n          + {q[:170]}")
    print("summary: " + ", ".join(f"{k} {v}" for k, v in sorted(totals.items())))


if __name__ == "__main__":
    main()
