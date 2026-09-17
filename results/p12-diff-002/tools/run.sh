#!/usr/bin/env bash
# P12-DIFF-002: build and run one evidence program against the working-tree library, from the repo
# root. Usage: run.sh <program-stem> <log-name> [args...]
R=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
P=$R/results/p12-diff-002
W=$HOME/m7d2; mkdir -p $W $P/logs $P/data
INC="-I$R/include -I$R/build/release/generated/include -I$R/build/release/_deps/nlohmann_json-src/include"
LIB=$R/build/release/src/libcfdcore.a
c++ -std=c++20 -O3 -DNDEBUG $INC $P/tools/$1.cpp $LIB -o $W/$1 || exit 1
cd $R
{
  echo "# P12-DIFF-002 $1 (libcfdcore.a sha256 $(sha256sum $LIB | cut -c1-16)); $(date -u +%Y-%m-%dT%H:%M:%SZ)"
  $W/$1 "${@:3}"
  echo "exit $?"
} > $P/logs/$2 2>&1
cat $P/logs/$2
