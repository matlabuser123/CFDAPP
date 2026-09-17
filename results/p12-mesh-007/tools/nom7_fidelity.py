#!/usr/bin/env python3
"""P12-MESH-007 Amendment A3 §2: fidelity of the G9 reference nom7 (a precondition, not a result).

usage: nom7_fidelity.py <BASE root> <nom7 root> <current root>

Compared over src/, include/, tests/ and apps/, with generated output directories (any path
component "results") and __pycache__ excluded:
  F1  nom7 vs current: the set of differing / missing / extra files equals MESH-007's file list
      exactly (A3 §2; the same list build_nom7.sh applies) -- nothing else was removed, nothing
      MESH-007 changed was kept.
  F2  nom7 vs BASE: every differing / missing / extra file is attributed to a later, separately
      gated phase (its repository path appears in the evidence of P12-DIFF-002, P12-GRAD-002 or
      P12-ASAN-001), and none of the files MESH-007 alone changed differs from BASE.
  F3  MeshGeometry.{hpp,cpp}, nom7 vs BASE: insertions only, and no inserted block is marked
      "P12-MESH-007".
(F4, the 0-warning build, is read from build_nom7.sh's own log.)
Exit 0 only if F1-F3 hold.
"""
import difflib
import filecmp
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from g93_inputs import MESH007_FILES, evidence_index  # noqa: E402

ROOTS = ("src", "include", "tests", "apps")
REMOVED = {
    "include/cfd/mesh/MeshMotion.hpp", "src/mesh/MeshMotion.cpp",
    "include/cfd/pressure_velocity/AlePISO.hpp", "src/pressure_velocity/AlePISO.cpp",
    "src/pressure_velocity/PisoStep.hpp", "tests/support/MeshMotionCases.hpp",
    "tests/unit/mesh/test_mesh_motion.cpp", "tests/unit/discretization/test_ale_operators.cpp",
    "tests/solver/piso/test_ale_piso.cpp",
}
# the later test that needs the motion API, removed from nom7 by A3 §2 (GRAD-002 A2's test)
LATER_REMOVED = {"tests/unit/discretization/test_gradient_boundary_consistency.cpp"}
SHARED = {"include/cfd/mesh/MeshGeometry.hpp", "src/mesh/MeshGeometry.cpp",
          "tests/unit/discretization/CMakeLists.txt"}


def listing(root):
    out = set()
    for top in ROOTS:
        for d, dirs, files in os.walk(os.path.join(root, top)):
            dirs[:] = [x for x in dirs if x not in ("results", "__pycache__")]
            for f in files:
                out.add(os.path.relpath(os.path.join(d, f), root).replace(os.sep, "/"))
    return out


def differing(a_root, b_root):
    la, lb = listing(a_root), listing(b_root)
    same = la & lb
    changed = {f for f in same
               if not filecmp.cmp(os.path.join(a_root, f), os.path.join(b_root, f), shallow=False)}
    return la - lb, lb - la, changed


def main():
    base, nom7, cur = sys.argv[1], sys.argv[2], sys.argv[3]
    ok = True
    only_nom7, only_cur, changed = differing(nom7, cur)
    observed = only_nom7 | only_cur | changed
    expected = set(MESH007_FILES) | LATER_REMOVED
    f1 = observed == expected and not only_nom7 and only_cur == REMOVED | LATER_REMOVED
    print(f"F1 nom7 vs current: {len(changed)} changed, {len(only_cur)} absent from nom7, "
          f"{len(only_nom7)} only in nom7; expected set {len(expected)} files -> {'PASS' if f1 else 'FAIL'}")
    for f in sorted(observed - expected):
        print(f"   unexpected difference: {f}")
    for f in sorted(expected - observed):
        print(f"   expected but not different: {f}")
    ok &= f1

    only_n, only_b, changed_b = differing(nom7, base)
    docs = evidence_index(cur)
    restored = set(MESH007_FILES) - REMOVED - SHARED
    f2 = True
    print(f"F2 nom7 vs BASE: {len(changed_b)} changed, {len(only_n)} only in nom7, {len(only_b)} only in BASE "
          f"(evidence files indexed: {len(docs)})")
    for kind, files in (("changed", changed_b), ("only in nom7", only_n), ("only in BASE", only_b)):
        for f in sorted(files):
            hit = next((p for p, text in docs if f in text), None)
            flag = ""
            if f in restored:
                flag = "  <-- a MESH-007-only file differs from BASE"
                f2 = False
            if hit is None:
                f2 = False
            print(f"   {kind:12s} {f}: {'attributed to ' + hit if hit else 'UNATTRIBUTED'}{flag}")
    print(f"F2 -> {'PASS' if f2 else 'FAIL'}")
    ok &= f2

    f3 = True
    for rel in ("include/cfd/mesh/MeshGeometry.hpp", "src/mesh/MeshGeometry.cpp"):
        b = open(os.path.join(base, rel)).read().splitlines()
        n = open(os.path.join(nom7, rel)).read().splitlines()
        for op, i1, i2, j1, j2 in difflib.SequenceMatcher(a=b, b=n, autojunk=False).get_opcodes():
            if op == "equal":
                continue
            block = n[j1:j2]
            marked = any("P12-MESH-007" in line for line in block)
            good = op == "insert" and not marked
            f3 &= good
            first = next((line.strip() for line in block if line.strip()), "")
            print(f"F3 {rel}: {op} BASE {i1 + 1}-{i2} / nom7 {j1 + 1}-{j2} ({j2 - j1} lines) "
                  f"first '{first[:70]}' names: {sorted({w for w in ('boundaryLineIntersection', 'BoundaryLineIntersection', 'boundaryInwardStencil', 'BoundaryInwardStencil', 'P12-GRAD-002', 'P12-DIFF-002') if any(w in line for line in block)})}"
                  f"{'' if good else '  <-- NOT an unmarked insertion'}")
    print(f"F3 -> {'PASS' if f3 else 'FAIL'}")
    ok &= f3
    print(f"FIDELITY {'PASS' if ok else 'FAIL'}")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
