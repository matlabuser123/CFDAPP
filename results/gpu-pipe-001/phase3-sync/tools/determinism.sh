#!/usr/bin/env bash
# GPU-PIPE-001 Phase 3 gate: determinism.
#
# Removing synchronizations lets kernels overlap with host execution. If any
# kernel had a data race or an ordering assumption that stream semantics do not
# actually guarantee, repeated identical solves would drift. Run the same case
# several times and require bit-for-bit identical results every time.
set -u
cd /mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp

RUNS=${RUNS:-5}
fail=0

for spec in 160:4 640:3; do
  edge=${spec%%:*}
  outer=${spec##*:}
  echo "=== ${edge}^2, $RUNS identical solves ==="
  first=""
  for r in $(seq 1 "$RUNS"); do
    line=$(/tmp/paired_after "$edge" "$outer" gpu | grep '^RESULT')
    # Every numerical output, bit patterns included via %.17g in the probe.
    key=$(echo "$line" | grep -oE 'p_res=[^ ]+ cont=[^ ]+ mass=[^ ]+ p_lin_it=[0-9]+')
    it=$(echo "$line" | grep -oE 'krylov_it=[0-9]+')
    if [ -z "$first" ]; then
      first="$key $it"
      echo "  run $r: $first"
    elif [ "$key $it" != "$first" ]; then
      echo "  run $r: *** DIFFERS ***"
      echo "      got  $key $it"
      echo "      want $first"
      fail=1
    else
      echo "  run $r: identical"
    fi
  done
  echo
done

[ "$fail" -eq 0 ] && echo "DETERMINISM: PASS (bitwise identical across repeats)" \
                  || echo "DETERMINISM: FAIL"
exit "$fail"
