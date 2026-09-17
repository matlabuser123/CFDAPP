#!/usr/bin/env python3
"""P12-MESH-007 gate rerun: corroborate that the rerun executes the ORIGINAL gate instrument
(NOT a gate criterion; disclosure support for logs 20-22).

The four ALE test sources are untracked, and the original run (logs 10-12, 2026-09-15) did not
record their hashes. FORMAT-001 (results/p12-diff-002/format-001) later reformatted them and kept
the pre-format copies in data/before, with a token-level proof that only layout changed. This
tool checks three independent links:

  A  the original run's gtest failure records (path:LINE + asserted expression) are found at
     exactly those lines, with exactly that expression, in the FORMAT-001 pre-format copy, and the
     same assertion occurs once in the current file;
  B  FORMAT-001's recorded hashes: pre-format copy == 'before', current file == 'after', and the
     token proof says code SAME for each of the four files;
  C  per stage (10 vs 20, 11 vs 21, 12 vs 22): the same tests run in the same order, and every
     test prints the same line skeletons (numbers masked) with the same printed thresholds
     ("(<= X)", "(>= X)" kept verbatim). Differences are listed; the only expected ones are the
     gtest failure records and FAILED/PASSED summary lines of a stage that changed outcome.

usage: instrument_identity.py            run A, B, C (C needs logs 20-22)
       instrument_identity.py --self-test  show that C flags a changed threshold, a missing test
                                           and a reordered test, and that A flags a wrong line
"""
import hashlib
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, "..", "..", ".."))
LOGS = os.path.join(REPO, "results", "p12-mesh-007", "logs")
F001 = os.path.join(REPO, "results", "p12-diff-002", "format-001")
FILES = [
    "tests/solver/piso/test_ale_piso.cpp",
    "tests/support/MeshMotionCases.hpp",
    "tests/unit/discretization/test_ale_operators.cpp",
    "tests/unit/mesh/test_mesh_motion.cpp",
]
PAIRS = [
    ("10_gate_stage1_G1.1_G1.3_G2_G3_G4.log", "20_rerun_stage1_G1.1_G1.3_G2_G3_G4.log"),
    ("11_gate_stage2_G1.4_G6.1_G6.5.log", "21_rerun_stage2_G1.4_G6.1_G6.5.log"),
    ("12_gate_stage3_G1.2_G5_G6_G7_G8.log", "22_rerun_stage3_G1.2_G5_G6_G7_G8.log"),
]
FAILURE = re.compile(r"^(/\S+?/)?(tests/\S+):(\d+): Failure$")
EXPECTED = re.compile(r"^Expected: \((.*)\) (<=|>=|<|>|==|!=) \((.*)\), actual: ")
OPS = {"<=": "EXPECT_LE", ">=": "EXPECT_GE", "<": "EXPECT_LT", ">": "EXPECT_GT",
       "==": "EXPECT_EQ", "!=": "EXPECT_NE"}
THRESH = re.compile(r"\((?:<=|>=|<|>)\s*[^()]*\)")
NUM = re.compile(r"[-+]?(?:\d+\.\d*|\.\d+|\d+)(?:[eE][-+]?\d+)?")
GTEST_NOISE = re.compile(r"^\[  (?:FAILED|PASSED)  \]|^ ?\d+ FAILED TESTS?$|^\[==========\] \d+ tests? from .* ran\.")


def sha(path):
    with open(path, "rb") as fh:
        return hashlib.sha256(fh.read()).hexdigest()


def squash(s):
    return re.sub(r"\s+", "", s)


def check_a(log12_lines, before_root, current_root):
    """Return (ok, report lines)."""
    out, ok, n = [], True, 0
    for i, line in enumerate(log12_lines):
        m = FAILURE.match(line)
        if not m or i + 1 >= len(log12_lines):
            continue
        e = EXPECTED.match(log12_lines[i + 1])
        if not e:
            continue
        n += 1
        rel, lineno = m.group(2), int(m.group(3))
        call = squash(f"{OPS[e.group(2)]}({e.group(1)},{e.group(3)});")
        with open(os.path.join(before_root, rel), errors="replace") as fh:
            before = fh.read().split("\n")
        at = squash(before[lineno - 1]) if lineno <= len(before) else ""
        hit = at == call
        with open(os.path.join(current_root, rel), errors="replace") as fh:
            cur = fh.read().split("\n")
        cur_hits = [k + 1 for k, l in enumerate(cur) if squash(l) == call]
        good = hit and len(cur_hits) == 1
        ok &= good
        out.append(f"  A {'OK  ' if good else 'FAIL'} {rel}:{lineno} {call} -- pre-format copy line "
                   f"{lineno}: {'same' if hit else 'DIFFERENT: ' + at}; current file: lines {cur_hits}")
    if n == 0:
        ok = False
        out.append("  A FAIL no failure records found")
    return ok, out


def parse_log(lines):
    """Ordered test names; per test, printed lines (skeleton, thresholds)."""
    tests, body, cur = [], {}, None
    for line in lines:
        if line.startswith("#") or line.startswith("exit "):
            continue
        if line.startswith("[ RUN      ] "):
            cur = line[13:].strip()
            tests.append(cur)
            body[cur] = []
            continue
        if line.startswith("[       OK ] ") or line.startswith("[  FAILED  ] ") and cur and line[13:].startswith(cur):
            cur = None
            continue
        if cur is None or not line.strip():
            continue
        if FAILURE.match(line) or EXPECTED.match(line) or GTEST_NOISE.match(line):
            body[cur].append(("GTEST-FAILURE-RECORD", ()))
            continue
        th = tuple(THRESH.findall(line))
        skel = NUM.sub("#", THRESH.sub("<T>", line))
        body[cur].append((skel, th))
    return tests, body


def check_c(old_lines, new_lines, label):
    out, ok = [], True
    to, bo = parse_log(old_lines)
    tn, bn = parse_log(new_lines)
    if to != tn:
        ok = False
        out.append(f"  C FAIL {label}: test list differs: original {len(to)} {to[:3]}..., rerun {len(tn)}")
        for k, (a, b) in enumerate(zip(to, tn)):
            if a != b:
                out.append(f"      first difference at #{k}: {a} vs {b}")
                break
        return ok, out
    nth = nlines = 0
    for t in to:
        a = [x for x in bo[t] if x[0] != "GTEST-FAILURE-RECORD"]
        b = [x for x in bn[t] if x[0] != "GTEST-FAILURE-RECORD"]
        fa = len(bo[t]) - len(a)
        fb = len(bn[t]) - len(b)
        nlines += len(a)
        nth += sum(len(x[1]) for x in a)
        if a != b:
            ok = False
            out.append(f"  C FAIL {label}: {t}: printed skeleton/thresholds differ ({len(a)} vs {len(b)} lines)")
            for x, y in zip(a, b):
                if x != y:
                    out.append(f"      original: {x[0][:150]} {x[1]}")
                    out.append(f"      rerun:    {y[0][:150]} {y[1]}")
                    break
        if fa != fb:
            out.append(f"  C note {label}: {t}: gtest failure-record lines original {fa}, rerun {fb}")
    out.insert(0, f"  C {'OK  ' if ok else 'FAIL'} {label}: {len(to)} tests, same order; {nlines} printed "
                  f"lines with identical skeletons; {nth} printed thresholds identical")
    return ok, out


def read(path):
    with open(path, errors="replace") as fh:
        return fh.read().split("\n")


def self_test():
    ok = True
    old = read(os.path.join(LOGS, PAIRS[2][0]))
    base_ok, _ = check_c(old, old, "identity")
    print(f"self-test C identical log accepted: {base_ok}")
    ok &= base_ok
    muts = {
        "threshold 1e-8 -> 1e-6 in one printed line": [
            l.replace("(<= 1e-8)", "(<= 1e-6)", 1) if l.startswith("G6.3 translating") else l for l in old],
        "one test removed": [l for l in old if "AlePisoOutput.VtkWritesTheCurrentMovedGeometry" not in l],
        "two tests swapped": None,
        "one printed line dropped": [l for l in old if not l.startswith("  G6.3 step 20:")],
    }
    runs = [i for i, l in enumerate(old) if l.startswith("[ RUN      ] ")]
    sw = list(old)
    sw[runs[0]], sw[runs[1]] = old[runs[1]], old[runs[0]]
    muts["two tests swapped"] = sw
    for name, lines in muts.items():
        r, _ = check_c(old, lines, name)
        print(f"self-test C mutant '{name}' rejected: {not r}")
        ok &= not r
    bad = [l.replace(":491: Failure", ":490: Failure") for l in old]
    r, _ = check_a(bad, os.path.join(F001, "data", "before"), REPO)
    print(f"self-test A wrong line number rejected: {not r}")
    ok &= not r
    print("SELF-TEST", "PASS" if ok else "FAIL")
    return 0 if ok else 1


def main():
    if "--self-test" in sys.argv:
        return self_test()
    ok = True
    print("# P12-MESH-007 rerun instrument identity (NOT a gate criterion)")
    a_ok, rep = check_a(read(os.path.join(LOGS, PAIRS[2][0])), os.path.join(F001, "data", "before"), REPO)
    print("## A: original failure records vs the FORMAT-001 pre-format copy and the current file")
    print("\n".join(rep))
    ok &= a_ok
    print("## B: FORMAT-001 hash chain and token proof")
    fmt = "\n".join(read(os.path.join(F001, "logs", "01_format.log")))
    tok = "\n".join(read(os.path.join(F001, "logs", "02_token_proof.log")))
    for f in FILES:
        m = re.search(re.escape(f) + r"\s+full ([0-9a-f]{64}) -> ([0-9a-f]{64})", fmt)
        hb = sha(os.path.join(F001, "data", "before", f))
        hc = sha(os.path.join(REPO, f))
        tp = re.search(re.escape(f) + r": code SAME, includes SAME-SET, comment words SAME", tok) is not None
        good = bool(m) and m.group(1) == hb and m.group(2) == hc and tp
        ok &= good
        print(f"  B {'OK  ' if good else 'FAIL'} {f}: pre-format copy {hb[:16]} (recorded before "
              f"{m.group(1)[:16] if m else '?'}), current {hc[:16]} (recorded after "
              f"{m.group(2)[:16] if m else '?'}), token proof code SAME: {tp}")
    print("## C: original gate logs vs rerun logs")
    for o, n in PAIRS:
        if not os.path.exists(os.path.join(LOGS, n)):
            print(f"  C SKIP {n} does not exist")
            ok = False
            continue
        c_ok, rep = check_c(read(os.path.join(LOGS, o)), read(os.path.join(LOGS, n)), f"{o[:2]} vs {n[:2]}")
        print("\n".join(rep))
        ok &= c_ok
    print("RESULT", "IDENTICAL INSTRUMENT CORROBORATED" if ok else "NOT CORROBORATED (see above)")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
