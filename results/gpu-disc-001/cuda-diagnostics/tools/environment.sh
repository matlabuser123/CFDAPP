#!/usr/bin/env bash
# GPU-DISC-001O -- the diagnostic build identity, recorded rather than assumed.
set -uo pipefail
ROOT=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
CUDA=/usr/local/cuda-12.9
cd "$ROOT"

echo "=== host ==="
uname -srm
echo
echo "=== compiler ==="
/usr/bin/c++ --version | head -1
echo
echo "=== CUDA toolkit ==="
"$CUDA/bin/nvcc" --version | tail -2
echo
echo "=== compute-sanitizer ==="
"$CUDA/bin/compute-sanitizer" --version 2>&1 | head -3
echo
echo "=== GPU ==="
nvidia-smi --query-gpu=name,compute_cap,driver_version,memory.total --format=csv,noheader 2>/dev/null \
  || echo "(nvidia-smi unavailable in this environment)"
echo
echo "=== build configuration ==="
grep -E "^CMAKE_BUILD_TYPE|^CMAKE_CUDA_ARCHITECTURES|^CMAKE_CXX_COMPILER:|^CMAKE_CUDA_COMPILER:|^CFDAPP_ENABLE_CUDA" \
  build/cuda/CMakeCache.txt
echo
echo "=== CUDA compile flags actually used (one discretization kernel) ==="
python3 - <<'PY'
import json
for e in json.load(open("build/cuda/compile_commands.json")):
    if e["file"].endswith("DeviceMomentumAssemblyKernel.cu"):
        print(e["command"][:900])
        break
PY
echo
echo "=== -fmad=false coverage (numerical flags MUST be unchanged for diagnostics) ==="
sed -n '/set_source_files_properties(/,/PROPERTIES COMPILE_OPTIONS/p' cuda/CMakeLists.txt | grep -c "\.cu"
echo "kernels carrying -fmad=false"
echo
echo "=== build identity ==="
ninja -C build/cuda 2>&1 | tail -1
sha256sum build/cuda/cuda/libcfdcuda.a build/cuda/src/libcfdcore.a
