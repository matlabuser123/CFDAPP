#!/usr/bin/env bash
# P12-GRAD-002 A2 -- the production-accuracy obstacle (INV-001 section 1): MESH-001's distorted
# Poiseuille and MESH-003's curved channel at the W8 test grids, with the COMMITTED case settings
# (now Rhie-Chow, DRIFT-001), solved with the current library (cur == build/release, 143a1dda) and
# with the pre-GRAD-002 gradient (nograd). The probe is DRIFT-001's drift_probe.cpp, unchanged
# (mode "gate": committed tolerances, the W8 tests' own extraction).
set -u
PREFIX=${1:-prod}
R=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
P=$R/results/p12-grad-002/a2
SRC=$R/results/p12-grad-002/drift-001/tools/drift_probe.cpp
SH=$(sha256sum $SRC | cut -c1-12)
GEN="-I$R/build/release/generated/include -I$R/build/release/_deps/nlohmann_json-src/include"
W=$HOME/g2/prod; mkdir -p $W
for tree in cur nograd; do
  LIB=$HOME/g2/$tree/build/src/libcfdcore.a
  BIN=$W/drift_probe.$tree.$SH.$(sha256sum $LIB | cut -c1-12)
  [ -x $BIN ] || c++ -std=c++20 -O3 -DNDEBUG -I$HOME/g2/$tree/src_tree/include $GEN $SRC $LIB -o $BIN || { echo "BUILD FAILED $tree"; exit 1; }
done
cd $R
run() {  # tree case n1 n2
  local tree=$1; shift
  local LIB=$HOME/g2/$tree/build/src/libcfdcore.a
  local BIN=$W/drift_probe.$tree.$SH.$(sha256sum $LIB | cut -c1-12)
  local LOG=$P/logs/${PREFIX}_${tree}_$1_$2x$3.log
  {
    echo "# A2 production accuracy tree=$tree libcfdcore.a $(sha256sum $LIB | cut -d' ' -f1) probe $SH"
    echo "# case inputs $(cat cases/poiseuille_distorted/*.json | sha256sum | cut -c1-16) / $(cat cases/curved_channel_multiblock/*.json | sha256sum | cut -c1-16)"
    $BIN "$@" base gate
    echo "exit $?"
  } > $LOG 2>&1
}
export -f run
export P W SH R PREFIX
jobs=()
for tree in cur nograd; do
  for g in "sq 64 8" "sq 96 12" "sq 144 18" "mb 8 20" "mb 12 30" "mb 18 45"; do
    jobs+=("$tree $g")
  done
done
printf '%s\n' "${jobs[@]}" | xargs -P 4 -I{} bash -c 'run {}'
grep -h -E "Converged|MaxIter" $P/logs/${PREFIX}_*.log | head -40
