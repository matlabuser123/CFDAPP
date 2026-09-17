#!/usr/bin/env bash
# A2 pre-freeze dry-run of the new C13 test file against the current library (repo) and the
# pre-GRAD-002 control (nograd), compiled standalone with the build's own gtest.
# usage: dry_c13.sh <log-suffix>
set -u
R=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
P=$R/results/p12-grad-002/a2
SUF=${1:-1}
W=$HOME/g2/c13; mkdir -p $W
SRC=$R/tests/unit/discretization/test_gradient_boundary_consistency.cpp
SH=$(sha256sum $SRC | cut -c1-12)
GT="-I$R/build/release/_deps/googletest-src/googletest/include"
GTL="$R/build/release/lib/libgtest_main.a $R/build/release/lib/libgtest.a -lpthread"
GEN="-I$R/build/release/generated/include -I$R/build/release/_deps/nlohmann_json-src/include"
for tree in repo nograd; do
  case $tree in
    repo)   INC="-I$R/include"; LIB=$R/build/release/src/libcfdcore.a ;;
    nograd) INC="-I$HOME/g2/nograd/src_tree/include"; LIB=$HOME/g2/nograd/build/src/libcfdcore.a ;;
  esac
  BIN=$W/c13.$tree.$SH.$(sha256sum $LIB | cut -c1-12)
  LOG=$P/logs/dry_c13_${tree}_$SUF.log
  {
    echo "# A2 C13 dry-run tree=$tree libcfdcore.a $(sha256sum $LIB | cut -d' ' -f1) test source $(sha256sum $SRC | cut -d' ' -f1)"
    if [ ! -x $BIN ]; then
      c++ -std=c++20 -O2 -DNDEBUG -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wsign-conversion $INC $GEN $GT $SRC $LIB $GTL -o $BIN.tmp > $W/compile.$tree.txt 2>&1 && mv $BIN.tmp $BIN
      echo "# compile warnings: $(grep -c 'warning:' $W/compile.$tree.txt)"; grep -m5 'warning:\|error:' $W/compile.$tree.txt
    fi
    [ -x $BIN ] || { echo "BUILD FAILED -- fail closed"; head -40 $W/compile.$tree.txt; exit 1; }
    $BIN 2>&1
    echo "exit $?"
  } > $LOG 2>&1 &
done
wait
for tree in repo nograd; do echo "== $tree"; grep -E "^\[  (FAILED|  OK|PASSED) |tests ran|FAILED TEST|BUILD FAILED|compile warnings|error:" $P/logs/dry_c13_${tree}_$SUF.log | head -30; done
