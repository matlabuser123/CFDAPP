#!/usr/bin/env python3
"""G5 investigation: write a square-duct case directory derived from the committed cases/duct_3d.

Usage: gen_duct_case.py <out_dir> <n> <axis x|y|z> [key=value ...]

The flow axis gets length 6 and 6n cells; the two cross-flow axes get length 1 and n cells. The
inlet (uniform U = 1 along the flow axis) sits on the min patch of the flow axis, the outlet on its
max patch, and walls on the other four. This is the same permutation as the gate's
ductDefinition() in tests/integration/case/test_3d_production_cases.cpp. case.json, physics.json
and solver.json are copied unchanged. Optional key=value pairs override top-level solver.json keys
(audit experiments only), e.g. velocity_relaxation=0.5, or length=12 (flow-axis length).
"""
import json
import pathlib
import shutil
import sys

REPO = pathlib.Path(__file__).resolve().parents[4]
COMMITTED = REPO / "cases" / "duct_3d"


def main():
    out, n, axis = pathlib.Path(sys.argv[1]), int(sys.argv[2]), "xyz".index(sys.argv[3])
    overrides = dict(a.split("=", 1) for a in sys.argv[4:])
    length = float(overrides.pop("length", 6.0))
    if out.exists():
        shutil.rmtree(out)
    out.mkdir(parents=True)
    for name in ("case.json", "physics.json", "solver.json"):
        shutil.copy(COMMITTED / name, out / name)
    if overrides:
        solver = json.loads((out / "solver.json").read_text())
        for key, value in overrides.items():
            solver[key] = json.loads(value) if value[:1] in "[{\"" else float(value) if any(c in value for c in ".e") else int(value)
        (out / "solver.json").write_text(json.dumps(solver, indent=2) + "\n")
    lengths = [1.0, 1.0, 1.0]
    cells = [n, n, n]
    lengths[axis] = length
    cells[axis] = int(round(length * n))
    (out / "geometry.json").write_text(json.dumps(
        {"type": "box", "length": lengths[0], "height": lengths[1], "depth": lengths[2]}, indent=2) + "\n")
    (out / "mesh.json").write_text(json.dumps(
        {"type": "structured_cartesian", "nx": cells[0], "ny": cells[1], "nz": cells[2]}, indent=2) + "\n")
    committed = json.loads((COMMITTED / "boundaries.json").read_text())["patches"]
    inlet, outlet, wall = committed["xmin"], committed["xmax"], committed["ymin"]
    patches = {}
    for b, (lo, hi) in enumerate((("xmin", "xmax"), ("ymin", "ymax"), ("zmin", "zmax"))):
        if b == axis:
            low = json.loads(json.dumps(inlet))
            low["velocity"]["value"] = [1.0 if c == axis else 0.0 for c in range(3)]
            patches[lo], patches[hi] = low, outlet
        else:
            patches[lo], patches[hi] = wall, wall
    (out / "boundaries.json").write_text(json.dumps({"patches": patches}, indent=2) + "\n")


if __name__ == "__main__":
    main()
