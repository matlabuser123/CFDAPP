#!/usr/bin/env bash
cd /mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
export PATH=/usr/local/cuda-12.9/bin:$PATH
for b in final debug cpuonly; do
  cmake --build build/$b -j 12 >/dev/null 2>&1
  n=$(cd build/$b && ctest -N -R CFDAppBenchmarkSuiteSmoke 2>/dev/null | grep -c "Test *#")
  echo "  build/$b: CFDAppBenchmarkSuiteSmoke registered = $n"
done
echo "=== run it in build/final ==="
( cd build/final && ctest -R CFDAppBenchmarkSuiteSmoke --output-on-failure ) 2>&1 | tail -8
