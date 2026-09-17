#!/usr/bin/env bash
# usage: bitprobe.sh <tree-root> <build-dir> <out>
T=$1; B=$2; OUT=$3
R=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
P=/mnt/c/Users/Hasib/AppData/Local/Temp/claude/c--Users-Hasib-Desktop-CFDAPP/a42596da-5eee-4a79-8544-4940809b2997/scratchpad/mesh005
mkdir -p $HOME/m5probe
c++ -std=c++20 -O3 -DNDEBUG -I$T/include -I$B/generated/include -I$B/_deps/nlohmann_json-src/include \
  $P/bitprobe.cpp $B/src/libcfdcore.a -o $HOME/m5probe/bitprobe_$(basename $OUT .txt) || exit 1
cases=""
for c in $R/cases/*; do [ -e $c/case.json ] && cases="$cases $c"; done
$HOME/m5probe/bitprobe_$(basename $OUT .txt) $cases > $OUT 2>&1
echo "exit $? lines $(wc -l < $OUT)"
