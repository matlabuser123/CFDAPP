#!/usr/bin/env bash
set -u
cd /mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
export PATH=/usr/local/cuda-12.9/bin:$PATH
echo "=== clang-format ==="
CHANGED=$(git diff --name-only HEAD -- '*.cpp' '*.hpp' | grep -v '\.hpp\.in$' || true)
FMT=0
for f in $CHANGED; do
  [ -f "$f" ] || continue
  if ! clang-format --style=file "$f" | diff -q - "$f" >/dev/null; then
    echo "FORMAT: $f"; FMT=$((FMT+1))
  fi
done
echo "format violations: $FMT"
echo "=== build build/final ==="
cmake --build build/final -j 12 2>&1 | tail -25
