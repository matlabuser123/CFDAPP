#!/usr/bin/env bash
# The recorded GPU BiCGSTAB restart asymmetry, re-run on the CURRENT binaries
# with the project's own already-qualified probe and its own settings.
# Part 2 of the authorization requires exactly this.
set -uo pipefail
ROOT=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
E=$ROOT/results/gpu-pipe-001/final-residency
BUILD=$ROOT/build/final
CUDA=/usr/local/cuda-12.9
cd "$ROOT"
/usr/bin/c++ -I include -I "$BUILD/generated/include" \
  -I "$BUILD/_deps/nlohmann_json-src/include" -isystem "$CUDA/include" \
  -O2 -DNDEBUG -std=c++20 -o /tmp/final_known_debt \
  "$ROOT/results/gpu-disc-001/full-regression/tools/known_debt_probe.cpp" \
  -Wl,--start-group "$BUILD/cuda/libcfdcuda.a" "$BUILD/src/libcfdcore.a" -Wl,--end-group \
  -L"$CUDA/lib64" -lcudart || exit 1
out=$E/known-bicgstab/after-resident-loop.log
{ echo "# known_debt_probe on the post-resident-SIMPLE-loop binaries"
  echo "# date: $(date -u '+%Y-%m-%dT%H:%M:%SZ')"
  echo "# libcfdcuda.a: $(sha256sum "$BUILD/cuda/libcfdcuda.a" | cut -d' ' -f1)"
  echo "# libcfdcore.a: $(sha256sum "$BUILD/src/libcfdcore.a" | cut -d' ' -f1)"
  echo "# SIMPLE.cpp:   $(sha256sum src/pressure_velocity/SIMPLE.cpp | cut -d' ' -f1)"
  echo ""; } > "$out"
/tmp/final_known_debt >> "$out" 2>&1
rc=$?
echo "# exit code: $rc" >> "$out"
grep -E "cpu |gpu-pipe|gpu-disc|matches|KNOWN DEBT|status|iterations" "$out" | tail -12
exit $rc
