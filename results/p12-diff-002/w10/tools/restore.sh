#!/usr/bin/env bash
# W10 stage 6: return the tracked generated outputs to their Release-built content.
# - RUNTIME-ONLY files (timing fields only): MESH-006 classifier --restore, from the pre-W10 snapshot.
# - The 5 VALUES files: the last writer was the W10 ASan (sanitized Debug) build, whose floating-point
#   results differ from Release at ~1e-9 relative. The snapshot copies are value-identical (all
#   non-timing entries) to what the current Release library writes -- verified against the isolated
#   Release copy (results/p12-grad-002/a2, tree "cur", library 143a1dda) -- so they are restored too.
set -u
R=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
P=$R/results/p12-diff-002/w10
W=$HOME/w10
LOG=$P/logs/06_restore.log
cd $R || exit 1
{
  echo "# W10 stage 6 restore $(date -u +%Y-%m-%dT%H:%M:%SZ)"
  python3 $W/classify.py $W/snapshot --restore | sed -n '/^restored/,$p'
  for f in results/validation/mms/distorted_mesh_mms.json results/validation/mms/distorted_mesh_momentum_mms.json \
           results/validation/mms/distorted_mesh_simple_mms.json results/validation/mms/distorted_mesh_simple_mms.md \
           results/validation/production/curved_channel_multiblock_grid_convergence.json; do
    python3 - "$f" <<'PY' || { echo "REFUSED: $f snapshot is not value-identical to Release"; exit 1; }
import sys, importlib.util
f = sys.argv[1]
spec = importlib.util.spec_from_file_location("m6", "results/p12-mesh-006/a3/tools/classify_generated_outputs.py")
m6 = importlib.util.module_from_spec(spec); spec.loader.exec_module(m6)
snap = open("/root/w10/snapshot/" + f).read(); cur = open("/root/g2/cur/src_tree/" + f).read()
fn = m6.json_diffs if f.endswith(".json") else m6.md_diffs
sys.exit(1 if fn(snap, cur) else 0)
PY
    cp -p "$W/snapshot/$f" "$f" && echo "restored (Release-identical values) $f"
  done
  echo "## after restore: snapshot files differing from the working tree: $(cd $W/snapshot && find . -type f | while read -r f; do cmp -s "$f" "$R/$f" || echo "$f"; done | wc -l)"
  echo "# end $(date -u +%Y-%m-%dT%H:%M:%SZ)"
} > $LOG 2>&1
cat $LOG
