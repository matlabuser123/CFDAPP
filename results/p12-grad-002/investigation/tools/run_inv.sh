#!/usr/bin/env bash
# P12-GRAD-002-INV-001: build and run one investigation program against a chosen library, from the
# repository root (the production case paths are relative to it).
# Usage: run_inv.sh <program-stem> <log-name> [base|grad001|new] [args...]
R=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
P=$R/results/p12-grad-002/investigation
W=$HOME/m7inv; mkdir -p $W $P/logs $P/data
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
c++ -std=c++20 -O3 -DNDEBUG $INC $P/tools/$1.cpp $LIB -o $W/$1_$which || exit 1
cd $R
{
  echo "# INV-001 $1 ($which library, sha256 $(sha256sum $LIB | cut -c1-16)); $(date -u +%Y-%m-%dT%H:%M:%SZ)"
  $W/$1_$which "${@:4}"
  echo "exit $?"
} > $P/logs/$2 2>&1
cat $P/logs/$2
