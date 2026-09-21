#!/usr/bin/env bash
# PROOF 1, corrected.
#
# The first version was wrong twice. It built its file list with
# `grep -oE "^[^:]+\.(cpp|hpp)"` over clang-format's output, which also matches
# the SOURCE LINES clang-format echoes back -- so the list contained entries
# like `#include` that are not files at all. And it compared with `diff -w`,
# which ignores whitespace WITHIN a line but still reports a difference when
# clang-format JOINS two lines, which is most of what it does here. Both made
# every file look changed, contradicting the byte-identical libcfdcore.a.
#
# Corrected: the file list comes from the snapshot directory itself, and the
# comparison strips all whitespace, which is what "reflowed but otherwise
# identical" actually means.
set -uo pipefail
ROOT=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
SNAP=/tmp/preformat
cd "$ROOT"
bad=0; n=0
while IFS= read -r g; do
  f=${g#"$SNAP/"}
  [ -f "$ROOT/$f" ] || { echo "  MISSING: $f"; bad=1; continue; }
  n=$((n+1))
  a=$(tr -d '[:space:]' < "$g" | sha256sum | cut -d' ' -f1)
  b=$(tr -d '[:space:]' < "$ROOT/$f" | sha256sum | cut -d' ' -f1)
  if [ "$a" != "$b" ]; then echo "  NON-WHITESPACE CHANGE: $f"; bad=1; fi
done < <(find "$SNAP" -type f \( -name '*.cpp' -o -name '*.hpp' \))
echo "  compared $n files, whitespace stripped"
[ $bad -eq 0 ] && echo "  IDENTICAL -- every change is whitespace only" \
               || echo "  A NON-WHITESPACE CHANGE WAS FOUND"
exit $bad
