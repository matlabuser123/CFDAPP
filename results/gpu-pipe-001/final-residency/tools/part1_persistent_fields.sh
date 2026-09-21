#!/usr/bin/env bash
# GPU-PIPE-001 final residency, Part 1 -- re-verify persistent GPU fields and
# record the transfer baseline BEFORE any SIMPLE-loop work.
#
#   1. build identity under test (so every later number names its binaries)
#   2. field authority / dirty state / lifecycle   (persistent-fields probe)
#   3. stage-level residency + per-iteration transfer baseline (new)
#   4. the 15 GPU-DISC differential gates, on these binaries
set -uo pipefail
ROOT=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
E=$ROOT/results/gpu-pipe-001/final-residency
BR=$ROOT/results/gpu-pipe-001/persistent-fields/tools/build_and_run.sh
BUILD=$ROOT/build/final
cd "$ROOT"
declare -A RC

echo "##################### 0. build identity under test #####################"
ninja -C "$BUILD" 2>&1 | tail -1
{
  echo "# GPU-PIPE-001 final residency Part 1 -- binaries under test"
  echo "date  : $(date -u '+%Y-%m-%dT%H:%M:%SZ')"
  sha256sum "$BUILD/cuda/libcfdcuda.a" "$BUILD/src/libcfdcore.a"
} | tee "$E/persistent-fields/build-identity.txt"

echo ""
echo "##################### 1. field authority / dirty state / lifecycle #####################"
bash "$BR" "$ROOT/results/gpu-pipe-001/persistent-fields/tools/dirty_state.cpp" \
  > "$E/persistent-fields/dirty-state.log" 2>&1
RC[dirty]=$?
grep -E "PASS|FAIL|DIRTY STATE" "$E/persistent-fields/dirty-state.log" | sed 's/^/  /'
echo "  -> rc=${RC[dirty]}"

echo ""
echo "##################### 2. stage residency + transfer baseline #####################"
bash "$BR" "$E/tools/residency_baseline.cpp" > "$E/transfers/before.log" 2>&1
RC[baseline]=$?
grep -E "PASS|FAIL|H2D|budgets|---|RESIDENCY BASELINE|responseCoef|momentum assembly" \
  "$E/transfers/before.log" | sed 's/^/  /'
echo "  -> rc=${RC[baseline]}"

echo ""
echo "##################### 3. the 15 GPU-DISC differential gates #####################"
bash "$ROOT/results/gpu-disc-001/full-regression/tools/gpu_gates.sh" \
  > "$E/persistent-fields/gpu-gates.log" 2>&1
RC[gates]=$?
grep -E "^(PASS|FAIL|BUILD-FAIL)|gates green" "$E/persistent-fields/gpu-gates.log" | sed 's/^/  /'
echo "  -> rc=${RC[gates]}"

echo ""
echo "############################## PART 1 SUMMARY ##############################"
bad=0
for k in dirty baseline gates; do
  printf "  %-10s rc=%s  %s\n" "$k" "${RC[$k]}" "$([ "${RC[$k]}" -eq 0 ] && echo PASS || echo FAIL)"
  [ "${RC[$k]}" -ne 0 ] && bad=1
done
[ $bad -eq 0 ] && echo "PART 1 PERSISTENT GPU FIELDS: RE-VERIFIED" \
               || echo "PART 1 PERSISTENT GPU FIELDS: FAILURES PRESENT"
exit $bad
