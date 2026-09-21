#!/usr/bin/env bash
# GPU-DISC-001P Step 5 -- full regression on the restored tree.
#
# Build freshness and the full baseline source hash set are recorded BEFORE and
# AFTER ctest. For this gate the "before" set matters twice over: it is also the
# proof that no mutation artifact survived the campaign, so it is compared
# directly against baseline-sha256.txt, recorded before the first mutation.
set -uo pipefail
ROOT=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
EVID=$ROOT/results/gpu-disc-001/negative-controls/regression
TOOLS=$ROOT/results/gpu-disc-001/negative-controls/tools
BASE=$ROOT/results/gpu-disc-001/negative-controls/baseline-sha256.txt
mkdir -p "$EVID"
cd "$ROOT"

FRESH=$EVID/regression_freshness.log
: > "$FRESH"
{
  echo "=== test command ==="
  echo "ctest --test-dir build/cuda --output-on-failure"
  echo ""
  echo "=== build identity BEFORE ctest ==="
  ninja -C build/cuda
  sha256sum build/cuda/cuda/libcfdcuda.a build/cuda/src/libcfdcore.a
  echo ""
  echo "=== every mutable production source BEFORE ctest ==="
  bash "$TOOLS/baseline_sha.sh"
} >> "$FRESH" 2>&1

echo "=== no mutation artifact remains? ==="
bash "$TOOLS/baseline_sha.sh" > "$EVID/post-campaign-sha256.txt"
if diff -u "$BASE" "$EVID/post-campaign-sha256.txt" > "$EVID/sha256-diff.txt"; then
  echo "  IDENTICAL to baseline-sha256.txt -- every production source restored byte for byte"
else
  echo "  !! DIFFERS from baseline-sha256.txt:"
  cat "$EVID/sha256-diff.txt"
  echo "  the campaign left something behind; this gate stays open"
  exit 1
fi

echo ""
echo "=== full regression ==="
ctest --test-dir build/cuda --output-on-failure > "$EVID/regression.log" 2>&1
RC=$?
{
  echo ""
  echo "=== build identity AFTER ctest ==="
  ninja -C build/cuda
  sha256sum build/cuda/cuda/libcfdcuda.a build/cuda/src/libcfdcore.a
  echo ""
  echo "=== every mutable production source AFTER ctest ==="
  bash "$TOOLS/baseline_sha.sh"
  echo ""
  echo "ctest exit=$RC"
} >> "$FRESH" 2>&1

grep -E "tests passed|tests failed|Total Test time" "$EVID/regression.log"
echo "--- freshness ---"
grep -E "no work to do|ctest exit" "$FRESH"
exit $RC
