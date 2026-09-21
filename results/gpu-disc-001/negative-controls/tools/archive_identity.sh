#!/usr/bin/env bash
# GPU-DISC-001P -- why does libcfdcuda.a hash differently after the campaign?
#
# The production SOURCES are restored byte for byte (regression.sh proves that
# against baseline-sha256.txt), and all 15 gates plus the full regression
# re-pass. But the ARCHIVE hash moved, and "archives aren't reproducible" is an
# assertion, not a measurement. This measures it: every member object is
# extracted and hashed, so the claim becomes "the compiled code is identical and
# only the archive metadata differs" -- or it does not, and that is a finding.
set -uo pipefail
ROOT=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
EVID=$ROOT/results/gpu-disc-001/negative-controls/restoration
cd "$ROOT"
mkdir -p "$EVID"

WORK=$(mktemp -d)
cd "$WORK"
ar x "$ROOT/build/cuda/cuda/libcfdcuda.a"
echo "=== member object sha256, current libcfdcuda.a ==="
sha256sum ./*.o | sort -k2

echo ""
echo "=== archive member table, with the metadata ar records ==="
# -v prints mode/uid/gid/mtime per member: the fields that make a plain `ar`
# archive non-reproducible even when every member is byte-identical.
ar tv "$ROOT/build/cuda/cuda/libcfdcuda.a"

cd "$ROOT"
rm -rf "$WORK"
