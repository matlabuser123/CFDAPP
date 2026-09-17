#!/usr/bin/env bash
# LOWMACH-001: build lowmach_probe against the current and the pre-DIFF-002 library and run it.
set -u
R=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
P=$R/results/p12-diff-002/lowmach-001
W=$HOME/lowmach001; mkdir -p "$W" "$P/logs"
J="-I$R/build/release/_deps/nlohmann_json-src/include"
c++ -std=c++20 -O3 -DNDEBUG -I$R/include -I$R/build/release/generated/include $J "$P/tools/lowmach_probe.cpp" "$R/build/release/src/libcfdcore.a" -o "$W/probe_current" || exit 1
c++ -std=c++20 -O3 -DNDEBUG -I/root/uf001_baseline/include -I/root/uf001_baseline/build/release/generated/include $J "$P/tools/lowmach_probe.cpp" /root/uf001_baseline/build/release/src/libcfdcore.a -o "$W/probe_base" || exit 1
for v in current base; do
  lib=$R/build/release/src/libcfdcore.a; [ $v = base ] && lib=/root/uf001_baseline/build/release/src/libcfdcore.a
  { echo "# LOWMACH-001 probe=$v libcfdcore $(sha256sum "$lib" | cut -d' ' -f1) source $(sha256sum "$P/tools/lowmach_probe.cpp" | cut -c1-16) $(date -u +%FT%TZ)"
    "$W/probe_$v"; echo "exit $?"; } > "$P/logs/02_probe_bound_$v.log" 2>&1 &
done
wait
cat "$P/logs/02_probe_bound_current.log" "$P/logs/02_probe_bound_base.log"
