#!/usr/bin/env bash
# P12-MESH-006 G10 (2D backward compatibility) on the FINAL sources (after A3, GUI, CLI fixtures,
# clang-format). BASE = the pre-MESH-006 working tree, $HOME/m6ref/base (built in Release, its cfdapp
# byte-identical to MESH-005's final binary, logs/00). NEW = build/release. 3D cases/fixtures (geometry
# "box") exist only in NEW -- BASE rejects them by design -- and are excluded from every comparison.
#   G10.1 bit identity: MESH-005 probe + MESH-006 probe (tools/bitprobe6.cpp) on every 2D committed case
#   G10.2 CLI: every 2D committed case and every 2D CLI fixture, BASE vs NEW -- fields.csv, solution.vtk,
#         residuals.csv byte for byte, metadata.json as parsed JSON, stdout line by line, exit codes
#   G10.4 inputs: every 2D case / fixture input file of BASE is byte-identical in NEW
R=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
BT=$HOME/m6ref/base
NEW=$R/build/release/apps/cli/cfdapp
BASE=$BT/build/apps/cli/cfdapp
L=$R/results/p12-mesh-006/a3/logs
is3d() { grep -q '"box"' "$1/geometry.json" 2>/dev/null; }

# ---- G10.1 -------------------------------------------------------------------------------------
W=$HOME/m6probe_final; rm -rf $W; mkdir -p $W
build() { c++ -std=c++20 -O3 -DNDEBUG -I$2/include -I$3/generated/include -I$3/_deps/nlohmann_json-src/include \
            $1 $3/src/libcfdcore.a -o $4; }
build $R/results/p12-mesh-005/tools/bitprobe.cpp $BT $BT/build $W/probe5_base
build $R/results/p12-mesh-005/tools/bitprobe.cpp $R $R/build/release $W/probe5_new
build $R/results/p12-mesh-006/tools/bitprobe6.cpp $BT $BT/build $W/probe6_base
build $R/results/p12-mesh-006/tools/bitprobe6.cpp $R $R/build/release $W/probe6_new
cases=""; skipped=""
for c in $R/cases/*; do
  [ -e $c/case.json ] || continue
  if is3d $c; then skipped="$skipped $(basename $c)"; else cases="$cases $c"; fi
done
for p in probe5 probe6; do
  $W/${p}_base $cases > $W/${p}_base.txt 2>&1 &
  $W/${p}_new $cases > $W/${p}_new.txt 2>&1 &
done
wait
{
  echo "# P12-MESH-006 G10.1 2D bit identity, FINAL sources; $(date -u +%Y-%m-%dT%H:%M:%SZ)"
  echo "# BASE lib $(sha256sum $BT/build/src/libcfdcore.a | cut -c1-16) ($BT, pre-MESH-006)"
  echo "# NEW  lib $(sha256sum $R/build/release/src/libcfdcore.a | cut -c1-16) (build/release, final)"
  echo "# 2D committed cases probed: $(echo $cases | wc -w); 3D cases excluded (BASE rejects box):$skipped"
  for p in probe5 probe6; do
    if cmp -s $W/${p}_base.txt $W/${p}_new.txt; then
      echo "VERDICT $p: BITWISE IDENTICAL ($(wc -l < $W/${p}_new.txt) lines)"
    else
      echo "VERDICT $p: DIFFERENT"; diff $W/${p}_base.txt $W/${p}_new.txt | head -40
    fi
  done
  echo; echo "## probe6 NEW output"; cat $W/probe6_new.txt
} > $L/13_g10_1_bit_identity_final.log 2>&1
grep -E "^VERDICT|excluded" $L/13_g10_1_bit_identity_final.log

# ---- G10.2 -------------------------------------------------------------------------------------
C=$HOME/m6compat; rm -rf $C; mkdir -p $C
runone() { # binary tag src name
  mkdir -p $C/$2; rm -rf $C/$2/$4; cp -r $3 $C/$2/$4; rm -rf $C/$2/$4/results
  (cd $HOME && $1 --case $C/$2/$4 > $C/$2/$4.stdout 2>&1; echo "exit $?" >> $C/$2/$4.stdout)
}
clean_stdout() { grep -v "$C" $1; }
compare() { # name
  local v="IDENTICAL" files=0
  for f in fields.csv solution.vtk residuals.csv; do
    if [ ! -e $C/base/$1/results/$f ] && [ ! -e $C/new/$1/results/$f ]; then continue; fi
    files=$((files+1))
    cmp -s $C/base/$1/results/$f $C/new/$1/results/$f || v="DIFFERENT($f)"
  done
  python3 - "$C/base/$1/results/metadata.json" "$C/new/$1/results/metadata.json" <<'PY' || v="$v DIFFERENT(metadata)"
import json, sys
def load(p):
    try:
        return json.load(open(p))
    except FileNotFoundError:
        return None
sys.exit(0 if load(sys.argv[1]) == load(sys.argv[2]) else 1)
PY
  a=$(clean_stdout $C/base/$1.stdout | md5sum | cut -c1-8); b=$(clean_stdout $C/new/$1.stdout | md5sum | cut -c1-8)
  [ "$a" = "$b" ] || v="$v DIFFERENT(stdout)"
  echo "base vs new  $1: $v | files compared $files | $(tail -1 $C/base/$1.stdout)/$(tail -1 $C/new/$1.stdout)"
  case "$v" in IDENTICAL*) ;; *) diff <(clean_stdout $C/base/$1.stdout) <(clean_stdout $C/new/$1.stdout) | head -10 | sed 's/^/      /' ;; esac
}
CASES=""; FIXTURES=""; SKIP=""
for c in $R/cases/*; do [ -e $c/case.json ] || continue; if is3d $c; then SKIP="$SKIP $(basename $c)"; else CASES="$CASES $(basename $c)"; fi; done
for c in $R/tests/data/cases/*; do [ -e $c/case.json ] || continue; if is3d $c; then SKIP="$SKIP data_$(basename $c)"; else FIXTURES="$FIXTURES $(basename $c)"; fi; done
for c in $CASES; do (runone $NEW new $R/cases/$c $c; runone $BASE base $R/cases/$c $c) & done
for c in $FIXTURES; do (runone $NEW new $R/tests/data/cases/$c data_$c; runone $BASE base $R/tests/data/cases/$c data_$c) & done
wait
{
  echo "# P12-MESH-006 G10.2 CLI 2D compatibility, FINAL sources; $(date -u +%Y-%m-%dT%H:%M:%SZ)"
  echo "# NEW cfdapp sha256 $(sha256sum $NEW | cut -c1-16); BASE cfdapp sha256 $(sha256sum $BASE | cut -c1-16)"
  echo "# excluded 3D (box) cases/fixtures (new in MESH-006; BASE rejects them):$SKIP"
  echo "=== committed 2D cases ($(echo $CASES | wc -w))"
  for c in $CASES; do compare $c; done
  echo "=== 2D CLI data fixtures ($(echo $FIXTURES | wc -w))"
  for c in $FIXTURES; do compare data_$c; done
  identical=$( { for c in $CASES; do compare $c; done; for c in $FIXTURES; do compare data_$c; done; } | grep -c ': IDENTICAL')
  echo "=== totals: $identical IDENTICAL of $(( $(echo $CASES | wc -w) + $(echo $FIXTURES | wc -w) ))"
} > $L/13_g10_2_cli_compat_final.log 2>&1
tail -1 $L/13_g10_2_cli_compat_final.log

# ---- G10.4 -------------------------------------------------------------------------------------
{
  echo "# P12-MESH-006 G10.4: every 2D case / CLI fixture input file of BASE vs NEW (results/ excluded)"
  n=0; d=0
  for dir in $BT/cases $BT/tests/data/cases; do
    rel=${dir#$BT/}
    for c in $dir/*/; do
      for f in $(cd $c && find . -type f -not -path './results/*' | sort); do
        n=$((n+1))
        if ! cmp -s "$c/$f" "$R/$rel/$(basename $c)/$f"; then d=$((d+1)); echo "DIFFERENT $rel/$(basename $c)/$f"; fi
      done
    done
  done
  echo "input files compared: $n; different: $d"
} > $L/13_g10_4_inputs_final.log 2>&1
tail -1 $L/13_g10_4_inputs_final.log
