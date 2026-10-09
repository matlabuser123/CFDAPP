#!/usr/bin/env bash
# P17.10 V7 / section 30 -- what --fmad=false is actually worth, MEASURED.
#
# The flag is not new and was not introduced to make this phase agree: it has applied to the
# twenty-odd existing kernels since before P17.7, and P17.7 measured its cost for the flux alone
# (17/17 bitwise with it, 8/17 without). This repeats that measurement for the SOLVER kernels, so
# the claim "contraction is the only remaining difference" is a number rather than a hypothesis.
#
# It flips ONE translation unit's property, rebuilds, runs, restores, and PROVES the restore by
# sha256 of the CMakeLists and by re-running the suite.
set -uo pipefail
cd /mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp || exit 1
export PATH="/usr/local/cuda-12.9/bin:$PATH"
P17=results/p17/cpu_gpu_equivalence
CM=cuda/CMakeLists.txt
TREE=build/qual-final
BIN="$TREE/tests/unit/gpu/CFDGpuTests"
FILTER='DensityBasedGpuEquivalence*'

BEFORE=$(sha256sum "$CM" | cut -d' ' -f1)
echo "cuda/CMakeLists.txt before: $BEFORE"
cp "$CM" /tmp/p17_10_cmakelists.bak

# Flip ONLY the P17.10 block. The line after the DeviceDensityBasedKernel property call.
python3 - "$CM" <<'PY'
import sys
p = sys.argv[1]
s = open(p, encoding="utf-8").read()
old = 'set_source_files_properties(kernels/DeviceDensityBasedKernel.cu PROPERTIES\n  COMPILE_OPTIONS "--fmad=false")'
new = 'set_source_files_properties(kernels/DeviceDensityBasedKernel.cu PROPERTIES\n  COMPILE_OPTIONS "--fmad=true")'
assert s.count(old) == 1, "expected exactly one P17.10 fmad property, found %d" % s.count(old)
open(p, "w", encoding="utf-8").write(s.replace(old, new))
print("flipped to --fmad=true")
PY

cmake -S . -B "$TREE" > "$P17/logs/30_fma_configure.log" 2>&1
cmake --build "$TREE" --target CFDGpuTests -j 16 > "$P17/logs/30_fma_build.log" 2>&1
echo "build exit: $?"
grep -ac "fmad=true" "$P17/logs/30_fma_build.log" | sed 's/^/build lines mentioning fmad=true: /'

OMP_NUM_THREADS=1 "$BIN" --gtest_filter="$FILTER" > "$P17/logs/31_fma_true.log" 2>&1
echo "fmad=true run exit: $?"
grep -aE '^\[  (PASSED|FAILED)  \]' "$P17/logs/31_fma_true.log"
grep -aE '^  (A7|B1|B2|C1|D1|D3) ' "$P17/logs/31_fma_true.log"

# Restore and PROVE the restore.
cp /tmp/p17_10_cmakelists.bak "$CM"
AFTER=$(sha256sum "$CM" | cut -d' ' -f1)
echo "cuda/CMakeLists.txt after:  $AFTER"
if [ "$BEFORE" != "$AFTER" ]; then echo "FATAL: restore failed"; exit 1; fi
echo "restore verified by sha256"

cmake -S . -B "$TREE" > "$P17/logs/32_restore_configure.log" 2>&1
cmake --build "$TREE" --target CFDGpuTests -j 16 > "$P17/logs/32_restore_build.log" 2>&1
OMP_NUM_THREADS=1 "$BIN" --gtest_filter="$FILTER" > "$P17/logs/33_fma_false_restored.log" 2>&1
echo "restored run exit: $?"
grep -aE '^\[  (PASSED|FAILED)  \]' "$P17/logs/33_fma_false_restored.log"
