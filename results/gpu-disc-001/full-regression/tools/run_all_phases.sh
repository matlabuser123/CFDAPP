#!/usr/bin/env bash
# GPU-DISC-001R -- phases D through L, sequentially, on the FINAL clean build.
#
# Sequential by necessity: ctest and the GPU phases would contend, and a
# contended GPU timing is a wrong timing. Each phase's failure is recorded and
# the run CONTINUES, so one failure does not hide the state of everything after
# it -- the brief requires preserving every failure, not stopping at the first.
set -uo pipefail
ROOT=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
T=$ROOT/results/gpu-disc-001/full-regression/tools
E=$ROOT/results/gpu-disc-001/full-regression
BUILD=$ROOT/build/final
cd "$ROOT"

declare -A RC

echo "##################### D: complete CTest on the final build #####################"
ctest --test-dir "$BUILD" --output-on-failure > "$E/ctest/ctest.log" 2>&1
RC[D_ctest]=$?
grep -E "tests passed|tests failed|Total Test time" "$E/ctest/ctest.log" | sed 's/^/  /'
ctest --test-dir "$BUILD" -N > "$E/ctest/discovery.txt" 2>&1
tail -1 "$E/ctest/discovery.txt" | sed 's/^/  /'
grep -cE "Disabled" "$E/ctest/ctest.log" | xargs -I{} echo "  disabled mentions: {}"
echo "  rc=${RC[D_ctest]}"
ninja -C "$BUILD" 2>&1 | tail -1 | sed 's/^/  freshness after ctest: /'

echo ""
echo "##################### E: 15 GPU-DISC gates on final binaries #####################"
bash "$T/gpu_gates.sh" > "$E/gpu-gates/gates.log" 2>&1
RC[E_gates]=$?
tail -3 "$E/gpu-gates/gates.log" | sed 's/^/  /'
echo "  rc=${RC[E_gates]}"

echo ""
echo "##################### I: determinism #####################"
bash "$T/determinism.sh" > "$E/determinism/run.log" 2>&1
RC[I_determinism]=$?
grep -E "^  (PASS|FAIL) |DETERMINISM:" "$E/determinism/run.log" | sed 's/^/  /'
echo "  rc=${RC[I_determinism]}"

echo ""
echo "##################### H: production smoke #####################"
bash "$T/production_smoke.sh" > "$E/production-smoke/run.log" 2>&1
RC[H_smoke]=$?
grep -E "exit code|output files|rc=|PRODUCTION SMOKE|FAIL" "$E/production-smoke/run.log" | sed 's/^/  /'
echo "  rc=${RC[H_smoke]}"

echo ""
echo "##################### L: performance smoke #####################"
bash "$T/perf_smoke.sh" > "$E/performance-smoke/run.log" 2>&1
RC[L_perf]=$?
grep -E "^  (cpu|gpu-disc) |PERFORMANCE SMOKE|no timing regression|FINDING" \
  "$E/performance-smoke/run.log" | sed 's/^/  /'
echo "  rc=${RC[L_perf]}"

echo ""
echo "##################### J: sanitizer freshness #####################"
bash "$T/sanitizer_freshness.sh" > "$E/sanitizer-freshness/run.log" 2>&1
RC[J_sanitizer]=$?
tail -4 "$E/sanitizer-freshness/run.log" | sed 's/^/  /'
echo "  rc=${RC[J_sanitizer]}"

echo ""
echo "##################### G: CPU-only build and tests #####################"
bash "$T/cpu_only.sh" > "$E/cpu-only/run.log" 2>&1
RC[G_cpuonly]=$?
grep -E "rc=|tests passed|tests failed|nvcc invocations|does not link|FAIL" \
  "$E/cpu-only/run.log" | sed 's/^/  /'
echo "  rc=${RC[G_cpuonly]}"

echo ""
echo "############################## PHASE SUMMARY ##############################"
bad=0
for k in D_ctest E_gates I_determinism H_smoke L_perf J_sanitizer G_cpuonly; do
  printf "  %-16s rc=%s  %s\n" "$k" "${RC[$k]}" \
    "$([ "${RC[$k]}" -eq 0 ] && echo PASS || echo FAIL)"
  [ "${RC[$k]}" -ne 0 ] && bad=1
done
echo ""
[ $bad -eq 0 ] && echo "GPU-DISC-001R PHASES D-L: ALL PASS" || echo "GPU-DISC-001R PHASES D-L: FAILURES PRESENT"
exit $bad
