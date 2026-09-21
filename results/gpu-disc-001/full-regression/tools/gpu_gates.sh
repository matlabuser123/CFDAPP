#!/usr/bin/env bash
# GPU-DISC-001R Phase E -- the 15 GPU-DISC differential gates, on the FINAL
# clean-build binaries.
#
# all_gates.sh hardcodes build/cuda. This runs the same 15 harnesses against
# build/final instead, so the gate evidence comes from the binaries this
# regression actually qualifies rather than from the incremental build.
set -uo pipefail
ROOT=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
R=$ROOT/results/gpu-disc-001
EVID=$R/full-regression/gpu-gates
BUILD=$ROOT/build/final
CUDA=/usr/local/cuda-12.9
cd "$ROOT"
mkdir -p "$EVID"

echo "=== build identity under test ==="
ninja -C "$BUILD" 2>&1 | tail -1
sha256sum "$BUILD/cuda/libcfdcuda.a" "$BUILD/src/libcfdcore.a"

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
velocity-correction/tools/velocity_correction_equivalence.cpp
face-flux-correction/tools/face_flux_correction_equivalence.cpp
single-iteration/tools/single_iteration_equivalence.cpp
integrated-simple/tools/integrated_simple_equivalence.cpp
full-solve-equivalence/tools/full_solve_equivalence.cpp
"

echo ""
green=0
total=0
for rel in $GATES; do
  name=$(basename "$rel" .cpp)
  total=$((total + 1))
  /usr/bin/c++ -I"$ROOT/include" -I"$BUILD/generated/include" \
    -I"$BUILD/_deps/nlohmann_json-src/include" -isystem "$CUDA/include" \
    -O2 -DNDEBUG -std=c++20 -o "/tmp/final_$name" "$R/$rel" \
    -Wl,--start-group "$BUILD/cuda/libcfdcuda.a" "$BUILD/src/libcfdcore.a" \
    -Wl,--end-group -L"$CUDA/lib64" -lcudart 2>"/tmp/final_$name.build"
  if [ $? -ne 0 ]; then
    echo "BUILD-FAIL  $name"
    tail -5 "/tmp/final_$name.build"
    continue
  fi
  if [ "$name" = "full_solve_equivalence" ]; then
    out=$("/tmp/final_$name" --quick 2>&1)
  else
    out=$("/tmp/final_$name" 2>&1)
  fi
  rc=$?
  echo "$out" > "$EVID/$name.log"
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
echo "$green/$total GPU-DISC differential gates green on the FINAL binaries"
[ "$green" -eq "$total" ]
