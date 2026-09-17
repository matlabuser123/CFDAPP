#!/usr/bin/env bash
# P12-MESH-007 performance baseline, memory part (measurement only; separate from the timed runs of
# perf_run.sh, whose log 50 is unaffected): peak resident set size per item, one process per item.
set -u
R=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
P=$R/results/p12-mesh-007
B=$R/build/release
LOG=$P/logs/51_performance_memory.log
[ -e "$LOG" ] && { echo "REFUSED: $LOG exists"; exit 1; }
W=$HOME/m7perf_mem; rm -rf $W; mkdir -p $W
{
  echo "# P12-MESH-007 performance baseline, memory (not timed) $(date -u +%Y-%m-%dT%H:%M:%SZ); libcfdcore.a $(sha256sum $B/src/libcfdcore.a | cut -d' ' -f1)"
  echo "# perf_baseline.cpp $(sha256sum $P/tools/perf_baseline.cpp | cut -c1-16) (included unchanged); perf_memory.cpp $(sha256sum $P/tools/perf_memory.cpp | cut -c1-16)"
  c++ -std=c++20 -O3 -DNDEBUG -I$P/tools -I$R/include -I$B/generated/include -I$B/_deps/nlohmann_json-src/include \
    $P/tools/perf_memory.cpp $B/src/libcfdcore.a -o $W/perf_memory || { echo "build failed"; exit 1; }
  for item in none geom2d128 geom2d256 geom3d32 geom3d64 solvers128; do
    (cd $R && /usr/bin/time -v taskset -c 3 $W/perf_memory $item > $W/$item.out 2> $W/$item.time)
    rc=$?
    echo "$item: exit $rc; peak RSS $(sed -n 's/.*Maximum resident set size (kbytes): //p' $W/$item.time) kB; $(grep -E '^(GEOM|SOLV)' $W/$item.out | sed 's/ \+/ /g' | cut -c1-60 | tr '\n' ';')"
  done
  echo "# PISO performs two pressure corrections per step by construction (PISO.hpp); TransientStepResult exposes no linear-solver iteration counts, so none are reported."
  echo "# end $(date -u +%Y-%m-%dT%H:%M:%SZ)"
} > $LOG 2>&1
cat $LOG
