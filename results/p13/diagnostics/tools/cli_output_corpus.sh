#!/usr/bin/env bash
# P13-005 -- the CLI's stdout is its REPORT. This captures it, per case, so a
# change to diagnostics can be shown not to have touched it.
#
# stdout and stderr are captured SEPARATELY and on purpose: the whole point of
# this item is that results and diagnostics stop sharing a channel. A corpus
# that merged them could not tell the difference.
set -uo pipefail
ROOT=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
OUT=${1:?usage: cli_output_corpus.sh <output-dir> [extra cfdapp args...]}
shift || true
B=$ROOT/build/final/apps/cli/cfdapp
cd "$ROOT"
mkdir -p "$OUT"

CASES=(valid_cavity valid_duct3d_cli_smoke does_not_converge bad_physics missing_mesh
       mesh_quality_warning_cli malformed_json)
for c in "${CASES[@]}"; do
  [ -d "tests/data/cases/$c" ] || continue
  W=/tmp/p13_corpus_$c; rm -rf $W; cp -r "tests/data/cases/$c" $W; rm -rf $W/results
  # A short budget where the case is valid, so the corpus is fast and stable.
  python3 - $W/solver.json <<'PY' 2>/dev/null || true
import json, sys
p = sys.argv[1]
d = json.load(open(p)); d["max_iterations"] = 8
json.dump(d, open(p, "w"), indent=2)
PY
  "$B" --case $W "$@" > "$OUT/$c.stdout" 2> "$OUT/$c.stderr"
  echo "exit=$?" >> "$OUT/$c.stdout"
  # The temp path varies per run; normalise it so the corpus is comparable.
  sed -i "s|$W|<case>|g" "$OUT/$c.stdout" "$OUT/$c.stderr"
done
echo "captured $(ls "$OUT"/*.stdout 2>/dev/null | wc -l) cases into $OUT"
wc -c "$OUT"/*.stdout "$OUT"/*.stderr 2>/dev/null | tail -1
