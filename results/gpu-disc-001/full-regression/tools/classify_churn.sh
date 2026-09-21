#!/usr/bin/env bash
# GPU-DISC-001R -- classify every modified generated output.
#
# The brief requires "no benchmark wall-time churn remains in tracked validation
# output" and "zero unexplained changes". ~50 files under results/validation/
# are modified, and the question is whether they are timing noise (every full
# ctest run rewrites them) or a real numerical change -- which would be a
# GPU-DISC regression in the CPU reference and a failed gate.
#
# P12-MESH-005 already built the instrument for this. It needs a pre-phase
# snapshot; every GPU-DISC change is UNCOMMITTED, so HEAD is exactly that.
#
# --restore copies back only RUNTIME-ONLY files. VALUES and NEW are never
# touched, so a genuine numerical change cannot be silently reverted.
set -uo pipefail
ROOT=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
EVID=$ROOT/results/gpu-disc-001/full-regression/repository-status
SNAP=/tmp/gpudisc-head-snapshot
cd "$ROOT"
mkdir -p "$EVID"

echo "=== extract HEAD as the pre-GPU-DISC snapshot ==="
rm -rf "$SNAP"
mkdir -p "$SNAP"
git archive HEAD | tar -x -C "$SNAP"
echo "  HEAD $(git rev-parse --short HEAD) -> $SNAP"
echo "  validation files in snapshot: $(find "$SNAP/results/validation" -type f 2>/dev/null | wc -l)"

echo ""
echo "=== classify (report only) ==="
python3 results/p12-mesh-005/tools/classify_generated_outputs.py "$SNAP" \
  > "$EVID/generated-output-classification.txt" 2>&1
rc=$?
echo "  rc=$rc"
grep -cE "^IDENTICAL" "$EVID/generated-output-classification.txt" \
  | xargs -I{} echo "  IDENTICAL    {}"
grep -cE "^RUNTIME-ONLY" "$EVID/generated-output-classification.txt" \
  | xargs -I{} echo "  RUNTIME-ONLY {}"
grep -cE "^VALUES" "$EVID/generated-output-classification.txt" \
  | xargs -I{} echo "  VALUES       {}"
grep -cE "^NEW" "$EVID/generated-output-classification.txt" \
  | xargs -I{} echo "  NEW          {}"

echo ""
echo "=== any VALUES difference? (a real numerical change would be a FAILED GATE) ==="
grep -A4 "^VALUES" "$EVID/generated-output-classification.txt" | head -40 \
  || echo "  none"

echo ""
echo "=== tail of the classification ==="
tail -12 "$EVID/generated-output-classification.txt"
