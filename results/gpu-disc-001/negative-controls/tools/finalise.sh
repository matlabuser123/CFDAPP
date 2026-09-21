#!/usr/bin/env bash
# GPU-DISC-001P Steps 3-5 -- everything that must be true AFTER the campaign.
#
#   1. CUDA diagnostics on the restored tree  (the mutations touched indexing,
#      buffer layout, memory ownership and 2D/3D contracts, so the brief's rule
#      applies: the sanitizers must not have been run only BEFORE a campaign)
#   2. all 15 GPU-DISC differential gates green again
#   3. baseline integrity + full repository regression
#
# Each step's failure is fatal: a gate that closes on a partially-verified tree
# is not closed.
set -uo pipefail
ROOT=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
EVID=$ROOT/results/gpu-disc-001/negative-controls
TOOLS=$EVID/tools
cd "$ROOT"

echo "################ 1. CUDA diagnostics on the restored tree ################"
bash "$TOOLS/post_restoration_diagnostics.sh" > "$EVID/cuda-diagnostics/run.log" 2>&1
diagRc=$?
tail -25 "$EVID/cuda-diagnostics/run.log"
echo "  -> rc=$diagRc"
[ $diagRc -ne 0 ] && { echo "DIAGNOSTICS FAILED -- stopping"; exit 1; }

echo ""
echo "################ 2. all 15 GPU-DISC differential gates ################"
bash "$ROOT/results/gpu-disc-001/full-solve-equivalence/tools/all_gates.sh" \
  > "$EVID/restoration/final_all_gates.log" 2>&1
gatesRc=$?
tail -20 "$EVID/restoration/final_all_gates.log"
echo "  -> rc=$gatesRc"
[ $gatesRc -ne 0 ] && { echo "GATES FAILED -- stopping"; exit 1; }

echo ""
echo "################ 3. baseline integrity + full regression ################"
bash "$TOOLS/regression.sh"
regRc=$?
echo "  -> rc=$regRc"

echo ""
if [ $regRc -eq 0 ]; then
  echo "GPU-DISC-001P FINALISATION: PASS"
else
  echo "GPU-DISC-001P FINALISATION: FAIL"
fi
exit $regRc
