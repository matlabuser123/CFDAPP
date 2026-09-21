#!/usr/bin/env bash
# GPU-DISC-001R Phase H -- a real production case through the normal CLI.
#
# A FINDING SHAPES THIS PHASE. `SIMPLESettings::enableGpuDiscretization` has NO
# case-file key: solver.json accepts 14 keys (SolverConfigParser.cpp:222-227)
# and rejectUnknownKeys hard-errors on anything else. The case format exposes
# `backend: "GPU"` for the LINEAR SOLVERS only (P6-GPU-002). So the integrated
# CUDA discretization qualified by GPU-DISC-001M is reachable only through the
# C++ API, not from a case file.
#
# That is reported, not worked around. This phase therefore does both halves:
#
#   H1  the normal CLI/case workflow on a real case with the GPU linear-solver
#       backend -- the user-facing path, end to end, exactly as shipped;
#   H2  the integrated GPU discretization through the production SIMPLE::solve
#       entry point, which is production code rather than a test double.
#
# Neither is skipped and neither is dressed up as the other.
set -uo pipefail
ROOT=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
EVID=$ROOT/results/gpu-disc-001/full-regression/production-smoke
BUILD=$ROOT/build/final
CUDA=/usr/local/cuda-12.9
WORK=/tmp/gpudisc-smoke
cd "$ROOT"
mkdir -p "$EVID"
rm -rf "$WORK"; mkdir -p "$WORK"

CLI="$BUILD/apps/cli/cfdapp"
[ -x "$CLI" ] || CLI=$(find "$BUILD" -name "cfdapp" -type f -perm -u+x | head -1)
echo "=== CLI ==="
echo "  $CLI"
"$CLI" --version 2>&1 | head -2 | sed 's/^/  /'

# ---------------------------------------------------------------- H1
echo ""
echo "########## H1: normal CLI workflow, GPU linear-solver backend ##########"
cp -r cases/lid_driven_cavity_40x40 "$WORK/case"
rm -rf "$WORK/case/results"
python3 - "$WORK/case/solver.json" <<'PY'
import json, sys
p = sys.argv[1]
c = json.load(open(p))
# Only the documented, parser-accepted key. Tolerances and iteration budgets
# are left exactly as the shipped case defines them.
c["momentum_linear_solver"]["backend"] = "GPU"
c["pressure_linear_solver"]["backend"] = "GPU"
json.dump(c, open(p, "w"), indent=2)
print("  momentum/pressure linear solver backend -> GPU (the only GPU key the case format has)")
PY

echo "  command: $CLI --case $WORK/case"
( cd "$WORK" && "$CLI" --case "$WORK/case" ) > "$EVID/cli-gpu-run.log" 2>&1
rc=$?
echo "  exit code: $rc"
tail -25 "$EVID/cli-gpu-run.log" | sed 's/^/  /'

echo ""
echo "  --- checks ---"
# The CLI prints its own structured verdict. PARSE THOSE FIELDS rather than
# grepping the log for substrings: a naive `grep -i nan|inf` matches the CLI's
# own "NaN/Inf: no" LABEL and fails a perfectly good run. (It did exactly that
# on the first attempt.) A check that cannot tell a label from a value is not a
# check.
fail=0
[ "$rc" -ne 0 ] && { echo "  FAIL CLI exit code $rc"; fail=1; }

converged=$(grep -E "^Converged:" "$EVID/cli-gpu-run.log" | awk '{print $2}')
naninf=$(grep -E "^NaN/Inf:" "$EVID/cli-gpu-run.log" | awk '{print $2}')
imbalance=$(grep -E "^Mass imbalance:" "$EVID/cli-gpu-run.log" | awk '{print $3}')
iters=$(grep -E "^Iterations:" "$EVID/cli-gpu-run.log" | awk '{print $2}')
contin=$(grep -E "^Continuity:" "$EVID/cli-gpu-run.log" | awk '{print $2}')

printf "  converged=%s  iterations=%s  continuity=%s  massImbalance=%s  NaN/Inf=%s\n" \
  "$converged" "$iters" "$contin" "$imbalance" "$naninf"
[ "$converged" != "yes" ] && { echo "  FAIL did not converge"; fail=1; }
[ "$naninf" != "no" ]     && { echo "  FAIL the CLI reports NaN/Inf present"; fail=1; }
# A genuine crash would show as a non-zero exit or a terminate message; look for
# the exception text the project actually emits, not the word "error".
grep -qE "terminate called|Segmentation fault|what\(\):" "$EVID/cli-gpu-run.log" \
  && { echo "  FAIL crash text in output"; fail=1; }

out=$(find "$WORK/case/results" -type f 2>/dev/null | wc -l)
echo "  output files produced: $out"
[ "$out" -eq 0 ] && { echo "  FAIL no output produced"; fail=1; }
find "$WORK/case/results" -name "*.json" | head -3 | while read -r f; do
  echo "    $(basename "$f")"
done
cp -r "$WORK/case/results" "$EVID/cli-results" 2>/dev/null

# ---------------------------------------------------------------- H2
echo ""
echo "########## H2: integrated GPU discretization via production SIMPLE::solve ##########"
echo "  (the 001O diagnostic workload drives SIMPLE::solve with enableGpuDiscretization;"
echo "   it is the production entry point, not a harness reimplementation)"
/usr/bin/c++ -I"$ROOT/include" -I"$BUILD/generated/include" \
  -I"$BUILD/_deps/nlohmann_json-src/include" -isystem "$CUDA/include" \
  -O2 -DNDEBUG -std=c++20 \
  -o /tmp/final_prod_workload \
  "$ROOT/results/gpu-disc-001/cuda-diagnostics/tools/diagnostic_workload.cpp" \
  -Wl,--start-group "$BUILD/cuda/libcfdcuda.a" "$BUILD/src/libcfdcore.a" -Wl,--end-group \
  -L"$CUDA/lib64" -lcudart || { echo "  build failed"; exit 1; }

for mode in cavity2d inletoutlet2d case3d; do
  /tmp/final_prod_workload "$mode" > "$EVID/production-path-$mode.log" 2>&1
  rc=$?
  line=$(grep -E "^DIAGNOSTIC WORKLOAD:" "$EVID/production-path-$mode.log" | tail -1)
  kern=$(grep -oE "kernelLaunches=[0-9]+" "$EVID/production-path-$mode.log" | tail -1)
  printf "  %-16s rc=%s  %s  %s\n" "$mode" "$rc" "$kern" "$line"
  [ $rc -ne 0 ] && fail=1
done

echo ""
if [ $fail -eq 0 ]; then
  echo "PRODUCTION SMOKE: PASS"
else
  echo "PRODUCTION SMOKE: FAIL"
fi
exit $fail
