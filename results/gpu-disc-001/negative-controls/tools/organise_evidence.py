#!/usr/bin/env python3
"""GPU-DISC-001P -- file the operator-campaign logs under their subsystem.

The engine writes every control's log into one directory while it runs, because
a campaign that reorganises as it goes is a campaign that can lose evidence
halfway through. This moves each pair into the per-subsystem directory the
brief's evidence structure asks for, using each control's declared `layer`
rather than parsing its id.

Idempotent: a log already filed is left where it is.
"""

import pathlib
import shutil
import sys

TOOLS = pathlib.Path("/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp/results/gpu-disc-001/"
                     "negative-controls/tools")
sys.path.insert(0, str(TOOLS))

from operator_controls import CONTROLS  # noqa: E402

EVID = TOOLS.parent
SRC = EVID / "operators"

# layer -> evidence directory (the brief's recommended structure).
DIRS = {
    "gradients": "gradients",
    "diffusion": "diffusion",
    "convection": "convection",
    "boundary-conditions": "boundary-conditions",
    "momentum-assembly": "momentum-assembly",
    "momentum-response": "momentum-response",
    "rhie-chow": "rhie-chow",
    "build-flags": "build-flags",
}


def main():
    moved = 0
    for control in CONTROLS:
        dest = EVID / DIRS[control["layer"]]
        dest.mkdir(parents=True, exist_ok=True)
        for suffix in ("_mutated.log", "_restored.log"):
            src = SRC / (control["id"] + suffix)
            if src.exists():
                shutil.move(str(src), str(dest / src.name))
                moved += 1
    print(f"filed {moved} logs under their subsystem directories")
    for layer in sorted(set(DIRS.values())):
        d = EVID / layer
        if d.exists():
            print(f"  {layer:22s} {len(list(d.glob('*.log')))} logs")
    # driver.log is the campaign's own transcript, not a control's evidence.
    left = sorted(p.name for p in SRC.glob("*.log") if p.name != "driver.log")
    print(f"  operators/             driver.log + {len(left)} unfiled (expected 0): {left}")
    return 0 if not left else 1


if __name__ == "__main__":
    raise SystemExit(main())
