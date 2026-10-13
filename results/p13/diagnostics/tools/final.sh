#!/usr/bin/env bash
# P13-005 final: P3 (no hot-loop logging), then the full regression.
cd /mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
export PATH=/usr/local/cuda-12.9/bin:$PATH
echo "date: $(date -u +%FT%TZ)"

echo
echo "=== P3: no logging in a hot loop ==="
echo "-- static: Logger call sites inside the SIMPLE outer-iteration loop --"
python3 - <<'PY'
import io,re
s=io.open('src/pressure_velocity/SIMPLE.cpp',encoding='utf-8').read()
# the outer loop body: from "for (Index iteration" to the matching closing of solve()
i=s.find('for (Index iteration')
sites=[m.start() for m in re.finditer(r'Logger::instance\(\)', s)]
print('  Logger call sites in SIMPLE.cpp:', len(sites))
print('  inside the outer-iteration loop  :', sum(1 for p in sites if p > i))
PY
echo "-- dynamic: stderr lines must not grow with the iteration count --"
for it in 5 50; do
  W=/tmp/p13_p3_$it; rm -rf $W; cp -r tests/data/cases/valid_cavity $W; rm -rf $W/results
  python3 - $W/solver.json $it <<'PY'
import json,sys
d=json.load(open(sys.argv[1])); d["max_iterations"]=int(sys.argv[2])
json.dump(d,open(sys.argv[1],"w"),indent=2)
PY
  n=$(build/final/apps/cli/cfdapp --case $W --log-level trace 2>&1 >/dev/null | wc -l)
  echo "  max_iterations=$it -> stderr lines at --log-level trace: $n"
done

echo
echo "=== P2: the P13-001..004 gates still pass ==="
( cd build/final && ctest -R "Restart|Checkpoint|CliGui|cli_gui|ResultExport|ExportWriteFailure|CaseDiagnostic" \
    --output-on-failure 2>&1 | tail -8 )

echo
echo "=== G: clang-format over the whole tree ==="
V=0
for f in $(git ls-files '*.cpp' '*.hpp' | grep -v '\.hpp\.in$'); do
  clang-format --style=file "$f" | diff -q - "$f" >/dev/null || { echo "  FORMAT: $f"; V=$((V+1)); }
done
echo "  violations: $V"

echo
echo "=== G: Release + CUDA (build/final) ==="
cmake --build build/final -j 12 2>&1 | tail -2
( cd build/final && ctest -j 4 2>&1 | tail -4; echo "  RC=$?" )

echo
echo "=== G: Debug + GUI (build/debug) ==="
cmake --build build/debug -j 12 2>&1 | tail -2
( cd build/debug && ctest -j 4 2>&1 | tail -4; echo "  RC=$?" )

echo
echo "=== G: CPU-only (build/cpuonly) ==="
cmake --build build/cpuonly -j 12 2>&1 | tail -2
( cd build/cpuonly && ctest -j 4 2>&1 | tail -4; echo "  RC=$?" )
echo
echo "done: $(date -u +%FT%TZ)"
