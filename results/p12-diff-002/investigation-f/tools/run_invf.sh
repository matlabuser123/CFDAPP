#!/usr/bin/env bash
# P12-DIFF-002-INV-002: build and run one investigation probe against the working-tree library.
# Usage: run_invf.sh <program-stem> <log-name> [extra-cxx-flags...]
R=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
P=$R/results/p12-diff-002/investigation-f
W=$HOME/m7invf; mkdir -p $W $P/logs $P/data
INC="-I$R/include -I$R/build/release/generated/include -I$R/build/release/_deps/nlohmann_json-src/include"
LIB=$R/build/release/src/libcfdcore.a
c++ -std=c++20 -O3 -DNDEBUG $INC "${@:3}" $P/tools/$1.cpp $LIB -o $W/$1 || exit 1
cd $R
{
  echo "# INV-002 $1 (libcfdcore.a sha256 $(sha256sum $LIB | cut -c1-16)); $(date -u +%Y-%m-%dT%H:%M:%SZ)"
  echo "# git HEAD $(git rev-parse HEAD)  probe $(sha256sum $P/tools/$1.cpp | cut -c1-16)"
  echo "# extra flags: ${*:3}"
  echo
  $W/$1
  echo "exit $?"
} > $P/logs/$2 2>&1
cat $P/logs/$2
