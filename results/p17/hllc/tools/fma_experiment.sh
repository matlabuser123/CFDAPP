#!/usr/bin/env bash
# P17.7 criterion G3 -- what is FMA contraction actually worth?
#
# The production kernel is built with --fmad=false so that CPU/GPU equality is BITWISE. That is a
# deliberate choice and G3 asks for its cost to be REPORTED rather than assumed. So: flip the flag,
# rebuild, measure, restore, and prove the restore by rebuilding and re-running the bitwise test.
#
# This is a controlled experiment, not a gate criterion. Nothing is weakened either way.
set -uo pipefail
ROOT=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
P17=$ROOT/results/p17/hllc
CM=$ROOT/cuda/CMakeLists.txt
cd "$ROOT" || exit 1
export PATH="/usr/local/cuda-12.9/bin:$PATH"
mkdir -p "$P17/logs"

BEFORE=$(sha256sum "$CM" | awk '{print $1}')
cp "$CM" "$P17/logs/CMakeLists.txt.fma_pristine"

log=$P17/logs/05_fma_experiment.log
{
echo "P17.7 G3 -- the FMA-contraction experiment"
echo "date: $(date -u +%FT%TZ)"
echo "cuda/CMakeLists.txt sha256 before: $BEFORE"
echo ""

echo "### 1. the PRODUCTION setting, --fmad=false ###"
grep -n 'fmad' "$CM" | sed 's/^/  /'
./build/qual-final/tests/unit/gpu/CFDGpuTests --gtest_filter='HllcGpuEquivalenceTest.G2_CpuAndGpu*' \
  > "$P17/logs/05_fma_off.log" 2>&1
echo "  exit: $?"
grep -aE 'rows compared|rows BITWISE|branch-selection|max \|flux|max \|wave' "$P17/logs/05_fma_off.log" \
  | sed 's/^ */  /'
echo ""

echo "### 2. flip to --fmad=true and rebuild ONLY the kernel ###"
sed -i 's|COMPILE_OPTIONS "--fmad=false"|COMPILE_OPTIONS "--fmad=true"|' "$CM"
if ! grep -q 'fmad=true' "$CM"; then
  echo "  *** the flip did not apply -- ABORTING rather than reporting a stale result ***"
  cp "$P17/logs/CMakeLists.txt.fma_pristine" "$CM"
  echo "done: $(date -u +%FT%TZ)"
  exit 1
fi
grep -n 'fmad' "$CM" | sed 's/^/  /'
cmake --build build/qual-final --target CFDGpuTests -j 16 > "$P17/logs/05_fma_on_build.log" 2>&1
echo "  rebuild exit: $?"
echo ""

echo "### 3. measure WITH contraction ###"
./build/qual-final/tests/unit/gpu/CFDGpuTests --gtest_filter='HllcGpuEquivalenceTest.G2_CpuAndGpu*' \
  > "$P17/logs/05_fma_on.log" 2>&1
echo "  exit: $? (a NONZERO exit here is the expected result -- bitwise equality should break)"
grep -aE 'rows compared|rows BITWISE|branch-selection|max \|flux|max \|wave|worst row' \
  "$P17/logs/05_fma_on.log" | sed 's/^ */  /'
echo ""
echo "  per-row differences with contraction ON:"
sed -n '/case  *branch/,/rows compared/p' "$P17/logs/05_fma_on.log" | sed 's/^ */    /'
echo ""

echo "### 4. restore, rebuild, and PROVE the bitwise result is back ###"
cp "$P17/logs/CMakeLists.txt.fma_pristine" "$CM"
AFTER=$(sha256sum "$CM" | awk '{print $1}')
if [ "$AFTER" = "$BEFORE" ]; then
  echo "  RESTORE: VERIFIED by SHA-256 ($AFTER)"
else
  echo "  RESTORE: *** FAILED *** got $AFTER expected $BEFORE"
fi
grep -n 'fmad' "$CM" | sed 's/^/  /'
cmake --build build/qual-final --target CFDGpuTests -j 16 > "$P17/logs/05_fma_restore_build.log" 2>&1
echo "  rebuild exit: $?"
./build/qual-final/tests/unit/gpu/CFDGpuTests --gtest_filter='HllcGpu*' \
  > "$P17/logs/05_fma_restored.log" 2>&1
echo "  re-run exit: $?  (0 required)"
grep -aE 'rows BITWISE|max \|flux|PASSED|FAILED' "$P17/logs/05_fma_restored.log" | sed 's/^ */  /'
rm -f "$P17/logs/CMakeLists.txt.fma_pristine"
echo ""
echo "done: $(date -u +%FT%TZ)"
} 2>&1 | tee "$log"
