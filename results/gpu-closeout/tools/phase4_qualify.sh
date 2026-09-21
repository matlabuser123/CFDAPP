#!/usr/bin/env bash
# GPU closeout Phase 4 -- final local qualification of the EXACT tree that will
# be committed.
#
# Everything runs sequentially. ctest and the GPU phases would contend, and a
# contended GPU timing is a wrong timing.
#
# WHY THE SANITIZERS ARE RE-RUN. The Part 6 CUDA diagnostics were produced
# before the clang-format remediation, which touched ten CUDA headers --
# DeviceBuffer.hpp and DeviceBoundaryConditions.hpp among them. The change is
# whitespace plus one reordered #include and is proven semantically null
# (libcfdcore.a byte-identical), but the source IDENTITY differs from the
# identity those runs recorded, and memory/synchronization headers are exactly
# what the brief names. Re-running is cheaper than arguing.
#
# Failures are recorded and the run CONTINUES, so one failure does not hide the
# state of everything after it. The gate decision is made from the summary.
set -uo pipefail
ROOT=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
E=$ROOT/results/gpu-closeout/local-qualification
T=$ROOT/results/gpu-pipe-001/final-residency/tools
G=$ROOT/results/gpu-disc-001/full-regression/tools
CUDA=/usr/local/cuda-12.9
cd "$ROOT"
mkdir -p "$E"
declare -A RC

echo "##################### A. clean Release+CUDA build #####################"
bash "$G/clean_build.sh" > "$E/01-clean-build.log" 2>&1
RC[build]=$?
grep -E "^  removed|configure rc|build rc|targets built|Linking|error" "$E/01-clean-build.log" \
  | tail -4 | sed 's/^/  /'
echo "  rc=${RC[build]}"

echo ""
echo "##################### B. build freshness #####################"
{
  echo "# binaries under test"
  sha256sum build/final/cuda/libcfdcuda.a build/final/src/libcfdcore.a
  echo ""
  echo "# newest production source"
  find src include cuda apps -type f \( -name '*.cpp' -o -name '*.hpp' -o -name '*.cu' \) \
    -printf '%T@ %p\n' | sort -rn | head -1
  echo "# oldest test binary"
  find build/final -name 'CFD*Tests' -type f -printf '%T@ %p\n' | sort -n | head -1
  echo ""
  echo "# ninja after the build (must be: no work to do)"
  ninja -C build/final 2>&1 | tail -1
} > "$E/02-freshness.txt"
RC[fresh]=$(grep -q "no work to do" "$E/02-freshness.txt" && echo 0 || echo 1)
tail -6 "$E/02-freshness.txt" | sed 's/^/  /'
echo "  rc=${RC[fresh]}"

echo ""
echo "##################### C. complete CTest, Release + CUDA #####################"
ctest --test-dir build/final --output-on-failure > "$E/03-ctest-release.log" 2>&1
RC[ctest]=$?
grep -E "tests passed|tests failed|Total Test time" "$E/03-ctest-release.log" | sed 's/^/  /'
ninja -C build/final 2>&1 | tail -1 | sed 's/^/  freshness after ctest: /'
echo "  rc=${RC[ctest]}"

echo ""
echo "##################### D. 15 GPU-DISC differential gates #####################"
bash "$G/gpu_gates.sh" > "$E/04-gpu-gates.log" 2>&1
RC[gates]=$?
tail -2 "$E/04-gpu-gates.log" | sed 's/^/  /'
echo "  rc=${RC[gates]}"

echo ""
echo "##################### E. GPU-PIPE residency gates #####################"
for phase in transfers compare dispatch lifecycle; do
  bash "$T/run_phase.sh" "$phase" > "$E/05-residency-$phase.log" 2>&1
  RC[$phase]=$?
  grep -E "GUARD:|COMPARISON:|DISPATCH PROBE:|LIFECYCLE:|cases=" "$E/05-residency-$phase.log" \
    | tail -2 | sed 's/^/  /'
  echo "  $phase rc=${RC[$phase]}"
done

echo ""
echo "##################### F. full CPU/GPU equivalence #####################"
bash "$T/run_phase.sh" equiv > "$E/06-equivalence.log" 2>&1
RC[equiv]=$?
grep -E "cases=|EQUIVALENCE" "$E/06-equivalence.log" | tail -2 | sed 's/^/  /'
echo "  rc=${RC[equiv]}"

echo ""
echo "##################### G. production GPU CLI smoke #####################"
bash "$G/production_smoke.sh" > "$E/07-production-smoke.log" 2>&1
RC[smoke]=$?
grep -E "exit code|output files|PRODUCTION SMOKE" "$E/07-production-smoke.log" | sed 's/^/  /'
echo "  rc=${RC[smoke]}"

echo ""
echo "##################### H. determinism #####################"
bash "$G/determinism.sh" > "$E/08-determinism.log" 2>&1
RC[determinism]=$?
grep -E "DETERMINISM:" "$E/08-determinism.log" | sed 's/^/  /'
echo "  rc=${RC[determinism]}"

echo ""
echo "##################### I. CUDA diagnostics, re-run #####################"
bash "$T/run_phase.sh" diagnostics > "$E/09-cuda-diagnostics.log" 2>&1
RC[diagnostics]=$?
grep -cE "0 errors|0 hazards" "$E/09-cuda-diagnostics.log" | xargs -I{} echo "  clean runs: {} / 16"
grep -c "VACUOUS" "$E/09-cuda-diagnostics.log" | xargs -I{} echo "  vacuous runs: {}"
echo "  rc=${RC[diagnostics]}"

echo ""
echo "##################### J. CPU-only build and full suite #####################"
bash "$G/cpu_only.sh" > "$E/10-cpu-only.log" 2>&1
RC[cpuonly]=$?
grep -E "tests passed|tests failed|nvcc invocations|does not link" "$E/10-cpu-only.log" | sed 's/^/  /'
echo "  rc=${RC[cpuonly]}"

echo ""
echo "##################### K. Debug + GUI #####################"
cmake --build --preset debug -j 16 > "$E/11-debug-build.log" 2>&1
RC[debug_build]=$?
ctest --test-dir build/debug --output-on-failure -j 16 > "$E/11-debug-ctest.log" 2>&1
RC[debug]=$?
grep -E "tests passed|tests failed|Total Test time" "$E/11-debug-ctest.log" | sed 's/^/  /'
echo "  rc=${RC[debug]}"

echo ""
echo "##################### L. sanitizers at CI settings #####################"
cmake --build --preset asan -j 16 > "$E/12-asan-build.log" 2>&1
RC[asan_build]=$?
ASAN_OPTIONS=detect_leaks=1:halt_on_error=0 \
UBSAN_OPTIONS=print_stacktrace=1:halt_on_error=0 \
  ctest --test-dir build/asan --output-on-failure --timeout 7200 -j 16 \
  > "$E/12-asan-ctest.log" 2>&1
RC[asan]=$?
grep -E "tests passed|tests failed|Total Test time" "$E/12-asan-ctest.log" | sed 's/^/  /'
grep -ciE "ERROR: AddressSanitizer|runtime error:|LeakSanitizer" "$E/12-asan-ctest.log" \
  | xargs -I{} echo "  sanitizer reports: {}"
echo "  rc=${RC[asan]}"

echo ""
echo "############################## PHASE 4 SUMMARY ##############################"
bad=0
for k in build fresh ctest gates transfers compare dispatch lifecycle equiv smoke \
         determinism diagnostics cpuonly debug_build debug asan_build asan; do
  printf "  %-13s rc=%s  %s\n" "$k" "${RC[$k]}" \
    "$([ "${RC[$k]}" -eq 0 ] && echo PASS || echo FAIL)"
  [ "${RC[$k]}" -ne 0 ] && bad=1
done
[ $bad -eq 0 ] && echo "PHASE 4 LOCAL QUALIFICATION: ALL PASS" \
               || echo "PHASE 4 LOCAL QUALIFICATION: FAILURES PRESENT"
exit $bad
