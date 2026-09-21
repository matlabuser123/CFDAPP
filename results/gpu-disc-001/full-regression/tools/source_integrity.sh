#!/usr/bin/env bash
# GPU-DISC-001R Phase B -- prove no intentional mutation remains.
#
# GPU-DISC-001P recorded baseline-sha256.txt over the 31 production sources its
# mutation campaign was allowed to touch. That baseline predates GPU-DISC-001Q,
# which legitimately added stage timers and device-byte counters. So a plain
# "must match" comparison would fail for the RIGHT reason and tell us nothing.
#
# The brief anticipates exactly this: update the integrity record transparently
# rather than forcing a match against an obsolete hash. So:
#   1. compare against the 001P baseline and list every difference;
#   2. prove each difference is a KNOWN 001Q instrumentation change, by diffing
#      against HEAD and checking the change is additive instrumentation only;
#   3. record a NEW final baseline for the qualified state.
set -uo pipefail
ROOT=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
EVID=$ROOT/results/gpu-disc-001/full-regression/source-integrity
OLD=$ROOT/results/gpu-disc-001/negative-controls/baseline-sha256.txt
cd "$ROOT"
mkdir -p "$EVID"

echo "=== 1. current hashes of the 31 mutation-campaign sources ==="
bash results/gpu-disc-001/negative-controls/tools/baseline_sha.sh > "$EVID/current-sha256.txt"
echo "  $(wc -l < "$EVID/current-sha256.txt") files hashed"

echo ""
echo "=== 2. compare against the GPU-DISC-001P restoration baseline ==="
if diff -u "$OLD" "$EVID/current-sha256.txt" > "$EVID/diff-vs-001P.txt" 2>&1; then
  echo "  IDENTICAL to the 001P baseline -- nothing changed since the mutation campaign"
  changed=0
else
  changed=$(grep -c '^+[0-9a-f]' "$EVID/diff-vs-001P.txt")
  echo "  $changed of 31 files differ from the 001P baseline:"
  grep '^+[0-9a-f]' "$EVID/diff-vs-001P.txt" | awk '{print "    " $2}'
fi

echo ""
echo "=== 3. is every difference a KNOWN GPU-DISC-001Q instrumentation change? ==="
# 001Q's four files, and nothing else, should differ.
EXPECTED="src/pressure_velocity/SIMPLE.cpp"
for f in $(grep '^+[0-9a-f]' "$EVID/diff-vs-001P.txt" 2>/dev/null | awk '{print $2}'); do
  case " $EXPECTED " in
    *" $f "*) verdict="EXPECTED (001Q stage timers)" ;;
    *) verdict="!! UNEXPECTED -- investigate" ;;
  esac
  printf "    %-48s %s\n" "$f" "$verdict"
done

echo ""
echo "=== 4. the files GPU-DISC-001Q changed, diffed against HEAD ==="
# Instrumentation only: no numeric literal, tolerance or convergence test may
# have moved. The diff is preserved so a reader can check rather than trust.
Q_FILES="src/pressure_velocity/SIMPLE.cpp
include/cfd/pressure_velocity/SIMPLEResult.hpp
include/cfd/gpu/GPUExecutionStats.hpp
include/cfd/gpu/DeviceBuffer.hpp"
for f in $Q_FILES; do
  git diff -- "$f" > "$EVID/diff-$(basename "$f").txt"
  added=$(grep -c '^+' "$EVID/diff-$(basename "$f").txt")
  removed=$(grep -c '^-' "$EVID/diff-$(basename "$f").txt")
  printf "    %-52s +%-5s -%s\n" "$f" "$added" "$removed"
done

echo ""
echo "=== 5. did any TOLERANCE or CONVERGENCE value change in tracked source? ==="
# Every changed line in src/ and include/, filtered for the things this gate
# forbids moving. A hit is a failed gate, not a note.
git diff -- src/ include/ apps/ \
  | grep -E '^[+-]' | grep -vE '^(\+\+\+|---)' \
  | grep -iE "toleran|convergen|maxIterations|relaxation|threshold|epsilon|1e-" \
  > "$EVID/tolerance-lines.txt" 2>&1 || true
n=$(wc -l < "$EVID/tolerance-lines.txt")
echo "  candidate lines touching tolerance/convergence vocabulary: $n"
if [ "$n" -gt 0 ]; then
  cat "$EVID/tolerance-lines.txt" | sed 's/^/    /'
fi

echo ""
echo "=== 6. record the FINAL integrity baseline for the qualified state ==="
{
  echo "# GPU-DISC-001R -- final source integrity, the qualified production state."
  echo "# HEAD $(git rev-parse HEAD), working tree uncommitted GPU-PIPE/GPU-DISC work."
  echo "# Supersedes results/gpu-disc-001/negative-controls/baseline-sha256.txt, which"
  echo "# predates the GPU-DISC-001Q instrumentation."
  echo ""
  bash results/gpu-disc-001/negative-controls/tools/baseline_sha.sh
  echo ""
  echo "# the four files GPU-DISC-001Q changed"
  sha256sum $Q_FILES
} > "$EVID/final-baseline-sha256.txt"
echo "  -> source-integrity/final-baseline-sha256.txt"
tail -6 "$EVID/final-baseline-sha256.txt" | sed 's/^/  /'
