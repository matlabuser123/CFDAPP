#!/usr/bin/env bash
# P12-MESH-006 full regression (G11) on the final sources, sequential (never two ctest runs at once):
#   1. build/release (Release -O3, GUI off)             -> a3/logs/16_full_regression_release.log
#   2. build/debug   (Debug, GUI on, Qt offscreen)       -> a3/logs/17_full_regression_debug_gui.log
#   3. build/asan    (Debug + ASan + UBSan, GUI off; the CI "sanitizers" job's configuration),
#      rebuilt first                                      -> a3/logs/18_full_regression_asan_ubsan.log
# git status before/after is kept for the G10.5 output classification.
R=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
L=$R/results/p12-mesh-006/a3/logs
W=$HOME/m6final; mkdir -p $W
cd $R
git status --short > $W/status_before.txt
{
  echo "# P12-MESH-006 full regression, build/release (Release -O3, GUI off), $(date -u +%Y-%m-%dT%H:%M:%SZ)"
  echo "# HEAD $(git rev-parse HEAD) + uncommitted P12-MESH-001..005 + P12-MESH-006 working tree"
  (cd build/release && ctest -j16 --output-on-failure 2>&1)
  echo "release ctest exit $?"
  echo "# end $(date -u +%Y-%m-%dT%H:%M:%SZ)"
} > $L/16_full_regression_release.log 2>&1
git status --short > $W/status_after_release.txt
{
  echo "# P12-MESH-006 full regression, build/debug (Debug, GUI on, Qt offscreen), $(date -u +%Y-%m-%dT%H:%M:%SZ)"
  echo "# HEAD $(git rev-parse HEAD) + uncommitted P12-MESH-001..005 + P12-MESH-006 working tree"
  (cd build/debug && QT_QPA_PLATFORM=offscreen ctest -j16 --output-on-failure 2>&1)
  echo "debug ctest exit $?"
  echo "# end $(date -u +%Y-%m-%dT%H:%M:%SZ)"
} > $L/17_full_regression_debug_gui.log 2>&1
git status --short > $W/status_after_debug.txt
{
  echo "# P12-MESH-006 full regression, build/asan (Debug + AddressSanitizer + UBSan, GUI off), $(date -u +%Y-%m-%dT%H:%M:%SZ)"
  echo "# configuration: $(grep -E 'CMAKE_BUILD_TYPE:|CFDAPP_ENABLE_SANITIZERS:|CFDAPP_BUILD_GUI:' build/asan/CMakeCache.txt | tr '\n' ' ')"
  cmake --build build/asan -j16 > $W/asan_build.log 2>&1
  echo "# asan build exit $?; warnings $(grep -c 'warning:' $W/asan_build.log)"
  (cd build/asan && ctest -j16 --timeout 1800 --output-on-failure 2>&1)
  echo "asan ctest exit $?"
  echo "# end $(date -u +%Y-%m-%dT%H:%M:%SZ)"
} > $L/18_full_regression_asan_ubsan.log 2>&1
{
  echo "# sanitizer diagnostics in the ASan+UBSan log: AddressSanitizer $(grep -c 'ERROR: AddressSanitizer' $L/18_full_regression_asan_ubsan.log), UBSan runtime errors $(grep -c 'runtime error:' $L/18_full_regression_asan_ubsan.log), LeakSanitizer $(grep -c 'ERROR: LeakSanitizer' $L/18_full_regression_asan_ubsan.log)"
} >> $L/18_full_regression_asan_ubsan.log
git status --short > $W/status_after.txt
for f in 16_full_regression_release.log 17_full_regression_debug_gui.log 18_full_regression_asan_ubsan.log; do
  echo "$f: $(grep -E 'tests passed|tests failed' $L/$f | tail -1) | $(grep -E 'ctest exit' $L/$f)"
done
tail -1 $L/18_full_regression_asan_ubsan.log
