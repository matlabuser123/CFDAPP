#!/usr/bin/env bash
# Build an investigation probe against the ISOLATED PRE-DIFF-002 baseline library instead of the
# current one, so the same probe source produces both sides of the comparison.
R=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
P=$R/results/p12-diff-002/investigation-f
BS=$HOME/invf_baseline/src; BB=$HOME/invf_baseline/build
W=$HOME/m7invf; mkdir -p $W
INC="-I$BS/include -I$BB/generated/include -I$BB/_deps/nlohmann_json-src/include"
LIB=$BB/src/libcfdcore.a
c++ -std=c++20 -O3 -DNDEBUG $INC $P/tools/$1.cpp $LIB -o $W/$1_base || exit 1
cd $BS
{
  echo "# INV-002 $1 against the PRE-DIFF-002 BASELINE library ($(sha256sum $LIB | cut -c1-16)); $(date -u +%Y-%m-%dT%H:%M:%SZ)"
  echo
  $W/$1_base
  echo "exit $?"
} > $P/logs/$2 2>&1
cat $P/logs/$2
