#!/usr/bin/env bash
# GPU-DISC-001R Phase A -- the final-state repository audit.
#
# Captured BEFORE anything is built or run, so the tree this gate qualifies is
# recorded exactly as it stood.
set -uo pipefail
ROOT=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
EVID=$ROOT/results/gpu-disc-001/full-regression
cd "$ROOT"
mkdir -p "$EVID/repository-status"

echo "=== HEAD ==="
git rev-parse HEAD
git log -1 --format="%h %ad %s" --date=iso

echo ""
echo "=== working-tree status (porcelain) ==="
git status --porcelain > "$EVID/repository-status/git-status.txt"
echo "  tracked modifications: $(grep -c '^ M' "$EVID/repository-status/git-status.txt")"
echo "  untracked entries:     $(grep -c '^??' "$EVID/repository-status/git-status.txt")"
echo "  staged:                $(grep -cE '^[MADRC] ' "$EVID/repository-status/git-status.txt")"
echo "  deleted:               $(grep -c '^ D' "$EVID/repository-status/git-status.txt")"

echo ""
echo "=== tracked modifications ==="
grep '^ M' "$EVID/repository-status/git-status.txt" | sed 's/^/  /'

echo ""
echo "=== untracked entries (top level of each) ==="
grep '^??' "$EVID/repository-status/git-status.txt" | sed 's/^/  /' | head -40

echo ""
echo "=== diff --stat (tracked files only) ==="
git diff --stat | tee "$EVID/repository-status/git-diff-stat.txt" | tail -20

echo ""
echo "=== full diff preserved ==="
git diff > "$EVID/repository-status/git-diff.txt"
echo "  $(wc -l < "$EVID/repository-status/git-diff.txt") lines -> repository-status/git-diff.txt"

echo ""
echo "=== does any tracked source still contain a mutation marker? ==="
# The negative-control campaign wrote "MUTATED" into sources it changed; the
# instrumentation gates never did. Any hit in src/ include/ cuda/ is a finding.
hits=$(grep -rn "MUTATED" src/ include/ cuda/ apps/ 2>/dev/null | wc -l)
echo "  'MUTATED' occurrences in src/ include/ cuda/ apps/: $hits"
[ "$hits" -gt 0 ] && grep -rn "MUTATED" src/ include/ cuda/ apps/ | sed 's/^/    /'

echo ""
echo "=== negative-control / qualification build options enabled? ==="
grep -iE "NEGATIVE_CONTROL|MUTATION|CFDAPP_[A-Z_]*MUTAT" CMakeLists.txt src/CMakeLists.txt \
  cuda/CMakeLists.txt 2>/dev/null | sed 's/^/  /' || echo "  none defined anywhere"

echo ""
echo "=== TODO.md Integration section ==="
sed -n '/^## Integration/,/^Evidence:/p' TODO.md | sed 's/^/  /'

echo ""
echo "=== TODO.md Status block ==="
sed -n '/^# Status/,/^---/p' TODO.md | sed 's/^/  /'

echo ""
echo "=== CMake configuration of the existing production build ==="
grep -E "^(CMAKE_BUILD_TYPE|CMAKE_CUDA_COMPILER|CMAKE_CXX_COMPILER|CFDAPP_ENABLE_CUDA|CFDAPP_ENABLE_OPENMP|BUILD_TESTING):" \
  build/cuda/CMakeCache.txt | sed 's/^/  /'

echo ""
echo "=== test count in the existing build ==="
ctest --test-dir build/cuda -N 2>/dev/null | tail -2 | sed 's/^/  /'
