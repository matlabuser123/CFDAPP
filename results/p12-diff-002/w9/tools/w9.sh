#!/usr/bin/env bash
# P12-DIFF-002 W9 -- committed-case backward compatibility, every committed case, three runs each:
#   A  pre-DIFF-002 library (UF-001's isolated baseline, libcfdcore 719d0fc7...) + ORIGINAL cases
#   C  current library                                                           + ORIGINAL cases
#   B  current library                                                           + CURRENT cases
# ORIGINAL cases = the committed cases with the W8B/DRIFT-001 edits reverse-applied (the two
# solver.json lines and the two case.json sentences; the result is hash-checked against the W8B
# freeze). A -> C isolates DIFF-002; C -> B isolates the DRIFT-001 case fix. Each run executes on
# a scratch copy (ProjectRunner exports into the case directory).
set -u
R=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
P=$R/results/p12-diff-002/w9
TAG=${TAG:-}          # log-name prefix (the fresh W9A rerun uses TAG=05_ so the original W9 logs stay intact)
W=$HOME/w9${TAG:+_$TAG}
rm -rf "$W"; mkdir -p "$W/bin" "$P/logs"
LOG=$P/logs/${TAG}01_w9_runs.log
cd "$R" || exit 1
INC_CUR="-I$R/include -I$R/build/release/generated/include -I$R/build/release/_deps/nlohmann_json-src/include"
INC_BASE="-I/root/uf001_baseline/include -I/root/uf001_baseline/build/release/generated/include -I$R/build/release/_deps/nlohmann_json-src/include"
c++ -std=c++20 -O3 -DNDEBUG $INC_CUR "$P/tools/w9_run.cpp" "$R/build/release/src/libcfdcore.a" -o "$W/bin/w9_current" || exit 1
c++ -std=c++20 -O3 -DNDEBUG $INC_BASE "$P/tools/w9_run.cpp" /root/uf001_baseline/build/release/src/libcfdcore.a -o "$W/bin/w9_base" || exit 1

# ORIGINAL cases: copy + reverse-apply + verify.
mkdir -p "$W/cases_original" "$W/cases_current"
rsync -a --exclude results "$R/cases/" "$W/cases_current/"
rsync -a --exclude results "$R/cases/" "$W/cases_original/"
for c in poiseuille_distorted curved_channel_multiblock; do
  grep -v '^  "face_flux": "rhie_chow",$' "$R/cases/$c/solver.json" > "$W/cases_original/$c/solver.json"
done
sed -i 's| The Rhie-Chow face flux (face_flux=rhie_chow) is selected because the 2D linear flux leaves an undamped odd-even pressure mode on this open domain, so its solutions keep moving long after the outer tolerance is met -- see results/p12-grad-002/drift-001/summary.md.",$|",|' "$W/cases_original/poiseuille_distorted/case.json"
sed -i 's| The Rhie-Chow face flux (face_flux=rhie_chow) is selected because the 2D linear flux leaves an undamped odd-even pressure mode on this open domain, which contaminates the pressure gradient long after the outer tolerance is met -- see results/p12-grad-002/drift-001/summary.md.",$|",|' "$W/cases_original/curved_channel_multiblock/case.json"
{
  echo "# P12-DIFF-002 W9 runs; $(date -u +%Y-%m-%dT%H:%M:%SZ); git HEAD $(git rev-parse HEAD)"
  echo "# current libcfdcore.a  $(sha256sum "$R/build/release/src/libcfdcore.a" | cut -d' ' -f1)"
  echo "# baseline libcfdcore.a $(sha256sum /root/uf001_baseline/build/release/src/libcfdcore.a | cut -d' ' -f1)"
  echo "# original-case reconstruction (must equal the W8B freeze's pre-edit hashes):"
  sha256sum "$W/cases_original/poiseuille_distorted/solver.json" "$W/cases_original/curved_channel_multiblock/solver.json" \
            "$W/cases_original/poiseuille_distorted/case.json" "$W/cases_original/curved_channel_multiblock/case.json" | sed 's/^/#   /'
} > "$LOG"

CASES=$(cd "$R/cases" && for d in */; do [ -f "$d/case.json" ] && echo "${d%/}"; done)
run_one() {  # config case
  local cfg=$1 c=$2 bin src
  case "$cfg" in
    A) bin=$W/bin/w9_base;    src=$W/cases_original ;;
    C) bin=$W/bin/w9_current; src=$W/cases_original ;;
    B) bin=$W/bin/w9_current; src=$W/cases_current ;;
  esac
  local d=$W/run_$cfg/$c
  mkdir -p "$W/run_$cfg"; rm -rf "$d"; cp -r "$src/$c" "$d"
  local t0; t0=$(date +%s.%N)
  "$bin" "$d" > "$d.out" 2>&1
  echo "exit $?" >> "$d.out"
  echo "wall_seconds $(echo "$(date +%s.%N) - $t0" | bc)" >> "$d.out"
}
pids=()
for cfg in A C B; do
  for c in $CASES; do run_one "$cfg" "$c" & pids+=($!); done
done
for p in "${pids[@]}"; do wait "$p"; done
{
  for cfg in A C B; do for c in $CASES; do echo "## $cfg $c"; sed 's/^/  /' "$W/run_$cfg/$c.out"; done; done
  echo "# end $(date -u +%Y-%m-%dT%H:%M:%SZ)"
} >> "$LOG"
python3 "$P/tools/w9_compare.py" "$W" $CASES > "$P/logs/${TAG}02_w9_compare.log" 2>&1
echo "compare exit $?" >> "$P/logs/${TAG}02_w9_compare.log"
cat "$P/logs/${TAG}02_w9_compare.log"
