#!/usr/bin/env bash
cd /mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
clang-format --version
for f in $(find include src apps tests \( -name '*.cpp' -o -name '*.hpp' -o -name '*.h' \)); do
  clang-format --style=file "$f" | diff -q - "$f" >/dev/null || echo "FORMAT: $f"
done
