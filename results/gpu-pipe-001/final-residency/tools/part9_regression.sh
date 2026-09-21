#!/usr/bin/env bash
# GPU-PIPE-001 Final Residency, Part 9 -- the final regression.
#
# Reuses the GPU-DISC-001R regression tools rather than reimplementing them, so
# the harnesses are the ones already qualified. Those tools write to HARDCODED
# evidence paths under results/gpu-disc-001/full-regression/, which means each
# reuse overwrites the previous phase's logs -- so every output is COPIED into
# this milestone's own evidence directory immediately after it is produced.
# Without that copy this gate's regression record would be indistinguishable
# from the previous gate's.
#
# Failures are recorded and the run CONTINUES, because the brief requires every
# failure preserved rather than one failure hiding the state of everything
# after it. The gate decision is made from the summary at the end.
set -uo pipefail
ROOT=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
T=$ROOT/results/gpu-disc-001/full-regression/tools
SRC=$ROOT/results/gpu-disc-001/full-regression
E=$ROOT/results/gpu-pipe-001/final-residency/regression
BUILD=$ROOT/build/final
cd "$ROOT"
mkdir -p "$E"
declare -A RC

snapshot() {  # snapshot <source-path> <destination-name>
  [ -e "$1" ] && cp -r "$1" "$E/$2" 2>/dev/null
}

echo "##################### 1. build freshness, before anything runs #####################"
ninja -C "$BUILD" 2>&1 | tail -1 | tee "$E/01-freshness-before.txt"
{
  echo "# binaries under test"
  sha256sum "$BUILD/cuda/libcfdcuda.a" "$BUILD/src/libcfdcore.a"
  echo ""
  echo "# newest production source vs oldest test binary"
  newest=$(find src include cuda apps -type f \( -name '*.cpp' -o -name '*.hpp' -o -name '*.cu' \) \
    -printf '%T@ %p\n' | sort -rn | head -1)
  echo "newest source: $newest"
  find "$BUILD" -name 'CFD*Tests' -type f -printf '%T@ %p\n' | sort -n | head -1 \
    | sed 's/^/oldest test binary: /'
} >> "$E/01-freshness-before.txt"
cat "$E/01-freshness-before.txt"

echo ""
echo "##################### 2. complete CTest on the production build #####################"
ctest --test-dir "$BUILD" --output-on-failure > "$E/02-ctest.log" 2>&1
RC[ctest]=$?
grep -E "tests passed|tests failed|Total Test time|\(Failed\)" "$E/02-ctest.log" | sed 's/^/  /'
ctest --test-dir "$BUILD" -N > "$E/02-ctest-discovery.txt" 2>&1
tail -1 "$E/02-ctest-discovery.txt" | sed 's/^/  /'
echo "  rc=${RC[ctest]}"
# Freshness AFTER: if ninja finds work now, what was tested was not what is on disk.
ninja -C "$BUILD" 2>&1 | tail -1 | tee "$E/02-freshness-after-ctest.txt" | sed 's/^/  freshness: /'

echo ""
echo "##################### 3. the 15 GPU-DISC differential gates #####################"
bash "$T/gpu_gates.sh" > "$E/03-gpu-gates.log" 2>&1
RC[gates]=$?
tail -2 "$E/03-gpu-gates.log" | sed 's/^/  /'
echo "  rc=${RC[gates]}"

echo ""
echo "##################### 4. determinism #####################"
bash "$T/determinism.sh" > "$E/04-determinism.log" 2>&1
RC[determinism]=$?
grep -E "^  (PASS|FAIL) |DETERMINISM:" "$E/04-determinism.log" | sed 's/^/  /'
echo "  rc=${RC[determinism]}"

echo ""
echo "##################### 5. production GPU CLI smoke #####################"
bash "$T/production_smoke.sh" > "$E/05-production-smoke.log" 2>&1
RC[smoke]=$?
grep -E "exit code|output files|rc=|PRODUCTION SMOKE|FAIL" "$E/05-production-smoke.log" | sed 's/^/  /'
snapshot "$SRC/production-smoke" "05-production-smoke-dir"
echo "  rc=${RC[smoke]}"

echo ""
echo "##################### 6. CPU-only build and full test suite #####################"
bash "$T/cpu_only.sh" > "$E/06-cpu-only.log" 2>&1
RC[cpuonly]=$?
grep -E "rc=|tests passed|tests failed|nvcc invocations|does not link|FAIL" "$E/06-cpu-only.log" \
  | sed 's/^/  /'
snapshot "$SRC/cpu-only" "06-cpu-only-dir"
echo "  rc=${RC[cpuonly]}"

echo ""
echo "##################### 7. sanitizer freshness #####################"
bash "$T/sanitizer_freshness.sh" > "$E/07-sanitizer-freshness.log" 2>&1
RC[sanitizer]=$?
tail -5 "$E/07-sanitizer-freshness.log" | sed 's/^/  /'
echo "  rc=${RC[sanitizer]}"

echo ""
echo "##################### 8. no negative-control mutation remains #####################"
# Every file any control in this milestone (or its predecessors) mutates, hashed
# and grepped for the mutation markers the control engine writes.
{
  echo "# files every negative control touches, and their current sha256"
  sha256sum src/pressure_velocity/SIMPLE.cpp \
    cuda/kernels/GpuSimpleDiscretizationCuda.cpp \
    cuda/kernels/GpuResidentSolveKernel.cu \
    cuda/kernels/GpuLinearSolverCuda.cpp \
    cuda/kernels/DevicePressureCorrectionPlan.cpp \
    cuda/kernels/DevicePersistentFieldsKernel.cu \
    include/cfd/gpu/GpuSimpleDiscretization.hpp \
    include/cfd/gpu/GpuResidentSolve.hpp \
    include/cfd/gpu/DevicePersistentFields.hpp
  echo ""
  echo "# any surviving mutation marker (must be empty)"
  grep -rn "MUTATED" src include cuda apps tests 2>/dev/null || echo "  none"
} > "$E/08-mutation-audit.txt" 2>&1
if grep -q "MUTATED" "$E/08-mutation-audit.txt"; then RC[mutations]=1; else RC[mutations]=0; fi
tail -3 "$E/08-mutation-audit.txt" | sed 's/^/  /'
echo "  rc=${RC[mutations]}"

echo ""
echo "##################### 9. working-tree audit #####################"
{
  echo "# git status --porcelain"
  git status --porcelain
  echo ""
  echo "# git diff --check"
  git diff --check && echo "  clean"
  echo ""
  echo "# tracked files modified by this milestone"
  git diff --name-only
} > "$E/09-worktree.txt" 2>&1
RC[worktree]=0
grep -cE "^ M" "$E/09-worktree.txt" | xargs -I{} echo "  tracked modified: {}"
grep -cE "^\?\?" "$E/09-worktree.txt" | xargs -I{} echo "  untracked entries: {}"

echo ""
echo "##################### 10. clang-format #####################"
mapfile -t FMT < <(find include src apps tests -type f \( -name '*.cpp' -o -name '*.hpp' \))
clang-format-18 --dry-run --Werror "${FMT[@]}" > "$E/10-clang-format.log" 2>&1
RC[format]=$?
if [ "${RC[format]}" -eq 0 ]; then echo "  clean (${#FMT[@]} files)"; else
  grep -E "error" "$E/10-clang-format.log" | head -10 | sed 's/^/  /'; fi
echo "  rc=${RC[format]}"

echo ""
echo "############################## PART 9 SUMMARY ##############################"
bad=0
for k in ctest gates determinism smoke cpuonly sanitizer mutations worktree format; do
  printf "  %-13s rc=%s  %s\n" "$k" "${RC[$k]}" \
    "$([ "${RC[$k]}" -eq 0 ] && echo PASS || echo FAIL)"
  [ "${RC[$k]}" -ne 0 ] && bad=1
done
[ $bad -eq 0 ] && echo "FINAL REGRESSION: ALL PASS" || echo "FINAL REGRESSION: FAILURES PRESENT"
exit $bad
