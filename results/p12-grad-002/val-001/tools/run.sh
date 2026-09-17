#!/usr/bin/env bash
# VAL-001: build and run one investigation program. Usage: run.sh <stem> <log-name> [args...]
R=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
P=$R/results/p12-grad-002/val-001
W=$HOME/val001; mkdir -p "$W" "$P/logs" "$P/data"
LIB=${LIB:-$R/build/release/src/libcfdcore.a}
INC="-I$R/include -I$R/build/release/generated/include -I$R/build/release/_deps/nlohmann_json-src/include -I$R/tests/unit/discretization -I$R/tests"
BIN=$W/$1.${2%.log}
c++ -std=c++20 -O3 -DNDEBUG $INC "$P/tools/$1.cpp" "$LIB" -o "$BIN" || exit 1
cd "$R" || exit 1
{
  echo "# VAL-001 $1"
  echo "# libcfdcore.a $(sha256sum "$LIB" | cut -d' ' -f1)"
  echo "# $(date -u +%Y-%m-%dT%H:%M:%SZ)"
  "$BIN" "${@:3}"
  echo "exit $?"
} > "$P/logs/$2" 2>&1
cat "$P/logs/$2"
