#!/usr/bin/env bash
# GPU-PIPE-001 Final Residency, Part 9d -- re-verification after the clang-format
# remediation, on binaries built from the reformatted bytes.
#
# The reformatting is proven semantically null two ways (14-format-remediation.log):
# libcfdcore.a is byte-identical, and 11 of the 12 files are whitespace-identical
# after stripping whitespace. The twelfth, SIMPLE.cpp, differs by ONE reordered
# #include -- Logger.hpp moved ahead of Gradient.hpp for alphabetical ordering.
# Both headers were already included; reordering two independent includes is
# semantically neutral, and the identical archive proves it for this compiler.
#
# nvcc is not bit-reproducible, so libcfdcuda.a cannot be compared the same way.
# These three checks cover it instead.
set -uo pipefail
ROOT=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
E=$ROOT/results/gpu-pipe-001/final-residency/regression
T=$ROOT/results/gpu-pipe-001/final-residency/tools
cd "$ROOT"
declare -A RC

echo "##################### 0. build identity after reformatting #####################"
ninja -C build/final 2>&1 | tail -1
ninja -C build/cuda 2>&1 | tail -1
sha256sum build/final/cuda/libcfdcuda.a build/final/src/libcfdcore.a | tee "$E/15-post-format-identity.txt"

echo ""
echo "##################### 1. clang-format #####################"
mapfile -t F < <(find include src apps tests -type f -name '*.cpp' -o -type f -name '*.hpp')
clang-format-18 --dry-run --Werror --style=file:.clang-format "${F[@]}" > "$E/15-clang-format.log" 2>&1
RC[format]=$?
echo "  files checked: ${#F[@]}   violations: $(grep -c 'error:' "$E/15-clang-format.log")   rc=${RC[format]}"

echo ""
echo "##################### 2. complete CTest, Release + CUDA #####################"
ctest --test-dir build/final --output-on-failure > "$E/15-ctest.log" 2>&1
RC[ctest]=$?
grep -E "tests passed|tests failed|Total Test time" "$E/15-ctest.log" | sed 's/^/  /'
ninja -C build/final 2>&1 | tail -1 | sed 's/^/  freshness after ctest: /'
echo "  rc=${RC[ctest]}"

echo ""
echo "##################### 3. the 15 GPU-DISC differential gates #####################"
bash "$ROOT/results/gpu-disc-001/full-regression/tools/gpu_gates.sh" > "$E/15-gpu-gates.log" 2>&1
RC[gates]=$?
tail -2 "$E/15-gpu-gates.log" | sed 's/^/  /'
echo "  rc=${RC[gates]}"

echo ""
echo "##################### 4. resident-loop comparison, bitwise #####################"
# Not in the authorised minimum, but ten of the twelve reformatted files are CUDA
# headers, and this is the gate that would see any change to what they compile to.
bash "$T/run_phase.sh" compare > "$E/15-comparison-console.log" 2>&1
RC[compare]=$?
grep -E "cases=|COMPARISON" "$E/15-comparison-console.log" | sed 's/^/  /'
echo "  rc=${RC[compare]}"

echo ""
echo "############################## PART 9d SUMMARY ##############################"
bad=0
for k in format ctest gates compare; do
  printf "  %-8s rc=%s  %s\n" "$k" "${RC[$k]}" "$([ "${RC[$k]}" -eq 0 ] && echo PASS || echo FAIL)"
  [ "${RC[$k]}" -ne 0 ] && bad=1
done
[ $bad -eq 0 ] && echo "POST-FORMAT RE-VERIFICATION: ALL PASS" \
               || echo "POST-FORMAT RE-VERIFICATION: FAILURES PRESENT"
exit $bad
