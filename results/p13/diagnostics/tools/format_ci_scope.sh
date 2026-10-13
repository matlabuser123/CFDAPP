#!/usr/bin/env bash
# The project's format gate, at the scope CI actually uses (.github/workflows/
# ci.yml: include src apps tests). The whole-tree scan in final.sh was wider
# than the gate and flagged benchmarks/, cuda/ and results/ evidence tools that
# have never been inside it -- recorded here rather than silently narrowed.
cd /mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
find include src apps tests \( -name '*.cpp' -o -name '*.hpp' -o -name '*.h' \) -print0 |
  xargs -0 clang-format --dry-run --Werror 2>&1 | grep -c "error:" | \
  xargs -I{} echo "clang-format errors in include/ src/ apps/ tests/: {}"
