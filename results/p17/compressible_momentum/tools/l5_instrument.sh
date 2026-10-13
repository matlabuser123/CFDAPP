#!/usr/bin/env bash
# P17.4 -- prove the REPLACEMENT L5 instrument is not vacuous.
#
# The original probe (git diff --numstat HEAD) was disqualified because it reported "touched"
# for a file P17.4 never opened. A replacement must pass the test the original failed: silent
# at the untouched baseline (L5a) AND firing on a real touch (L5b). A probe that only ever
# prints PASS would be exactly the kind of instrument this amendment exists to reject.
set -uo pipefail
ROOT=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
P17=$ROOT/results/p17/compressible_momentum
cd "$ROOT" || exit 1

CPP=src/physics/MomentumEquation.cpp
HPP=include/cfd/physics/MomentumEquation.hpp
FROZEN_CPP=e46d49fcda78e0271538bc69117e249b0ea50dd2265c5f603d64d4206dc7bb59
FROZEN_HPP=7feca5c4a3a22943c442d1fa162b4e860ab037b69a3c6a6ce0e6c413bb0e37af

# the replacement instrument, as it will live in run_gate.sh
l5_probe() {
  local a b
  a=$(sha256sum "$CPP" | awk '{print $1}')
  b=$(sha256sum "$HPP" | awk '{print $1}')
  if [ "$a" = "$FROZEN_CPP" ] && [ "$b" = "$FROZEN_HPP" ]; then
    echo "UNTOUCHED"
  else
    echo "*** TOUCHED ***"
  fi
}

log=$P17/logs/09_L5_instrument.log
{
echo "P17.4 -- L5 replacement instrument, non-vacuity proof"
echo "date: $(date -u +%FT%TZ)"
echo ""
echo "frozen expectations (logs/00_freeze.log, '### NOT to be touched ###')"
echo "  $FROZEN_CPP  $CPP"
echo "  $FROZEN_HPP  $HPP"
echo ""

echo "=== L5a -- the untouched baseline: the probe must be SILENT ==="
echo "  measured now:"
sha256sum "$CPP" "$HPP" | sed 's/^/    /'
echo "  probe verdict: $(l5_probe)   (must be UNTOUCHED)"
echo "  the ORIGINAL probe, for comparison, on this SAME untouched tree:"
echo "    git diff --numstat HEAD -> '$(git diff --numstat HEAD -- "$CPP" "$HPP" | tr '\n' ' ')'"
echo "    i.e. the original called an UNTOUCHED file touched. That is the disqualification."
echo ""

echo "=== L5b -- a real touch: the probe must FIRE ==="
cp "$CPP" "$P17/controls/MomentumEquation.cpp.pristine"
printf '\n// L5b probe control -- appended, then reverted.\n' >> "$CPP"
echo "  planted: one comment line appended to $CPP"
echo "  probe verdict: $(l5_probe)   (must be *** TOUCHED ***)"
cp "$P17/controls/MomentumEquation.cpp.pristine" "$CPP"
echo "  restored; probe verdict: $(l5_probe)   (must be UNTOUCHED again)"
rm -f "$P17/controls/MomentumEquation.cpp.pristine"
echo ""

echo "=== final state, proved back at the freeze ==="
sha256sum "$CPP" "$HPP" | sed 's/^/  /'
echo "  L5b markers left: $(grep -c 'L5b probe control' "$CPP" "$HPP" | awk -F: '{s+=$2} END{print s}')  (0 required)"
echo ""
echo "done: $(date -u +%FT%TZ)"
} 2>&1 | tee "$log"
