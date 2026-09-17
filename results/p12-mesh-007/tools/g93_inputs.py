#!/usr/bin/env python3
"""P12-MESH-007 G9.3 (Amendment A3): no existing case, fixture or test input is modified BY MESH-007.

usage: g93_inputs.py <BASE root> <nom7 root> <current root>

Every path under cases/ and tests/data/ is compared, with generated output directories (any path
component named "results") and __pycache__ excluded.

  (1) MESH-007's own changes are exactly the files A3 §2 lists (the nom7 removal and restore
      lists). None of them is a case, fixture or test input. This is checked against that list.
  (2) nom7 vs current, cases/ and tests/data/: must be IDENTICAL. nom7 is the current tree minus
      MESH-007, so a difference here would be a case or test input MESH-007 touched.
  (3) BASE vs current, cases/ and tests/data/: every added, removed or changed file must be
      attributed to a later, separately gated phase. A file is attributed when its repository
      path appears in the evidence of P12-DIFF-002, P12-GRAD-002 or P12-ASAN-001 (results/...,
      text files). The first evidence file naming it is printed.
Exit 0 only if (1)-(3) all hold.
"""
import filecmp
import os
import sys

MESH007_FILES = [
    "include/cfd/mesh/MeshMotion.hpp", "src/mesh/MeshMotion.cpp",
    "include/cfd/pressure_velocity/AlePISO.hpp", "src/pressure_velocity/AlePISO.cpp",
    "src/pressure_velocity/PisoStep.hpp", "tests/support/MeshMotionCases.hpp",
    "tests/unit/mesh/test_mesh_motion.cpp", "tests/unit/discretization/test_ale_operators.cpp",
    "tests/solver/piso/test_ale_piso.cpp",
    "include/cfd/mesh/Cell.hpp", "include/cfd/mesh/Face.hpp", "include/cfd/mesh/Mesh.hpp",
    "src/mesh/Mesh.cpp", "include/cfd/discretization/TimeDerivative.hpp",
    "src/discretization/TimeDerivative.cpp", "include/cfd/physics/MassFlux.hpp",
    "src/physics/MassFlux.cpp", "include/cfd/pressure_velocity/TransientMomentum.hpp",
    "src/pressure_velocity/TransientMomentum.cpp", "src/pressure_velocity/PISO.cpp",
    "include/cfd/solver/TransientSolver.hpp", "src/solver/TransientSolver.cpp",
    "src/CMakeLists.txt", "tests/solver/piso/CMakeLists.txt", "tests/unit/mesh/CMakeLists.txt",
    "include/cfd/mesh/MeshGeometry.hpp", "src/mesh/MeshGeometry.cpp",
    "tests/unit/discretization/CMakeLists.txt",
]
INPUT_ROOTS = ("cases", "tests/data")
EVIDENCE = ("results/p12-diff-002", "results/p12-grad-002", "results/p12-asan-001")
TEXT = (".md", ".log", ".sh", ".py", ".txt", ".diff", ".patch", ".cpp")


def listing(root):
    out = set()
    for top in INPUT_ROOTS:
        base = os.path.join(root, top)
        for d, dirs, files in os.walk(base):
            dirs[:] = [x for x in dirs if x not in ("results", "__pycache__")]
            for f in files:
                out.add(os.path.relpath(os.path.join(d, f), root).replace(os.sep, "/"))
    return out


def evidence_index(current):
    docs = []
    for top in EVIDENCE:
        for d, _, files in os.walk(os.path.join(current, top)):
            for f in files:
                if f.endswith(TEXT):
                    p = os.path.join(d, f)
                    try:
                        with open(p, errors="replace") as fh:
                            docs.append((os.path.relpath(p, current), fh.read()))
                    except OSError:
                        pass
    return docs


def self_test(base, nom7, cur):
    """(2) and (3) must fail on copies with a planted change (NOT a gate item)."""
    import shutil
    import subprocess
    import tempfile
    tmp = tempfile.mkdtemp(prefix="g93_selftest_")
    ign = shutil.ignore_patterns("results", "__pycache__")
    good = True
    try:
        for name, root in (("nom7", nom7), ("cur", cur)):
            for top in INPUT_ROOTS:
                shutil.copytree(os.path.join(root, top), os.path.join(tmp, name, top), ignore=ign)
        # the evidence index must still be read from the real tree
        os.symlink(os.path.join(cur, "results"), os.path.join(tmp, "cur", "results"))
        n7, cu = os.path.join(tmp, "nom7"), os.path.join(tmp, "cur")
        r = subprocess.run([sys.executable, __file__, base, n7, cu], capture_output=True, text=True)
        print(f"  unmodified copies: exit {r.returncode} (must be 0)")
        good &= r.returncode == 0
        victim = os.path.join(n7, "cases", "lid_driven_cavity", "solver.json")
        with open(victim, "a") as fh:
            fh.write(" ")
        r = subprocess.run([sys.executable, __file__, base, n7, cu], capture_output=True, text=True)
        flagged = "DIFFERENT cases/lid_driven_cavity/solver.json" in r.stdout
        print(f"  (2) a nom7 case file changed: exit {r.returncode}, flagged {flagged} (must be 1, True)")
        good &= r.returncode == 1 and flagged
        with open(victim, "rb") as fh:
            data = fh.read()
        with open(victim, "wb") as fh:
            fh.write(data[:-1])
        planted = os.path.join(cu, "tests", "data", "g93_selftest_unattributed.json")
        with open(planted, "w") as fh:
            fh.write("{}\n")
        shutil.copy(planted, os.path.join(n7, "tests", "data", "g93_selftest_unattributed.json"))
        r = subprocess.run([sys.executable, __file__, base, n7, cu], capture_output=True, text=True)
        flagged = "added    tests/data/g93_selftest_unattributed.json: UNATTRIBUTED" in r.stdout
        print(f"  (3) an input added in both trees, named by no evidence: exit {r.returncode}, flagged {flagged} "
              f"(must be 1, True)")
        good &= r.returncode == 1 and flagged
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    print(f"G9.3 SELF-TEST {'PASS' if good else 'FAIL'}")
    return 0 if good else 1


def main():
    base, nom7, cur = sys.argv[1], sys.argv[2], sys.argv[3]
    if "--self-test" in sys.argv[4:]:
        return self_test(base, nom7, cur)
    ok = True
    print("## (1) MESH-007's own file list contains no case, fixture or test input")
    bad = [f for f in MESH007_FILES if f.startswith(INPUT_ROOTS)]
    print(f"  {len(MESH007_FILES)} files; under cases/ or tests/data/: {len(bad)} {bad}")
    ok &= not bad
    print("## (2) nom7 vs current, cases/ and tests/data/")
    ln, lc = listing(nom7), listing(cur)
    diff2 = sorted((ln ^ lc) | {f for f in ln & lc
                                if not filecmp.cmp(os.path.join(nom7, f), os.path.join(cur, f), shallow=False)})
    print(f"  files: nom7 {len(ln)}, current {len(lc)}; differing {len(diff2)}")
    for f in diff2:
        print(f"  DIFFERENT {f}")
    ok &= not diff2
    print("## (3) BASE vs current, cases/ and tests/data/: every change attributed to a later phase")
    lb = listing(base)
    added, removed = sorted(lc - lb), sorted(lb - lc)
    changed = sorted(f for f in lb & lc
                     if not filecmp.cmp(os.path.join(base, f), os.path.join(cur, f), shallow=False))
    print(f"  files: BASE {len(lb)}, current {len(lc)}; added {len(added)}, removed {len(removed)}, "
          f"changed {len(changed)}")
    docs = evidence_index(cur)
    print(f"  evidence text files indexed: {len(docs)}")
    unattributed = 0
    for kind, files in (("added", added), ("removed", removed), ("changed", changed)):
        for f in files:
            hit = next((p for p, text in docs if f in text), None)
            if hit is None:
                unattributed += 1
            print(f"  {kind:8s} {f}: {'attributed to ' + hit if hit else 'UNATTRIBUTED'}")
    ok &= unattributed == 0
    print(f"G9.3 {'PASS' if ok else 'FAIL'} (unattributed {unattributed})")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
