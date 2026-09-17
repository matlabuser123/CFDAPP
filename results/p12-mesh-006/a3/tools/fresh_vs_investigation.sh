#!/usr/bin/env bash
# P12-MESH-006 A3 (reported, not gated): the fresh acceptance CLI outputs vs the g5-investigation CLI
# outputs of the same cases (same solver sources; determinism check).
for r in x8 x16 x24 y16 z16; do
  a=$(sha256sum $HOME/m6a3/$r/results/fields.csv | cut -c1-64)
  b=$(sha256sum $HOME/m6g5/$r/results/fields.csv | cut -c1-64)
  if [ "$a" = "$b" ]; then v=IDENTICAL; else v=DIFFERENT; fi
  echo "$r fields.csv fresh $a investigation $b $v"
done
