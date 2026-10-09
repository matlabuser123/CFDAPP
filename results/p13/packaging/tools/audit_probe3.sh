#!/usr/bin/env bash
# P13-006 audit, part 3 -- the two measurements part 2 got wrong:
#   A10 used a relative log path after cd'ing away (my bug, not the product's)
#   A8's echo had backticks inside double quotes, which RAN cmake --install
set -uo pipefail
ROOT=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
cd $ROOT
R=$ROOT/results/p13/packaging

echo "=== A10: does the installed CLI run an installed example? ==="
W=/tmp/p13_pkg_case; rm -rf $W
EX=$(ls -d /tmp/p13_pkg_install/examples/*/ | head -1)
echo "  example: $EX"
cp -r "$EX" $W
cd /tmp
/tmp/p13_pkg_install/bin/cfdapp --case $W > $R/logs/05_installed_run.log 2>&1
echo "  exit status: $?"
head -12 $R/logs/05_installed_run.log | sed 's/^/    /'
cd $ROOT

echo
echo "=== A12: where does the version suffix come from? ==="
grep -n "dev\|VERSION_STRING\|kVersion" include/cfd/core/Version.hpp.in | sed 's/^/  /'
grep -n "CFDApp_VERSION\b" CMakeLists.txt | head -5 | sed 's/^/  /'
echo "  project() VERSION      : $(grep -A1 'project(CFDApp' CMakeLists.txt | grep VERSION | tr -s ' ')"
echo "  cfdapp --version       : $(/tmp/p13_pkg_install/bin/cfdapp --version)"
echo "  CPACK_PACKAGE_VERSION  : $(grep -a 'CPACK_PACKAGE_VERSION ' build/pkg/CPackConfig.cmake)"
echo "  package file name      : $(ls build/pkg/*.zip | xargs -n1 basename)"

echo
echo "=== A13: what is IN the mislabelled zip? ==="
cd build/pkg
unzip -l CFDApp-0.2.0-Windows-x64.zip | head -12 | sed 's/^/  /'
unzip -o -q CFDApp-0.2.0-Windows-x64.zip -d /tmp/p13_zip_check
echo "  binary type of the packaged cfdapp:"
file /tmp/p13_zip_check/*/bin/cfdapp 2>/dev/null | sed 's/^/    /'
echo "  entries in the zip: $(unzip -l CFDApp-0.2.0-Windows-x64.zip | tail -1)"

echo
echo "=== A14: is the packaged binary runnable, and does it say what the package claims? ==="
B=$(ls /tmp/p13_zip_check/*/bin/cfdapp 2>/dev/null | head -1)
if [ -n "$B" ]; then
  chmod +x "$B"
  echo "  packaged cfdapp --version: $("$B" --version 2>&1)"
else
  echo "  no packaged cfdapp found"
fi
