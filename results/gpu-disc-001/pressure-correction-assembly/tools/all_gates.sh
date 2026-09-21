#!/usr/bin/env bash
# GPU-DISC-001 -- re-run every differential gate against the current build.
set -uo pipefail

ROOT=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
R=$ROOT/results/gpu-disc-001
CUDA=/usr/local/cuda-12.9
cd "$ROOT"

ninja -C build/cuda || exit 1

GATES="
mesh/tools/mesh_equivalence.cpp
gradients/tools/gradient_equivalence.cpp
diffusion/tools/diffusion_equivalence.cpp
convection/tools/convection_equivalence.cpp
convection/tools/momentum_convection_equivalence.cpp
boundary-conditions/tools/boundary_condition_equivalence.cpp
momentum-assembly/tools/momentum_assembly_equivalence.cpp
momentum-response/tools/momentum_response_equivalence.cpp
rhie-chow/tools/face_flux_equivalence.cpp
pressure-correction-assembly/tools/pressure_correction_equivalence.cpp
"

green=0
total=0
for rel in $GATES; do
  name=$(basename "$rel" .cpp)
  total=$((total + 1))
  /usr/bin/c++ -I"$ROOT/include" -I"$ROOT/build/cuda/generated/include" \
    -I"$ROOT/build/cuda/_deps/nlohmann_json-src/include" -isystem "$CUDA/include" \
    -O2 -DNDEBUG -std=c++20 -o "/tmp/$name" "$R/$rel" \
    build/cuda/cuda/libcfdcuda.a build/cuda/src/libcfdcore.a -L"$CUDA/lib64" -lcudart 2>/tmp/$name.build
  if [ $? -ne 0 ]; then
    echo "BUILD-FAIL  $name"
    tail -5 "/tmp/$name.build"
    continue
  fi
  out=$("/tmp/$name" 2>&1)
  rc=$?
  last=$(echo "$out" | tail -2 | head -1)
  summary=$(echo "$out" | tail -1)
  if [ $rc -eq 0 ]; then
    green=$((green + 1))
    echo "PASS  $name  |  $last  |  $summary"
  else
    echo "FAIL  $name  rc=$rc"
    echo "$out" | grep -i fail | head -10
  fi
done

echo ""
echo "$green/$total GPU-DISC differential gates green"
[ "$green" -eq "$total" ]
