#!/usr/bin/env bash
# P12-MESH-005 gates G3/G4 repeated on the FINAL binaries (after the post-verification comment edit of
# src/discretization/Gradient.cpp, logs/05). Same tools as logs/02 and logs/12:
#   G4 bit identity: tools/bitprobe.cpp compiled against BASE ($HOME/m5ref/base) and NEW (build/release) -> logs/20
#   G3 CLI:          tools/compat.sh (every committed case and CLI fixture, BASE vs NEW)             -> logs/21
R=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
T=$R/results/p12-mesh-005/tools
L=$R/results/p12-mesh-005/logs
BT=$HOME/m5ref/base
mkdir -p $HOME/m5probe
probe() { # tree builddir exe
  c++ -std=c++20 -O3 -DNDEBUG -I$1/include -I$2/generated/include -I$2/_deps/nlohmann_json-src/include \
    $T/bitprobe.cpp $2/src/libcfdcore.a -o $3
}
probe $BT $BT/build $HOME/m5probe/bitprobe_base_final || exit 1
probe $R $R/build/release $HOME/m5probe/bitprobe_new_final || exit 1
cases=""
for c in $R/cases/*; do [ -e $c/case.json ] && cases="$cases $c"; done
$HOME/m5probe/bitprobe_base_final $cases > $HOME/m5probe/base_final.txt 2>&1
$HOME/m5probe/bitprobe_new_final $cases > $HOME/m5probe/new_final.txt 2>&1
{
  echo "# P12-MESH-005 gate G4 bit identity, repeated on the FINAL library (logs/02 method; probe tools/bitprobe.cpp)"
  echo "# BASE lib $(sha256sum $BT/build/src/libcfdcore.a | cut -c1-16) (pre-MESH-005 snapshot)"
  echo "# NEW  lib $(sha256sum $R/build/release/src/libcfdcore.a | cut -c1-16) (final build/release); $(date -u)"
  echo "## NEW"; cat $HOME/m5probe/new_final.txt
  echo
  if cmp -s $HOME/m5probe/base_final.txt $HOME/m5probe/new_final.txt; then
    echo "VERDICT: BITWISE IDENTICAL ($(grep -c fingerprint $HOME/m5probe/new_final.txt) meshes; BASE output byte-identical to NEW)"
  else
    echo "VERDICT: DIFFERENT"; diff $HOME/m5probe/base_final.txt $HOME/m5probe/new_final.txt
  fi
  if cmp -s <(sed -n '/^## NEW/,/^VERDICT/p' $L/02_2d_bit_identity_probe.log | sed '1d;$d' | sed '/^$/d') <(sed '/^$/d' $HOME/m5probe/new_final.txt); then
    echo "and identical to the NEW output recorded in logs/02 (the pre-comment-edit library)"
  else
    echo "NOTE: differs from the NEW output recorded in logs/02"
  fi
} > $L/20_2d_bit_identity_probe_final.log 2>&1
tail -2 $L/20_2d_bit_identity_probe_final.log
{
  echo "# P12-MESH-005 gate G3, repeated on the FINAL CLI (logs/12 method; tools/compat.sh); $(date -u)"
  bash $T/compat.sh
} > $L/21_backward_compat_cli_final.log 2>&1
tail -1 $L/21_backward_compat_cli_final.log
