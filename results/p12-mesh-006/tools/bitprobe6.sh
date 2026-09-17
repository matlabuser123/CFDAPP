#!/usr/bin/env bash
# P12-MESH-006 gate G10.1: 2D bit identity, BASE (pre-MESH-006 tree, $HOME/m6ref/base) vs NEW (build/release).
# Runs the MESH-005 probe (geometry, scalar operators) and the MESH-006 probe (momentum, pressure correction,
# SIMPLE solves) built against each tree, on two programmatic meshes and every committed case.
# usage: bitprobe6.sh <log>
R=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
BT=$HOME/m6ref/base
LOG=${1:-/dev/stdout}
W=$HOME/m6probe; mkdir -p $W
build() { # source tree builddir exe
  c++ -std=c++20 -O3 -DNDEBUG -I$2/include -I$3/generated/include -I$3/_deps/nlohmann_json-src/include \
    $1 $3/src/libcfdcore.a -o $4
}
set -e
build $R/results/p12-mesh-005/tools/bitprobe.cpp $BT $BT/build $W/probe5_base
build $R/results/p12-mesh-005/tools/bitprobe.cpp $R $R/build/release $W/probe5_new
build $R/results/p12-mesh-006/tools/bitprobe6.cpp $BT $BT/build $W/probe6_base
build $R/results/p12-mesh-006/tools/bitprobe6.cpp $R $R/build/release $W/probe6_new
set +e
cases=""
for c in $R/cases/*; do [ -e $c/case.json ] && cases="$cases $c"; done
for p in probe5 probe6; do
  $W/${p}_base $cases > $W/${p}_base.txt 2>&1 &
  $W/${p}_new $cases > $W/${p}_new.txt 2>&1 &
done
wait
{
  echo "# P12-MESH-006 G10.1 2D bit identity; $(date -u)"
  echo "# BASE lib $(sha256sum $BT/build/src/libcfdcore.a | cut -c1-16) ($BT, pre-MESH-006)"
  echo "# NEW  lib $(sha256sum $R/build/release/src/libcfdcore.a | cut -c1-16) (build/release)"
  echo "# probe5 = results/p12-mesh-005/tools/bitprobe.cpp (geometry + scalar operators)"
  echo "# probe6 = results/p12-mesh-006/tools/bitprobe6.cpp (momentum, pressure correction, SIMPLE solves)"
  for p in probe5 probe6; do
    echo "## $p NEW output"; cat $W/${p}_new.txt; echo
    if cmp -s $W/${p}_base.txt $W/${p}_new.txt; then
      echo "VERDICT $p: BITWISE IDENTICAL ($(wc -l < $W/${p}_new.txt) lines)"
    else
      echo "VERDICT $p: DIFFERENT"; diff $W/${p}_base.txt $W/${p}_new.txt | head -40
    fi
    echo
  done
} > $LOG 2>&1
grep -E "^VERDICT" $LOG
