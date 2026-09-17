#!/usr/bin/env bash
# P12-DIFF-002-INV-001: build and run one investigation probe.
# Usage: run_diff2.sh <program-stem> <log-name> [standalone|new] [args...]
R=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
P=$R/results/p12-diff-002/investigation
W=$HOME/m7diff2; mkdir -p $W $P/logs $P/data
mode=${3:-new}
if [ "$mode" = standalone ]; then
  c++ -std=c++20 -O2 $P/tools/$1.cpp -o $W/$1 || exit 1
  LIBNOTE="standalone (no CFDApp library)"
else
  INC="-I$R/include -I$R/build/release/generated/include -I$R/build/release/_deps/nlohmann_json-src/include"
  LIB=$R/build/release/src/libcfdcore.a
  c++ -std=c++20 -O3 -DNDEBUG $INC $P/tools/$1.cpp $LIB -o $W/$1 || exit 1
  LIBNOTE="libcfdcore.a sha256 $(sha256sum $LIB | cut -c1-16)"
fi
cd $R
{
  echo "# P12-DIFF-002-INV-001 $1 ($LIBNOTE); $(date -u +%Y-%m-%dT%H:%M:%SZ)"
  $W/$1 "${@:4}"
  echo "exit $?"
} > $P/logs/$2 2>&1
cat $P/logs/$2
