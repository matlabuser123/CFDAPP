#!/usr/bin/env bash
# P12-GRAD-002 A1: build and run one A1 evidence program, logging under a1/logs/.
# Usage: run_a1.sh <program-stem> <log-name> [base|grad001|new]
R=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
P=$R/results/p12-grad-002
W=$HOME/m7grad2; mkdir -p $W $P/a1/logs
GEN="-I$R/build/release/generated/include -I$R/build/release/_deps/nlohmann_json-src/include"
which=${3:-new}
case $which in
  base)    INC="-I$HOME/m7ref/base/include -I$HOME/m7ref/base/build/generated/include -I$HOME/m7ref/base/build/_deps/nlohmann_json-src/include"
           LIB=$HOME/m7ref/base/build/src/libcfdcore.a ;;
  grad001) INC="-I$HOME/m7ref/grad001/include $GEN"
           LIB=$HOME/m7ref/grad001/libcfdcore.a ;;
  *)       INC="-I$R/include $GEN"
           LIB=$R/build/release/src/libcfdcore.a ;;
esac
c++ -std=c++20 -O3 -DNDEBUG $INC $P/tools/$1.cpp $LIB -o $W/a1_$1_$which || exit 1
{
  echo "# P12-GRAD-002 A1 $1 ($which library, sha256 $(sha256sum $LIB | cut -c1-16)); $(date -u +%Y-%m-%dT%H:%M:%SZ)"
  $W/a1_$1_$which
  echo "exit $?"
} > $P/a1/logs/$2 2>&1
cat $P/a1/logs/$2
