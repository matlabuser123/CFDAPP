#!/usr/bin/env bash
# P12-GRAD-001: build and run one evidence program against a chosen library.
# Usage: build_and_run.sh <program-stem> <log-name> [base|new]
R=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
P=$R/results/p12-grad-001
W=$HOME/m7grad; mkdir -p $W $P/logs
which=${3:-new}
if [ "$which" = base ]; then
  INC="-I$HOME/m7ref/base/include -I$HOME/m7ref/base/build/generated/include -I$HOME/m7ref/base/build/_deps/nlohmann_json-src/include"
  LIB=$HOME/m7ref/base/build/src/libcfdcore.a
else
  INC="-I$R/include -I$R/build/release/generated/include -I$R/build/release/_deps/nlohmann_json-src/include"
  LIB=$R/build/release/src/libcfdcore.a
fi
c++ -std=c++20 -O3 -DNDEBUG $INC $P/tools/$1.cpp $LIB -o $W/$1_$which || exit 1
{
  echo "# P12-GRAD-001 $1 ($which library, sha256 $(sha256sum $LIB | cut -c1-16)); $(date -u +%Y-%m-%dT%H:%M:%SZ)"
  $W/$1_$which
  echo "exit $?"
} > $P/logs/$2 2>&1
cat $P/logs/$2
