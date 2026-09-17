#!/usr/bin/env python3
"""P12-MESH-007 G2.3, supplementary negative controls (NOT a gate criterion).

The frozen G2.3 self-test perturbs only the kernel's first cell volume, so it shows that the volume
comparison can fail. This control shows the same for the other three channels. It writes perturbed
COPIES of the gate's dump files and runs the unchanged instrument (independent_geometry.py) on each
copy; every perturbed copy must be reported FAIL.

  identity     the dumps re-serialized, unperturbed: must PASS, with the gate log's exact ratios
  centroid     kernel centroid of cell 0, x += 1e-9 at every step
  faceCentroid kernel centroid of face 0, x += 1e-9 at every step
  areaVector   kernel area vector of face 0, x += 1e-9 at every step
  orientation  kernel area vector of face 0 negated at every step

1e-9 exceeds every G2.2 bound for these meshes by more than 500x (the largest bound is the 2D
centroid's 32 eps X^3 / V, about 1.8e-12). Python's json writes floats with repr, so the untouched
values round-trip exactly; the identity control checks this.
usage: g23_channel_controls.py <gate-data-dir> <work-dir>
"""
import glob
import json
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
TOOL = os.path.join(HERE, "independent_geometry.py")


def first_vector(values):
    """the first 3-vector of a dumped array, stored either nested [[x, y, z], ...] or flat."""
    if isinstance(values[0], list):
        return values[0], 0
    return values, 0


def perturb(kind, step):
    # (the first version indexed the arrays as flat; the dumps store them nested -- see the
    # CRASHED log kept next to log 27)
    if kind in ("centroid", "faceCentroid", "areaVector"):
        vec, k = first_vector(step[kind])
        vec[k] += 1e-9
    elif kind == "orientation":
        vec, k = first_vector(step["areaVector"])
        for i in range(3):
            vec[k + i] = -vec[k + i]


def main():
    src, work = sys.argv[1], sys.argv[2]
    files = sorted(glob.glob(os.path.join(src, "geometry_*.json")))
    ok = True
    for kind, must_pass in (("identity", True), ("centroid", False), ("faceCentroid", False),
                            ("areaVector", False), ("orientation", False)):
        out = os.path.join(work, kind)
        os.makedirs(out, exist_ok=True)
        for f in files:
            d = json.load(open(f))
            if kind != "identity":
                for step in d["steps"]:
                    perturb(kind, step)
            with open(os.path.join(out, os.path.basename(f)), "w") as fh:
                json.dump(d, fh)
        r = subprocess.run([sys.executable, TOOL, out], capture_output=True, text=True)
        lines = [l for l in r.stdout.splitlines() if l.startswith("G2.3")]
        # per-case lines only: the last line is "G2.3 overall: ..." (the second run counted it --
        # see the MISCOUNTED log kept next to log 27)
        case_lines = [l for l in lines if not l.startswith("G2.3 overall")]
        per_case_fail = sum(1 for l in case_lines if l.endswith("FAIL"))
        overall = lines[-1] if lines else "(no output)"
        good = (r.returncode == 0) if must_pass else (r.returncode == 1 and per_case_fail == len(files))
        ok &= good
        print(f"## {kind}: instrument exit {r.returncode}; {overall}; cases reported FAIL {per_case_fail} of "
              f"{len(files)} -> {'as required' if good else 'NOT AS REQUIRED'}")
        for l in lines[:-1]:
            print("   " + l)
    print("CONTROLS:", "ALL AS REQUIRED" if ok else "NOT AS REQUIRED")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
