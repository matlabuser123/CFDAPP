#!/usr/bin/env bash
# P13-006 audit -- measure what the packaging configuration actually does,
# rather than reading it. Nothing here changes production files.
set -uo pipefail
cd /mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
export PATH=/usr/local/cuda-12.9/bin:$PATH

echo "=== A1: what the packaging config hardcodes ==="
grep -n "Windows\|NSIS\|ZIP\|\.exe" cmake/Packaging.cmake | sed 's/^/  /'

echo
echo "=== A2: is a CLI-only package possible? ==="
grep -n "NOT CFDAPP_BUILD_GUI" -A 5 cmake/Packaging.cmake | sed 's/^/  /'

echo
echo "=== A3: install() rules, and what has none ==="
echo "  install(TARGETS ...) targets : $(grep -c 'install(TARGETS' cmake/Packaging.cmake)"
echo "  library/header install rules : $(grep -rc 'install(' src/CMakeLists.txt include 2>/dev/null | paste -sd+ | bc 2>/dev/null || echo 0)"
echo "  CMake package config export  : $(grep -rn 'install(EXPORT\|CMakePackageConfigHelpers' CMakeLists.txt cmake/*.cmake 2>/dev/null | wc -l)"

echo
echo "=== A4: CPack generators available on this host ==="
which cpack && cpack --help 2>/dev/null | sed -n '/Generators/,$p' | head -20

echo
echo "=== A5: does a packaging-enabled configure even work here? ==="
rm -rf build/pkg
cmake -S . -B build/pkg -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCFDAPP_BUILD_GUI=ON \
  -DCFDAPP_ENABLE_PACKAGING=ON \
  -DCFDAPP_ENABLE_CUDA=OFF \
  > results/p13/packaging/logs/00_configure_gui_packaging.log 2>&1
echo "  configure exit status: $?"
tail -5 results/p13/packaging/logs/00_configure_gui_packaging.log | sed 's/^/  /'

echo
echo "=== A6: a CLI-only packaging configure ==="
rm -rf build/pkgcli
cmake -S . -B build/pkgcli -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCFDAPP_BUILD_GUI=OFF \
  -DCFDAPP_ENABLE_PACKAGING=ON \
  -DCFDAPP_ENABLE_CUDA=OFF \
  > results/p13/packaging/logs/01_configure_cli_packaging.log 2>&1
echo "  configure exit status: $?"
grep -n "WARNING\|Skipping" -A 3 results/p13/packaging/logs/01_configure_cli_packaging.log | head -10 | sed 's/^/  /'
echo "  CPackConfig.cmake produced: $([ -f build/pkgcli/CPackConfig.cmake ] && echo YES || echo NO)"
