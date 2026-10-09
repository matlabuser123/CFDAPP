#!/usr/bin/env python3
"""P17.10 -- CLAUDE.md section 7 runtime-noise audit, done properly.

Classifies EVERY changed line under results/validation as timing or not-timing, and fails closed on
anything that is not timing. Two earlier versions of this script were wrong, and both are worth
stating because each produced a confident answer:

  v1  matched the set of changed COLUMN INDICES (5, 7, 9) against the indices P17.9 recorded, and
      called that agreement. It is not agreement: the same index carries different headers in
      different files -- index 5 is 'iterations' in ten files and 'runtime (s)' in two; index 7 is
      'accepted' in two and 'runtime (s)' in ten. An index match can hide a change to iterations,
      accepted, status or linear-iteration counts.

  v2  resolved each changed cell to its own file's header, but never flushed pending rows at a FILE
      boundary -- only at a hunk boundary. Markdown rows collected from a .md file were therefore
      attributed to the NEXT file in the diff, which was usually a .json with no tables at all. It
      reported 47 'NON-TIMING' changes in files whose diffs contain no pipe characters.

  v3  took the FIRST table header in each file. results/validation/production/poiseuille.md holds
      TEN tables in SIX different shapes; in the 9-cell ones index 5 is 'runtime (s)', while in the
      12-cell first table index 5 is 'status'. It reported 6 runtime changes as changes to 'status'.
      Headers are now resolved from the NEAREST PRECEDING header row, located by the hunk's own line
      numbers.

The lesson all three times: the tool agreed with a plausible story before it was checked against a case
whose answer was known independently.
"""
import collections
import subprocess
import sys

TIMING_HEADERS = {"runtime (s)", "runtime", "seconds", "wall (s)", "time (s)"}
TIMING_JSON_KEYS = {"runtime", "runtime_seconds", "seconds", "wall_time_seconds",
                    "elapsed_seconds"}


def headers_of(path, cache={}):
    """All table headers in a file as [(line_number, cells)], ascending.

    A header is a pipe row whose NEXT row is a |---|---| separator. One file can hold many tables of
    different widths, so the caller must pick by line number, not take the first."""
    if path in cache:
        return cache[path]
    out = []
    try:
        with open(path, encoding="utf-8") as fh:
            lines = fh.read().splitlines()
        for i, ln in enumerate(lines):
            t = ln.strip().lstrip('"').lstrip()
            if not t.startswith("|"):
                continue
            nxt = lines[i + 1].strip().lstrip('"').lstrip() if i + 1 < len(lines) else ""
            if nxt.startswith("|") and set(nxt) <= set("|- :\\\""):
                out.append((i + 1, [c.strip() for c in t.split("|")]))
    except OSError:
        pass
    cache[path] = out
    return out


def header_for(path, line_no, ncells):
    """The header governing a row at `line_no`: the nearest preceding header with the same width."""
    best = None
    for hline, cells in headers_of(path):
        if hline <= line_no and len(cells) == ncells:
            best = cells
    if best is None:                      # fall back to nearest preceding of any width
        for hline, cells in headers_of(path):
            if hline <= line_no:
                best = cells
    return best


def is_table_row(text):
    return text.strip().lstrip('"').lstrip().startswith("|")


def json_key(text):
    t = text.strip()
    if t.startswith('"') and '"' in t[1:]:
        return t[1:].split('"', 1)[0]
    return None


diff = subprocess.run(["git", "diff", "-U0", "--", "results/validation"],
                      capture_output=True, text=True).stdout

table_pairs = []        # (path, line_no, minus_row, plus_row)
scalar_changes = []     # (path, raw_line)
cur = None
hunk_line = 0
minus, plus = [], []


def flush():
    global minus, plus
    for k, (a, b) in enumerate(zip(minus, plus)):
        table_pairs.append((cur, hunk_line + k, a, b))
    # Unpaired table rows mean a row was added or removed outright -- not runtime noise.
    for extra in minus[len(plus):] + plus[len(minus):]:
        scalar_changes.append((cur, "UNPAIRED TABLE ROW: " + extra.strip()[:80]))
    minus, plus = [], []


for line in diff.splitlines():
    if line.startswith("+++ b/"):
        flush()                      # <-- the fix: flush at the FILE boundary, not only at @@
        cur = line[6:]
        continue
    if line.startswith("--- ") or line.startswith("diff --git") or line.startswith("index "):
        continue
    if line.startswith("@@"):
        flush()
        try:
            hunk_line = int(line.split("+", 1)[1].split(",")[0].split()[0])
        except (IndexError, ValueError):
            hunk_line = 0
        continue
    if line.startswith("-") and is_table_row(line[1:]):
        minus.append(line[1:])
    elif line.startswith("+") and is_table_row(line[1:]):
        plus.append(line[1:])
    elif line.startswith(("+", "-")):
        scalar_changes.append((cur, line[1:]))
flush()

byheader = collections.Counter()
bykey = collections.Counter()
offenders = []
cells = 0

for path, line_no, a, b in table_pairs:
    ca = [c.strip() for c in a.split("|")]
    cb = [c.strip() for c in b.split("|")]
    hdr = header_for(path, line_no, len(ca))
    if len(ca) != len(cb):
        offenders.append((path, "COLUMN COUNT CHANGED", a.strip()[:70]))
        continue
    for i in range(len(ca)):
        if ca[i] == cb[i]:
            continue
        cells += 1
        name = hdr[i] if hdr and i < len(hdr) else f"<no header, index {i}>"
        byheader[name] += 1
        if name.lower() not in TIMING_HEADERS:
            offenders.append((path, name, f"{ca[i]!r} -> {cb[i]!r}"))

for path, raw in scalar_changes:
    k = json_key(raw)
    label = k if k else f"<not a key> {raw.strip()[:50]}"
    bykey[label] += 1
    if k is None or k not in TIMING_JSON_KEYS:
        offenders.append((path, label, raw.strip()[:70]))

print(f"changed MARKDOWN table rows compared : {len(table_pairs)}")
print(f"  cells that changed                 : {cells}")
for name, n in byheader.most_common():
    mark = "TIMING" if name.lower() in TIMING_HEADERS else "*** NON-TIMING ***"
    print(f"      {n:4d}  {name!r}   {mark}")
print(f"changed NON-TABLE (JSON) lines       : {len(scalar_changes)}")
for name, n in bykey.most_common():
    mark = "TIMING" if name in TIMING_JSON_KEYS else "*** NON-TIMING ***"
    print(f"      {n:4d}  {name!r}   {mark}")

print("\ncontrols -- the classifier must reject things that are not timing:")
for probe in ("wall_shear_stress", "status", "iterations", "accepted"):
    ok = probe not in TIMING_JSON_KEYS and probe not in TIMING_HEADERS
    print(f"    {probe!r:22} rejected as non-timing: {'yes' if ok else 'NO -- BROKEN'}")

if offenders:
    print(f"\n*** {len(offenders)} NON-TIMING CHANGES -- this is NOT runtime noise ***")
    for path, name, detail in offenders[:25]:
        print(f"    {path}  [{name}]  {detail}")
    sys.exit(1)

print("\nVERDICT: every changed line under results/validation is a timing value.")
