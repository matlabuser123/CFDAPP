#!/usr/bin/env bash
# GPU-PIPE-001 Phase 1: verify the CUDA build is the qualified toolchain and is
# not stale, before any measurement is taken (CLAUDE.md 8).
set -u
cd /mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp

echo "=== toolkit ==="
ls /usr/local/cuda-12.9/bin/ | tr '\n' ' ' | head -c 400; echo
CUOBJ=$(command -v cuobjdump || echo /usr/local/cuda-12.9/bin/cuobjdump)
echo "cuobjdump: $CUOBJ"
/usr/local/cuda-12.9/bin/nvcc --version | grep release

echo
echo "=== cuda object files ==="
find build/cuda -name '*.cu.o' | head -5
CU=$(find build/cuda -name '*.cu.o' | head -1)
echo "picked: ${CU:-<none>}"

if [ -n "${CU:-}" ] && [ -x "$CUOBJ" ]; then
  echo "--- native cubins (ELF) ---"
  "$CUOBJ" --list-elf "$CU" 2>&1 | grep -oE 'sm_[0-9]+' | sort -u
  echo "--- PTX ---"
  "$CUOBJ" --list-ptx "$CU" 2>&1 | grep -oE 'compute_[0-9]+' | sort -u
fi

echo
echo "=== freshness (authoritative: ninja's own dependency graph) ==="
# An earlier version of this check compared each artifact's mtime against the
# newest file under src/ + include/.  That is WRONG and produced a false STALE:
# include/cfd/gpu/DeviceVectorOps.hpp is included only from cuda/, so cfdcore
# does not depend on it, and a cosmetic mtime touch on that header does not make
# cfdcore stale.  Reimplementing the dependency graph by hand is how a
# fail-closed check turns into a check that cries wolf, so ask the build system
# -- `ninja -n` reports exactly what it would rebuild, using the real graph.
for tgt in src/libcfdcore.a cuda/libcfdcuda.a benchmarks/gpu/cfd_benchmark_cuda_end_to_end; do
  out=$(ninja -C build/cuda -n "$tgt" 2>&1)
  if echo "$out" | grep -q 'no work to do'; then
    printf '  %-48s UP TO DATE\n' "$tgt"
  else
    printf '  %-48s WOULD REBUILD -- STALE:\n' "$tgt"
    echo "$out" | head -5 | sed 's/^/        /'
  fi
done

echo
echo "  full-tree check:"
ninja -C build/cuda -n 2>&1 | tail -2 | sed 's/^/    /'

echo
echo "=== hashes (recorded for the baseline) ==="
sha256sum build/cuda/cuda/libcfdcuda.a build/cuda/src/libcfdcore.a \
          build/cuda/benchmarks/gpu/cfd_benchmark_cuda_end_to_end 2>/dev/null
