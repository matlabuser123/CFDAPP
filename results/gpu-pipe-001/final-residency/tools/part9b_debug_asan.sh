#!/usr/bin/env bash
# GPU-PIPE-001 Final Residency, Part 9b -- the two legs of CLAUDE.md section 8's
# "full regression" that the authorization's Part 9 list does not enumerate:
# the Debug configuration and the sanitizer configuration, both at CI settings.
#
# Both trees are configured CFDAPP_ENABLE_CUDA=OFF, so neither exercises the
# resident GPU path at all. What they DO exercise is the part of this change
# that lives in CPU-only code: SIMPLE.cpp's host branch, which was restructured
# (previousU/previousV and velocityStar are now conditionally constructed), and
# src/gpu/GpuSimpleDiscretization.cpp's four added stubs. That is the reason to
# run them, and the reason not to overclaim what they cover.
set -uo pipefail
ROOT=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
E=$ROOT/results/gpu-pipe-001/final-residency/regression
cd "$ROOT"
declare -A RC

echo "##################### A. Debug #####################"
cmake --build --preset debug -j 16 > "$E/11-debug-build.log" 2>&1
RC[debug_build]=$?
tail -2 "$E/11-debug-build.log" | sed 's/^/  /'
ctest --test-dir build/debug --output-on-failure -j 16 > "$E/11-debug-ctest.log" 2>&1
RC[debug]=$?
grep -E "tests passed|tests failed|Total Test time" "$E/11-debug-ctest.log" | sed 's/^/  /'
ninja -C build/debug 2>&1 | tail -1 | sed 's/^/  freshness after ctest: /'
echo "  rc=${RC[debug]}"

echo ""
echo "##################### B. sanitizers, at CI settings #####################"
cmake --build --preset asan -j 16 > "$E/12-asan-build.log" 2>&1
RC[asan_build]=$?
tail -2 "$E/12-asan-build.log" | sed 's/^/  /'
ASAN_OPTIONS=detect_leaks=1:halt_on_error=0 \
UBSAN_OPTIONS=print_stacktrace=1:halt_on_error=0 \
  ctest --test-dir build/asan --output-on-failure --timeout 7200 -j 16 \
  > "$E/12-asan-ctest.log" 2>&1
RC[asan]=$?
grep -E "tests passed|tests failed|Total Test time" "$E/12-asan-ctest.log" | sed 's/^/  /'
ninja -C build/asan 2>&1 | tail -1 | sed 's/^/  freshness after ctest: /'
echo "  rc=${RC[asan]}"

echo ""
echo "############################## PART 9b SUMMARY ##############################"
bad=0
for k in debug_build debug asan_build asan; do
  printf "  %-12s rc=%s  %s\n" "$k" "${RC[$k]}" \
    "$([ "${RC[$k]}" -eq 0 ] && echo PASS || echo FAIL)"
  [ "${RC[$k]}" -ne 0 ] && bad=1
done
[ $bad -eq 0 ] && echo "DEBUG + SANITIZERS: ALL PASS" || echo "DEBUG + SANITIZERS: FAILURES PRESENT"
exit $bad
