#!/usr/bin/env bash
# GPU-DISC-001P -- re-execute the six gates that DID preserve a driver.
#
# These are run VERBATIM rather than transcribed into the consolidated engine.
# Transcription is the one way a control can silently stop testing what its name
# says, and there is no reason to take that risk when the original definitions
# still exist and still apply to the current tree.
#
# Each driver refreshes its own gate's per-control logs, so the previous
# driver.log -- the authoritative verdict record -- is snapshotted here FIRST.
# A re-run that changed a verdict must remain visible.
#
# One campaign at a time: every driver mutates the shared working tree.
set -uo pipefail

ROOT=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
EVID=$ROOT/results/gpu-disc-001/negative-controls
cd "$ROOT"

# gate directory : evidence directory under negative-controls/
GATES="
pressure-correction-assembly:pressure-correction
velocity-correction:velocity-correction
face-flux-correction:face-flux-correction
single-iteration:single-iteration
integrated-simple:integrated-simple
full-solve-equivalence:full-solve
"

echo "=== snapshot the historical verdict records before anything is re-run ==="
for pair in $GATES; do
  gate=${pair%%:*}
  dest=${pair##*:}
  mkdir -p "$EVID/$dest"
  if [ -f "results/gpu-disc-001/$gate/negative-control/driver.log" ]; then
    cp "results/gpu-disc-001/$gate/negative-control/driver.log" \
       "$EVID/$dest/historical-driver.log"
    echo "  saved $dest/historical-driver.log"
  elif [ -f "results/gpu-disc-001/$gate/negative-controls/driver.log" ]; then
    cp "results/gpu-disc-001/$gate/negative-controls/driver.log" \
       "$EVID/$dest/historical-driver.log"
    echo "  saved $dest/historical-driver.log"
  else
    echo "  !! no historical driver.log for $gate"
  fi
done

echo ""
fail=0
for pair in $GATES; do
  gate=${pair%%:*}
  dest=${pair##*:}
  log=$EVID/$dest/driver.log
  echo "########## $gate ##########"
  start=$(date +%s)
  python3 "results/gpu-disc-001/$gate/tools/negative_controls.py" > "$log" 2>&1
  rc=$?
  elapsed=$(( $(date +%s) - start ))
  tail -5 "$log"
  echo "  rc=$rc  ${elapsed}s  -> $dest/driver.log"
  [ $rc -ne 0 ] && fail=1
  echo ""
done

echo "=== did any verdict change since the historical run? ==="
for pair in $GATES; do
  dest=${pair##*:}
  h=$EVID/$dest/historical-driver.log
  n=$EVID/$dest/driver.log
  [ -f "$h" ] || continue
  # Compare only the per-control verdict lines; timings and hashes differ.
  a=$(grep -oE "(DETECTED|UNDETECTED|BUILD-FAILED)" "$h" | tr '\n' ' ')
  b=$(grep -oE "(DETECTED|UNDETECTED|BUILD-FAILED)" "$n" | tr '\n' ' ')
  if [ "$a" = "$b" ]; then
    echo "  $dest: verdicts IDENTICAL to the historical run"
  else
    echo "  $dest: VERDICTS CHANGED"
    echo "    was: $a"
    echo "    now: $b"
    fail=1
  fi
done

echo ""
if [ $fail -eq 0 ]; then
  echo "INTEGRATION CAMPAIGN: PASS"
else
  echo "INTEGRATION CAMPAIGN: FAIL"
fi
exit $fail
