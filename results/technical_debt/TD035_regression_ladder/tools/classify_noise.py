"""Classify every tracked file the regression rewrote: timing-only, or a REAL change?

CLAUDE.md section 7: "Every full ctest run rewrites tracked results/validation/** reports with
timing-only changes. Classify those changes and restore the noise; do not commit it."

This classifies rather than assumes. It FAILS CLOSED: anything it cannot prove is timing-only is
reported as SUSPECT, so a real physics change can never be silently restored away.

Method, per changed hunk line pair:
  * JSON / key:value  -- the changed KEY must be a timing key.
  * Markdown table row -- split on '|' and compare cells pairwise; every differing cell must sit
    under a timing-named column header, and the row must have the same cell count.
  * anything else      -- SUSPECT.
"""
import re
import subprocess
import sys

# FIXED: the previous alternation required a closing quote immediately after "runtime", so
# "runtime_seconds" fell through as unclassified and ~20 files were FALSE POSITIVES. Match any
# key whose NAME contains a timing word.
TIMING_KEY = re.compile(r'"(\w*(?:seconds|runtime|wall|elapsed|duration|_time|time_)\w*)"\s*:', re.I)
# A build stamp is NOT timing: it is the commit SHA embedded at configure time via Version.hpp.
# Tracked separately so it can never be restored away as "noise".
BUILD_KEY = re.compile(r'"(build_identity|version|git_sha|commit)"\s*:', re.I)
TIMING_COL = re.compile(r'runtime|seconds|wall|elapsed|time|duration', re.I)

REPO = "/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp"


def run(args):
    return subprocess.run(args, cwd=REPO, capture_output=True, text=True,
                          errors="replace").stdout


files = [f for f in run(["git", "diff", "--name-only"]).split("\n") if f.strip()]
print("tracked files modified by the run: %d" % len(files))
print()

verdicts = {}
for f in files:
    diff = run(["git", "diff", "-U3", "--", f]).split("\n")
    minus = [l[1:] for l in diff if l.startswith("-") and not l.startswith("---")]
    plus = [l[1:] for l in diff if l.startswith("+") and not l.startswith("+++")]
    context = [l[1:] for l in diff if l.startswith(" ")]

    # the most recent table header seen in context, for column names
    header = None
    for l in context:
        if l.count("|") >= 3 and TIMING_COL.search(l):
            header = [c.strip() for c in l.split("|")]
            break

    reasons = []
    if len(minus) != len(plus):
        reasons.append("line count differs (%d removed, %d added)" % (len(minus), len(plus)))
    else:
        for a, b in zip(minus, plus):
            if a == b:
                continue
            # --- build stamp: real, explainable, NOT timing
            if BUILD_KEY.search(a) and BUILD_KEY.search(b):
                reasons.append("BUILD STAMP (not timing): %s" % a.strip()[:60])
                continue
            # --- JSON / key:value
            if TIMING_KEY.search(a) and TIMING_KEY.search(b):
                ka = TIMING_KEY.search(a).group(1).lower()
                kb = TIMING_KEY.search(b).group(1).lower()
                if ka == kb:
                    continue
                reasons.append("key changed: %s -> %s" % (ka, kb))
                continue
            # --- markdown table row
            if a.count("|") >= 3 and b.count("|") >= 3:
                ca = [c.strip() for c in a.split("|")]
                cb = [c.strip() for c in b.split("|")]
                if len(ca) != len(cb):
                    reasons.append("table row cell count differs")
                    continue
                bad = []
                for i, (x, y) in enumerate(zip(ca, cb)):
                    if x == y:
                        continue
                    name = header[i] if header and i < len(header) else "(col %d)" % i
                    if not TIMING_COL.search(name):
                        bad.append("%s: %r -> %r" % (name, x, y))
                if bad:
                    reasons.extend(bad)
                continue
            reasons.append("unclassified line: %r -> %r" % (a[:70], b[:70]))

    verdicts[f] = reasons

clean = [f for f, r in verdicts.items() if not r]
suspect = {f: r for f, r in verdicts.items() if r}

print("TIMING-ONLY (safe to restore) : %d" % len(clean))
print("SUSPECT  (do NOT restore)     : %d" % len(suspect))
print()
if suspect:
    print("=== SUSPECT FILES -- every one must be explained before any restore")
    for f, r in sorted(suspect.items()):
        print("  %s" % f)
        for x in r[:6]:
            print("      %s" % x)
        if len(r) > 6:
            print("      ... and %d more" % (len(r) - 6))
else:
    print("Every changed line is a timing field or a timing column. The whole set is noise.")

sys.exit(1 if suspect else 0)
