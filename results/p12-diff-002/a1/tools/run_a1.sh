#!/usr/bin/env bash
# P12-DIFF-002 Amendment A1: build and run one A1 evidence program against the working-tree library,
# from the repo root. Usage: run_a1.sh <program-stem> <log-name> [args...]
R=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
P=$R/results/p12-diff-002/a1
W=$HOME/m7d2a1; mkdir -p $W $P/logs $P/data
INC="-I$R/include -I$R/build/release/generated/include -I$R/build/release/_deps/nlohmann_json-src/include"
LIB=$R/build/release/src/libcfdcore.a
c++ -std=c++20 -O3 -DNDEBUG $INC $P/tools/$1.cpp $LIB -o $W/$1 || exit 1
cd $R
{
  echo "# P12-DIFF-002 A1 $1 (libcfdcore.a sha256 $(sha256sum $LIB | cut -c1-16)); $(date -u +%Y-%m-%dT%H:%M:%SZ)"
  echo "# git HEAD $(git rev-parse HEAD)"
  echo "# probe sha256 $(sha256sum $P/tools/$1.cpp | cut -c1-16)"
  echo
  $W/$1 "${@:3}"
  echo "exit $?"
} > $P/logs/$2 2>&1
cat $P/logs/$2
