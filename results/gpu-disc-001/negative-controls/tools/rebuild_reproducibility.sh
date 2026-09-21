#!/usr/bin/env bash
# GPU-DISC-001P -- is nvcc bit-reproducible on this toolchain?
#
# After the campaign libcfdcuda.a hashes differently from the pre-campaign
# archive even though every SOURCE is restored byte for byte. `ar tv` shows the
# archive is built in DETERMINISTIC mode (uid/gid 0/0, mtime zeroed), so archive
# metadata cannot be the explanation -- which means either a member object
# differs, or the member order changed.
#
# This settles it by measurement: recompile one kernel from byte-identical
# source and compare the object. Touch changes mtime only; the source sha256 is
# unchanged, so baseline integrity is not affected.
set -uo pipefail
ROOT=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
EVID=$ROOT/results/gpu-disc-001/negative-controls/restoration
cd "$ROOT"

SRC=cuda/kernels/DeviceGradientKernel.cu
OBJ=$(find build/cuda -name 'DeviceGradientKernel.cu.o' | head -1)

echo "=== source sha256 (must not change) ==="
sha256sum "$SRC"

echo ""
echo "=== object BEFORE recompiling ==="
sha256sum "$OBJ"
before=$(sha256sum "$OBJ" | cut -d' ' -f1)
archiveBefore=$(sha256sum build/cuda/cuda/libcfdcuda.a | cut -d' ' -f1)

echo ""
echo "=== touch the source (mtime only) and rebuild ==="
touch "$SRC"
ninja -C build/cuda 2>&1 | tail -1

echo ""
echo "=== source sha256 AFTER (must be unchanged) ==="
sha256sum "$SRC"

echo ""
echo "=== object AFTER recompiling byte-identical source ==="
sha256sum "$OBJ"
after=$(sha256sum "$OBJ" | cut -d' ' -f1)
archiveAfter=$(sha256sum build/cuda/cuda/libcfdcuda.a | cut -d' ' -f1)

echo ""
if [ "$before" = "$after" ]; then
  echo "VERDICT: nvcc IS bit-reproducible here -- the same source gives the same object."
  echo "         A differing archive would therefore be a real difference, not noise."
else
  echo "VERDICT: nvcc is NOT bit-reproducible on this toolchain -- recompiling"
  echo "         byte-identical source produces a different object. The archive"
  echo "         hash therefore carries no information about source identity;"
  echo "         the source sha256 set and the test results are what do."
fi
echo "  archive before recompile: $archiveBefore"
echo "  archive after  recompile: $archiveAfter"
