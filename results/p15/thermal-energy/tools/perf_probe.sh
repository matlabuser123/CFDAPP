#!/usr/bin/env bash
# Read-only probes. Does NOT disturb the running regression.
cd /mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
echo "### 1. generator in use (Ninja?) ###"
for b in final debug cpuonly; do
  g=$(grep -E "^CMAKE_GENERATOR:" build/$b/CMakeCache.txt 2>/dev/null | cut -d= -f2)
  echo "  build/$b : ${g:-?}"
done

echo
echo "### 2. ccache availability and current launcher ###"
echo "  ccache binary   : $(command -v ccache || echo 'NOT INSTALLED')"
for b in final; do
  echo "  build/$b C++ launcher: $(grep -E '^CMAKE_CXX_COMPILER_LAUNCHER' build/$b/CMakeCache.txt 2>/dev/null | cut -d= -f2 | sed 's/^$/<unset>/')"
done
command -v ccache >/dev/null && ccache -s 2>/dev/null | head -6 | sed 's/^/    /'

echo
echo "### 8. the 797 s Debug test -- is it shardable? ###"
f=build/debug/Testing/Temporary/CTestCostData.txt
if [ -f "$f" ]; then
  awk '$3>200 {printf "  %-62s %7.1f s\n", $1, $3}' "$f" | sort -k2 -g -r | head -12
  echo "  ---"
  awk '{s+=$3; if($3>200) l+=$3} END {printf "  total %.0f s; tests over 200 s account for %.0f s (%.0f%%)\n", s, l, 100*l/s}' "$f"
fi

echo
echo "### filesystem comparison: 9p vs native ext4 ###"
echo "  repo mount : $(df -T . | tail -1 | awk '{print $1" ("$2")"}')"
echo "  \$HOME mount: $(df -T ~ | tail -1 | awk '{print $1" ("$2")"}')"
echo "  free space on \$HOME: $(df -h ~ | tail -1 | awk '{print $4}')"
