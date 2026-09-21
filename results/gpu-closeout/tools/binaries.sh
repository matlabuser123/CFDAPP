#!/usr/bin/env bash
set -uo pipefail
cd /mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
for f in \
  results/gpu-disc-001/full-regression/clean-build/warnings.log \
  results/gpu-disc-001/negative-controls/regression/sha256-diff.txt \
  results/gpu-disc-001/performance/profiling/gpu-disc-320.nsys-rep \
  results/gpu-disc-001/performance/profiling/gpu-disc-320.sqlite \
  results/gpu-disc-001/performance/scaling/openmp/link-1.log \
  results/gpu-pipe-001/final-residency/equivalence/production.log.build \
  results/gpu-pipe-001/final-residency/regression/15-clang-format.log ; do
  printf "%-78s %8s B  %s\n" "$f" "$(stat -c %s "$f")" "$(file -b "$f" | cut -c1-42)"
done
echo ""
echo "=== why are the small ones 'binary'? first non-ASCII byte ==="
for f in results/gpu-disc-001/full-regression/clean-build/warnings.log \
         results/gpu-disc-001/negative-controls/regression/sha256-diff.txt \
         results/gpu-pipe-001/final-residency/regression/15-clang-format.log ; do
  echo "--- $f"
  LC_ALL=C grep -naxv '.*' "$f" 2>/dev/null | head -2 | cut -c1-110
  head -c 200 "$f" | LC_ALL=C tr -c '[:print:][:space:]' '.' | head -3
done
echo ""
echo "=== the .log.build files (my harness's compiler stderr capture) ==="
find results/gpu-pipe-001 -name '*.log.build' -printf "%8s B  %p\n"
