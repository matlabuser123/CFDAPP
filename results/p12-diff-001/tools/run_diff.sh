#!/usr/bin/env bash
# P12-DIFF-001: build and run one evidence program against a chosen library, from the repo root.
# Usage: run_diff.sh <program-stem> <log-name> [base|grad001|new] [args...]
R=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
P=$R/results/p12-diff-001
W=$HOME/m7diff; mkdir -p $W $P/logs $P/data
GEN="-I$R/build/release/generated/include -I$R/build/release/_deps/nlohmann_json-src/include"
which=${3:-new}
case $which in
  base)    INC="-I$HOME/m7ref/base/include -I$HOME/m7ref/base/build/generated/include -I$HOME/m7ref/base/build/_deps/nlohmann_json-src/include"
           LIB=$HOME/m7ref/base/build/src/libcfdcore.a ;;
  grad002) INC="-I$HOME/m7ref/grad002/include $GEN"
           LIB=$HOME/m7ref/grad002/libcfdcore.a ;;
  *)       INC="-I$R/include $GEN"
           LIB=$R/build/release/src/libcfdcore.a ;;
esac
c++ -std=c++20 -O3 -DNDEBUG $INC $P/tools/$1.cpp $LIB -o $W/$1_$which || exit 1
cd $R
{
  echo "# P12-DIFF-001 $1 ($which library, sha256 $(sha256sum $LIB | cut -c1-16)); $(date -u +%Y-%m-%dT%H:%M:%SZ)"
  $W/$1_$which "${@:4}"
  echo "exit $?"
} > $P/logs/$2 2>&1
cat $P/logs/$2
