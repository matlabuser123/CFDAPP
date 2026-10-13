#!/usr/bin/env bash
# P13 Final Qualification, item 1: CLEAN builds from scratch.
#
# From scratch on purpose: every incremental build in this session reused
# objects, so a configure-time or include-order breakage introduced anywhere in
# P13-001..007 could still be hiding behind a stale artifact. Deleting the
# directory is the only way to know the tree builds as a new checkout would.
set -uo pipefail
cd /mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
export PATH=/usr/local/cuda-12.9/bin:$PATH
R=results/p13/final-qualification/2026-09-24
echo "date: $(date -u +%FT%TZ)"
echo "HEAD: $(git rev-parse --short=12 HEAD)  (working tree dirty: P13 is uncommitted)"

build() { # build <name> <dir> <cmake args...>
  local name=$1 dir=$2; shift 2
  echo
  echo "=== $name -> $dir ==="
  rm -rf "$dir"
  local t0=$(date +%s)
  if ! cmake -S . -B "$dir" -G Ninja "$@" > "$R/logs/01_${name}_configure.log" 2>&1; then
    echo "  CONFIGURE FAILED"; tail -12 "$R/logs/01_${name}_configure.log"; return 1
  fi
  echo "  configure: ok ($(( $(date +%s) - t0 )) s)"
  grep -a "CFDApp build identity\|CFDApp package:" "$R/logs/01_${name}_configure.log" | sed 's/^/    /'
  t0=$(date +%s)
  if ! cmake --build "$dir" -j 12 > "$R/logs/01_${name}_build.log" 2>&1; then
    echo "  BUILD FAILED"; grep -E "error|FAILED" "$R/logs/01_${name}_build.log" | head -15; return 1
  fi
  echo "  build: ok ($(( $(date +%s) - t0 )) s)"
  echo "  warnings: $(grep -c "warning:" "$R/logs/01_${name}_build.log")"
  echo "  targets : $(grep -c "^\[" "$R/logs/01_${name}_build.log")"
}

FAIL=0
build release_cuda build/qual-final   -DCMAKE_BUILD_TYPE=Release -DCFDAPP_ENABLE_CUDA=ON  -DCFDAPP_ENABLE_OPENMP=ON || FAIL=1
build debug_gui    build/qual-debug   -DCMAKE_BUILD_TYPE=Debug   -DCFDAPP_BUILD_GUI=ON    -DCFDAPP_ENABLE_CUDA=ON || FAIL=1
build cpu_only     build/qual-cpuonly -DCMAKE_BUILD_TYPE=Release -DCFDAPP_ENABLE_CUDA=OFF -DCFDAPP_ENABLE_OPENMP=ON || FAIL=1
build packaging    build/qual-pkg     -DCMAKE_BUILD_TYPE=Release -DCFDAPP_BUILD_GUI=ON -DCFDAPP_ENABLE_PACKAGING=ON -DCFDAPP_ENABLE_CUDA=OFF || FAIL=1

echo
echo "clean_builds_done: $(date -u +%FT%TZ)  FAIL=$FAIL"
