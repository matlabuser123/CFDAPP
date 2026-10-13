#!/usr/bin/env python3
"""Strip C++ comments and string literals, leaving code only.

WHY THIS EXISTS. final_integrity.sh asserts that certain things are ABSENT from P18.5's
production files -- no heat release, no equilibrium constant, no clamping, no duplicated
conservation formula. The first version grepped the files as written and reported three
"findings":

    equilibrium  1    from "...they are NOT derived from an equilibrium constant."
    omega        2    from the comment diagram "-> omega_i"
    clamp        2    from "Nothing is clamped." and "EXACT branch, not a clamp"

Every one of them was prose stating that P18.5 does not do that thing. A probe that cannot tell
code from prose ABOUT the code measures the wrong thing, and in this direction it is the dangerous
one: the documentation most likely to name a forbidden concept is the documentation explaining its
absence, so the better the comments, the more false findings.

Line and block comments and string literals are replaced, preserving line structure so reported
line numbers still mean something.

    python3 strip_code.py <in> <out>
"""

import re
import sys


def strip(text):
    def blank(match):
        # keep the newlines so line numbers survive
        return "\n" * match.group(0).count("\n")

    text = re.sub(r"/\*.*?\*/", blank, text, flags=re.S)  # block comments
    text = re.sub(r"//[^\n]*", "", text)  # line comments
    text = re.sub(r'"(\\.|[^"\\\n])*"', '""', text)  # string literals
    text = re.sub(r"'(\\.|[^'\\\n])'", "''", text)  # char literals
    return text


def main():
    if len(sys.argv) != 3:
        print("usage: strip_code.py <in> <out>", file=sys.stderr)
        return 2
    with open(sys.argv[1], encoding="utf-8") as handle:
        text = handle.read()
    out = strip(text)
    if not out.strip():
        print(f"strip_code: {sys.argv[1]} stripped to nothing", file=sys.stderr)
        return 1
    with open(sys.argv[2], "w", encoding="utf-8") as handle:
        handle.write(out)
    return 0


if __name__ == "__main__":
    sys.exit(main())
