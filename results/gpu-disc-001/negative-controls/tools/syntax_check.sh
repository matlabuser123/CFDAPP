#!/usr/bin/env bash
# Syntax-check the harnesses improved by GPU-DISC-001P, without touching the
# build tree (a mutation campaign may be running against it).
set -uo pipefail
ROOT=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
CUDA=/usr/local/cuda-12.9
cd "$ROOT"
rc=0
for f in results/gpu-disc-001/full-solve-equivalence/tools/full_solve_equivalence.cpp \
         results/gpu-disc-001/integrated-simple/tools/integrated_simple_equivalence.cpp; do
  echo "--- $f ---"
  if /usr/bin/c++ -Iinclude -Ibuild/cuda/generated/include \
      -Ibuild/cuda/_deps/nlohmann_json-src/include -isystem "$CUDA/include" \
      -O2 -DNDEBUG -std=c++20 -Wall -Wextra -fsyntax-only "$f"; then
    echo "OK"
  else
    rc=1
  fi
done
exit $rc
