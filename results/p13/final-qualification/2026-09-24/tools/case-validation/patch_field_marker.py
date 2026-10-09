#!/usr/bin/env python3
"""Control N1b's one edit: rename the marker the GUI's scraper reads.

    fieldLabel:   field "<name>"   ->   key "<name>"

`field` -> `key` is the most ordinary wording change imaginable, and it is
exactly the token `fieldForMessage()` (apps/gui/SimulationControllerEditing.cpp)
searches for. Nothing else in the message changes.

Kept in its own file rather than inside the shell script: a C++ string literal
full of escaped quotes does not survive a shell heredoc reliably, and the first
attempt at this control was lost to exactly that.

usage: patch_field_marker.py <JsonUtil.cpp> [--revert]
"""
import io
import sys

ORIGINAL = 'return field.empty() ? std::string() : ("field \\"" + std::string(field) + "\\" ");'
PATCHED = 'return field.empty() ? std::string() : ("key \\"" + std::string(field) + "\\" ");'


def main():
    path = sys.argv[1]
    revert = "--revert" in sys.argv
    old, new = (PATCHED, ORIGINAL) if revert else (ORIGINAL, PATCHED)
    text = io.open(path, encoding="utf-8").read()
    if text.count(old) != 1:
        sys.stderr.write("PATCH TARGET NOT UNIQUE (%d matches) in %s\n" % (text.count(old), path))
        return 1
    io.open(path, "w", encoding="utf-8", newline="\n").write(text.replace(old, new, 1))
    return 0


if __name__ == "__main__":
    sys.exit(main())
