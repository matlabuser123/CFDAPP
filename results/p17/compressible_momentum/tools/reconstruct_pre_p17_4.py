#!/usr/bin/env python3
"""Recover the PRE-P17.4 text of the two files P17.4 was allowed to touch, and PROVE the
recovery against the SHA-256 values the freeze recorded before any P17.4 code existed.

Why this exists: the gate's original delta table used `git diff --numstat HEAD`, but HEAD is
2026-09-21 and 286 files are uncommitted against it, so that number is the union of every
uncommitted phase. To state P17.4's OWN delta, the pre-P17.4 text is needed. Nothing is
committed and no stash is created; the text is rebuilt by reverse-applying the recorded
edits, and the frozen hash is what makes the result trustworthy rather than plausible.
A reconstruction that does not hash to the frozen value is discarded, not used.
"""
import hashlib
import os
import sys

ROOT = "/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp"
OUT = os.path.join(ROOT, "results/p17/compressible_momentum/controls/pre_p17_4")

FROZEN = {
    "include/cfd/compressible/CompressibleRelaxedMomentum.hpp":
        "5d97510fbac1f38c2dd0b5051a3cdccd22b3d960e9b3187e9a14444cc22a0cc5",
    "src/compressible/CompressibleRelaxedMomentum.cpp":
        "f44c8da4175f228f2b67bc225728e621d00534b2acc3f34cb028bccccbf57a4e",
}


def current(rel):
    with open(os.path.join(ROOT, rel), "r", newline="") as f:
        return f.read()


def undo_header(text):
    """Remove the P17.4 parameter and its comment block; restore the closing ");"."""
    lines = text.split("\n")
    start = next(i for i, l in enumerate(lines)
                 if l.strip().startswith("// P17.4: include the DILATATIONAL"))
    end = next(i for i, l in enumerate(lines)
               if l.strip() == "bool includeDilatationalStress = false);")
    prev = "const cfd::fields::VectorField* momentumSource = nullptr,"
    assert lines[start - 1].strip() == prev, lines[start - 1]
    return "\n".join(lines[:start - 1]
                     + ["    const cfd::fields::VectorField* momentumSource = nullptr);"]
                     + lines[end + 1:])


def undo_source(text):
    """Remove the include, the parameter, and the wiring block."""
    lines = text.split("\n")
    out, i = [], 0
    while i < len(lines):
        l = lines[i]
        if l == '#include "cfd/compressible/DilatationalStress.hpp"':
            i += 1
            continue
        if (l.rstrip().endswith("const VectorField* momentumSource,")
                and lines[i + 1].strip() == "bool includeDilatationalStress) {"):
            out.append(l.replace("const VectorField* momentumSource,",
                                 "const VectorField* momentumSource) {"))
            i += 2
            continue
        if l.strip().startswith("// P17.4: the dilatational part"):
            j = i
            while lines[j].strip() != "}":
                j += 1
            # The blank line after the block belonged to the ORIGINAL file -- it separated the
            # momentumSource block from the time-derivative comment -- so it is KEPT. Dropping
            # it was the first reconstruction attempt and it failed the hash, which is exactly
            # what the hash is for.
            i = j + 1
            continue
        out.append(l)
        i += 1
    return "\n".join(out)


def main():
    os.makedirs(OUT, exist_ok=True)
    ok = True
    for rel, undo in (("include/cfd/compressible/CompressibleRelaxedMomentum.hpp", undo_header),
                      ("src/compressible/CompressibleRelaxedMomentum.cpp", undo_source)):
        text = undo(current(rel))
        got = hashlib.sha256(text.encode()).hexdigest()
        want = FROZEN[rel]
        good = got == want
        ok = ok and good
        print(f"  {'PROVEN  ' if good else 'MISMATCH'}  {rel}")
        print(f"      reconstructed {got}")
        print(f"      frozen        {want}")
        if good:
            with open(os.path.join(OUT, os.path.basename(rel)), "w", newline="") as f:
                f.write(text)
    print("  ALL RECONSTRUCTIONS PROVEN AGAINST THE FREEZE" if ok
          else "  *** RECONSTRUCTION FAILED -- the delta below is NOT trustworthy ***")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
