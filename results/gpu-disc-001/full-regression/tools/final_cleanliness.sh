#!/usr/bin/env bash
# GPU-DISC-001R -- final repository cleanliness.
#
# The brief does NOT require a clean working tree -- the GPU-DISC
# implementation is intentionally uncommitted. It requires that every change be
# EXPLAINED, in three classes:
#
#   intended GPU-DISC implementation changes
#   intended evidence changes
#   unintended / generated changes        <- must be zero after this script
#
# The generated class is the ~50 results/validation/** files every full ctest
# run rewrites with only timings changed. They were classified RUNTIME-ONLY
# (0 VALUES) in Phase A, and are restored here from the HEAD snapshot. VALUES
# and NEW files are never touched by the restorer.
set -uo pipefail
ROOT=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
EVID=$ROOT/results/gpu-disc-001/full-regression/repository-status
SNAP=/tmp/gpudisc-head-snapshot
cd "$ROOT"

echo "=== re-classify after all the test runs ==="
python3 results/p12-mesh-005/tools/classify_generated_outputs.py "$SNAP" \
  > "$EVID/classification-before-restore.txt" 2>&1
tail -1 "$EVID/classification-before-restore.txt" | sed 's/^/  /'
values=$(grep -cE "^VALUES" "$EVID/classification-before-restore.txt" || true)
echo "  VALUES differences: $values  (must be 0 -- a real numerical change would be a FAILED GATE)"
if [ "$values" -ne 0 ]; then
  grep -A4 "^VALUES" "$EVID/classification-before-restore.txt" | head -30
  echo "  FAILED: a generated output changed NUMERICALLY"
  exit 1
fi

echo ""
echo "=== restore the RUNTIME-ONLY churn ==="
python3 results/p12-mesh-005/tools/classify_generated_outputs.py "$SNAP" --restore \
  > "$EVID/restore.txt" 2>&1
tail -1 "$EVID/restore.txt" | sed 's/^/  /'

echo ""
echo "=== after restore ==="
python3 results/p12-mesh-005/tools/classify_generated_outputs.py "$SNAP" \
  > "$EVID/classification-after-restore.txt" 2>&1
tail -1 "$EVID/classification-after-restore.txt" | sed 's/^/  /'

echo ""
echo "=== tracked modifications now, classified ==="
git status --porcelain > "$EVID/git-status-final.txt"
echo "  total tracked modifications: $(grep -c '^ M' "$EVID/git-status-final.txt")"
echo ""
echo "  --- intended GPU-DISC / GPU-PIPE implementation ---"
grep '^ M' "$EVID/git-status-final.txt" | grep -vE " results/" | sed 's/^/    /'
echo ""
echo "  --- intended evidence (results/) ---"
grep '^ M' "$EVID/git-status-final.txt" | grep -E " results/" | sed 's/^/    /' | head -10
n=$(grep '^ M' "$EVID/git-status-final.txt" | grep -cE " results/")
echo "    ($n tracked files under results/)"
echo ""
echo "  --- untracked: new implementation + new evidence ---"
echo "    new production sources: $(git status --porcelain | grep -cE '^\?\? (cuda|include|src)/')"
echo "    new evidence trees:     $(git status --porcelain | grep -cE '^\?\? results/')"

echo ""
echo "=== no mutation, no temporary instrumentation ==="
echo "  'MUTATED' in production source: $(grep -rc "MUTATED" src/ include/ cuda/ apps/ 2>/dev/null | grep -v ':0' | wc -l) files"
echo "  build directories present:      $(ls -d build/*/ 2>/dev/null | tr '\n' ' ')"

echo ""
echo "=== TODO.md [x] audit: every checked GPU-DISC item ==="
sed -n '/^# 3. GPU-DISC-001/,/^### Gate/p' TODO.md | grep -E '^\* \[' | sed 's/^/  /'

echo ""
echo "FINAL CLEANLINESS: complete"
