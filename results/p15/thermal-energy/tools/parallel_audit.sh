#!/usr/bin/env bash
cd /mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
echo "### hardware as WSL sees it ###"
echo "  nproc            : $(nproc)"
echo "  MemTotal         : $(awk '/MemTotal/{printf "%.1f GiB", $2/1048576}' /proc/meminfo)"
echo "  loadavg          : $(cut -d' ' -f1-3 /proc/loadavg)"
echo
echo "### is OpenMP ON in each build? (decides whether ctest -j oversubscribes) ###"
for b in final debug cpuonly; do
  v=$(grep -E "^CFDAPP_ENABLE_OPENMP" build/$b/CMakeCache.txt 2>/dev/null | head -1)
  echo "  build/$b : ${v:-not found}"
done
echo
echo "### does the CFDApp code actually spawn OpenMP threads? ###"
echo "  '#pragma omp' in src/ : $(grep -rc '#pragma omp' src/ 2>/dev/null | awk -F: '{s+=$2} END{print s+0}')"
echo "  files containing it   : $(grep -rl '#pragma omp' src/ 2>/dev/null | wc -l)"
grep -rl '#pragma omp' src/ 2>/dev/null | head -8 | sed 's/^/    /'
echo
echo "### does any test set OMP_NUM_THREADS itself? ###"
grep -rn "OMP_NUM_THREADS\|omp_set_num_threads" tests/ src/ benchmarks/ 2>/dev/null | head -6
echo
echo "### current OMP default in this shell ###"
echo "  OMP_NUM_THREADS  : ${OMP_NUM_THREADS:-<unset -> defaults to nproc>}"
echo
echo "### ctest cost data: how many tests are genuinely long? ###"
for b in final debug; do
  f=build/$b/Testing/Temporary/CTestCostData.txt
  if [ -f "$f" ]; then
    echo "  build/$b: $(wc -l < $f) entries; tests over 5 s: $(awk '$3>5' $f 2>/dev/null | wc -l); over 30 s: $(awk '$3>30' $f 2>/dev/null | wc -l)"
  fi
done
