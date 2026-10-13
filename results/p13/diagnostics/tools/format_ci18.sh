#!/usr/bin/env bash
# The format gate exactly as CI runs it: clang-format-18, over include src apps
# tests. The default `clang-format` in this WSL image is 14, and ci.yml's own
# comment records that 14/15 and 18 give genuinely different output -- so 14 is
# the wrong instrument for this check.
cd /mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
clang-format-18 --version
echo "--- files clang-format-18 would change ---"
n=0
for f in $(find include src apps tests \( -name '*.cpp' -o -name '*.hpp' -o -name '*.h' \)); do
  clang-format-18 --style=file "$f" | diff -q - "$f" >/dev/null || { echo "FORMAT: $f"; n=$((n+1)); }
done
echo "clang-format-18 violations in the CI scope: $n"
