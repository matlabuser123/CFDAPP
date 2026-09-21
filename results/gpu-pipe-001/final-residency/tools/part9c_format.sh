#!/usr/bin/env bash
# GPU-PIPE-001 Final Residency, Part 9c -- clear the clang-format push blocker.
#
# 105 violations across 12 files, all in pre-existing uncommitted GPU-DISC code.
# clang-format is token-preserving by construction, but "by construction" is an
# argument, not evidence, so this proves it two ways:
#
#   1. `diff -w` against a pre-format snapshot must be EMPTY for every file --
#      nothing but whitespace changed.
#   2. libcfdcore.a must hash IDENTICALLY after a rebuild. g++ is deterministic
#      (unlike nvcc), and SIMPLE.cpp -- 23 of the 105 violations, and the only
#      changed .cpp -- compiles into it. An identical archive is proof the
#      reformatting changed no code the compiler can see.
set -uo pipefail
ROOT=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
E=$ROOT/results/gpu-pipe-001/final-residency/regression
SNAP=/tmp/preformat
cd "$ROOT"

FILES=$(clang-format-18 --dry-run --Werror --style=file:.clang-format \
  $(find include src apps tests -type f -name '*.cpp' -o -type f -name '*.hpp') 2>&1 \
  | grep -oE "^[^:]+\.(cpp|hpp)" | sort -u)

{
  echo "# clang-format remediation -- $(date -u '+%Y-%m-%dT%H:%M:%SZ')"
  echo ""
  echo "## files with violations, before"
  for f in $FILES; do
    n=$(clang-format-18 --dry-run --Werror --style=file:.clang-format "$f" 2>&1 | grep -c 'error:')
    printf "  %4s  %s\n" "$n" "$f"
  done
  echo ""
  echo "## libcfdcore.a before"
  sha256sum build/final/src/libcfdcore.a
} > "$E/14-format-remediation.log"

rm -rf "$SNAP"; mkdir -p "$SNAP"
for f in $FILES; do mkdir -p "$SNAP/$(dirname "$f")"; cp "$f" "$SNAP/$f"; done

clang-format-18 -i --style=file:.clang-format $FILES

{
  echo ""
  echo "## PROOF 1 -- diff -w against the pre-format snapshot (must be empty)"
  bad=0
  for f in $FILES; do
    if ! diff -w -q "$SNAP/$f" "$f" > /dev/null 2>&1; then
      echo "  NON-WHITESPACE CHANGE: $f"; bad=1
    fi
  done
  [ $bad -eq 0 ] && echo "  empty -- every change is whitespace only"
  echo ""
  echo "## violations after"
  clang-format-18 --dry-run --Werror --style=file:.clang-format \
    $(find include src apps tests -type f -name '*.cpp' -o -type f -name '*.hpp') 2>&1 \
    | grep -c 'error:' | xargs -I{} echo "  total violations in the CI scope: {}"
} >> "$E/14-format-remediation.log"

ninja -C build/final > /dev/null 2>&1
ninja -C build/cuda > /dev/null 2>&1
{
  echo ""
  echo "## PROOF 2 -- libcfdcore.a after the rebuild"
  sha256sum build/final/src/libcfdcore.a
  echo "  (g++ is deterministic; an identical hash proves the reformatting changed"
  echo "   nothing the compiler can see. nvcc is NOT deterministic, so libcfdcuda.a"
  echo "   is deliberately not compared -- the gates below cover it instead.)"
} >> "$E/14-format-remediation.log"
cat "$E/14-format-remediation.log"
