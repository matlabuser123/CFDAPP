#!/usr/bin/env bash
# Build and run the amended transfer guard. Separate from run_phase.sh because
# the amendment needs a dry-run and a fresh run of the SAME binary.
set -uo pipefail
ROOT=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
E=$ROOT/results/gpu-pipe-001/final-residency
CUDA=/usr/local/cuda-12.9
cd "$ROOT"
ninja -C build/final > /dev/null 2>&1
/usr/bin/c++ -I include -I build/final/generated/include \
  -I build/final/_deps/nlohmann_json-src/include -isystem "$CUDA/include" \
  -O2 -DNDEBUG -std=c++20 -Wall -Wextra \
  -o /tmp/loop_transfer_guard "$E/tools/loop_transfer_guard.cpp" \
  -Wl,--start-group build/final/cuda/libcfdcuda.a build/final/src/libcfdcore.a -Wl,--end-group \
  -L"$CUDA/lib64" -lcudart || exit 1
out=$1; shift
{ echo "# loop_transfer_guard $*"
  echo "# date: $(date -u '+%Y-%m-%dT%H:%M:%SZ')"
  echo "# harness sha256: $(sha256sum "$E/tools/loop_transfer_guard.cpp" | cut -d' ' -f1)"
  echo "# libcfdcuda.a:   $(sha256sum build/final/cuda/libcfdcuda.a | cut -d' ' -f1)"
  echo "# libcfdcore.a:   $(sha256sum build/final/src/libcfdcore.a | cut -d' ' -f1)"
  echo ""; } > "$out"
/tmp/loop_transfer_guard "$@" >> "$out" 2>&1
rc=$?
echo "# exit code: $rc" >> "$out"
exit $rc
