#!/usr/bin/env python3
"""Measure how far a regenerated results/validation report moved, KEY BY KEY.

WHY THIS REPLACED A ONE-LINE awk PIPELINE. The first version of restore_validation_noise.sh
measured the magnitude by grepping `"l2"` lines out of `git diff -U0` and pairing them with
`paste - -`. That is only correct when the diff alternates `-`/`+`. When a hunk GROUPS its removed
lines before its added ones -- which it does whenever several adjacent keys change together -- the
pairing lines up grid 1's old value with grid 2's old value, and reports the REFINEMENT RATIO
BETWEEN DIFFERENT GRIDS as if it were drift.

Demonstrated on a synthetic diff whose true answer was 1.0e-09:

    alternating -/+   reported 1.000e-09    correct
    grouped --/++     reported 3.000e+00    wrong by nine orders of magnitude

and that is exactly how P18.5's run first reported `max relative |delta l2| = 4.632e-02` against
KNOWN_LIMITATIONS L36's recorded <= 4.1e-07. The L36 bound was never contradicted; the instrument
was broken. A measurement that can be wrong in the alarming direction by nine orders of magnitude
is worse than no measurement, because it manufactures findings.

This parses both JSON documents and compares them by KEY PATH, so a value is only ever compared
with the same value. It reports the largest relative change and where it occurred.

    python3 compare_validation_json.py              # every modified tracked .json under
                                                    # results/validation, old (HEAD) vs working tree
"""

import json
import subprocess
import sys


def flatten(node, prefix=""):
    """Every numeric leaf, keyed by its full path."""
    out = {}
    if isinstance(node, dict):
        for key, value in node.items():
            out.update(flatten(value, f"{prefix}.{key}" if prefix else str(key)))
    elif isinstance(node, list):
        for index, value in enumerate(node):
            out.update(flatten(value, f"{prefix}[{index}]"))
    elif isinstance(node, bool):
        pass  # not a measurement
    elif isinstance(node, (int, float)):
        out[prefix] = float(node)
    return out


# keys whose change is expected and meaningless
TIMING = ("seconds", "runtime_seconds")

# A RELATIVE CHANGE IS MEANINGLESS FOR A QUANTITY SITTING AT THE CONVERGENCE FLOOR.
#
# P18.6's run reported a 5.609e-02 relative change on
#     distorted_mesh_simple_mms.json :: levels[2].errors.continuity.l1
# which looks like a five-order violation of KNOWN_LIMITATIONS L36's 4.1e-07 bound. It is not.
# That key is a CONTINUITY RESIDUAL: its tracked value is 5.741112e-10, against solution error
# norms of 2.07e-04 (u), 2.78e-03 (p) in the SAME file -- six orders of magnitude larger. The 5.6%
# is an absolute move of ~3e-11, which is what an iterative solve stopping at a slightly different
# point under different optimisation produces.
#
# SO THE DENOMINATOR, NOT THE NUMERATOR, IS THE PROBLEM. Dividing by a number that IS the
# convergence floor manufactures a large relative error out of nothing.
#
# The fix is to judge each key against the SCALE OF ITS OWN FILE: a key whose magnitude is below
# FLOOR_FRACTION of the largest magnitude in that file is reported SEPARATELY, by absolute change,
# rather than being allowed to dominate a relative maximum it cannot meaningfully contribute to.
# L36's bound is about SOLUTION norms, and this is what makes the comparison about those.
FLOOR_FRACTION = 1.0e-6

# Structural/metadata leaves: counts and sizes, not measured quantities. They must not
# set the file scale, and a change in one of them is a STRUCTURAL change rather than
# drift -- which the key allow-list in restore_validation_noise.sh would catch anyway.
METADATA = ("cells", "volume", "grid", "level", "levels", "index", "count")


def main():
    changed = subprocess.run(
        ["git", "status", "--porcelain", "--", "results/validation"],
        capture_output=True, text=True, check=True,
    ).stdout.splitlines()
    files = [line[3:].strip() for line in changed if line[3:].strip().endswith(".json")]

    if not files:
        print("no modified tracked .json under results/validation")
        return 0

    worst = (0.0, None, None)
    worst_timing = (0.0, None, None)
    worst_floor = (0.0, None, None)  # absolute change, for keys at the convergence floor
    compared = 0
    missing_keys = 0
    floor_keys = 0

    for path in files:
        old_text = subprocess.run(
            ["git", "show", f"HEAD:{path}"], capture_output=True, text=True,
        )
        if old_text.returncode != 0:
            print(f"  (skipped, not in HEAD: {path})")
            continue
        try:
            old = flatten(json.loads(old_text.stdout))
            with open(path, encoding="utf-8") as handle:
                new = flatten(json.load(handle))
        except (json.JSONDecodeError, OSError) as error:
            print(f"FAIL: cannot parse {path}: {error}")
            return 1

        # THE SCALE OF THIS FILE, over non-timing MEASUREMENT leaves only.
        # `cells`, `volume`, `grid` and the like are METADATA, not magnitudes -- letting a cell
        # COUNT of 1024 set the scale would make every real error norm look like a floor value.
        scale = max((abs(v) for k, v in old.items()
                     if k.split(".")[-1] not in TIMING and k.split(".")[-1] not in METADATA),
                    default=0.0)

        for key, before in old.items():
            if key not in new:
                missing_keys += 1
                continue
            after = new[key]
            compared += 1
            if before == 0.0:
                continue
            leaf = key.split(".")[-1]
            relative = abs(after - before) / abs(before)
            if leaf in TIMING:
                if relative > worst_timing[0]:
                    worst_timing = (relative, path, key)
                continue
            if scale > 0.0 and abs(before) < FLOOR_FRACTION * scale:
                # at the convergence floor for this file: judge by ABSOLUTE change
                floor_keys += 1
                absolute = abs(after - before)
                if absolute > worst_floor[0]:
                    worst_floor = (absolute, path, key)
                continue
            if relative > worst[0]:
                worst = (relative, path, key)

    print(f"files compared     : {len(files)}")
    print(f"numeric keys compared : {compared}")
    print(f"keys absent from the new file : {missing_keys}  (a structural change, not drift)")
    print()

    # THE POSITIVE CONTROL. Zero keys compared would make every number below vacuous.
    if compared == 0:
        print("FAIL: 0 keys compared -- this measurement is vacuous")
        return 1

    if worst[1] is None:
        print("non-timing keys: NONE CHANGED (max relative change = 0)")
    else:
        print(f"max relative change, NON-TIMING : {worst[0]:.3e}")
        print(f"  at {worst[1]}")
        print(f"     {worst[2]}")
    print(f"keys at the convergence floor (< {FLOOR_FRACTION:g} x the file scale) : {floor_keys}")
    if worst_floor[1] is None:
        print("  none of them changed")
    else:
        print(f"  max ABSOLUTE change among them : {worst_floor[0]:.3e}")
        print(f"    at {worst_floor[1]}")
        print(f"       {worst_floor[2]}")
        print("  (a RELATIVE change is not reported for these: the denominator IS the")
        print("   convergence floor, so dividing by it manufactures a large number from nothing)")
    if worst_timing[1] is None:
        print("timing keys: none changed")
    else:
        print(f"max relative change, TIMING     : {worst_timing[0]:.3e}  (expected, meaningless)")
        print(f"  at {worst_timing[1]} :: {worst_timing[2]}")
    print()
    print("KNOWN_LIMITATIONS L36 records raw norms moving <= 4.1e-07 relative and")
    print("Richardson-extrapolated quantities <= 1.1e-05, between Release and Debug builds.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
