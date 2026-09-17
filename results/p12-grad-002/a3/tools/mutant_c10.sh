#!/usr/bin/env bash
# A3 C10-A3(e): operator-mutant control for C10-A2(b). The mutant is the nograd tree (pre-GRAD-002
# gradient) with ONE change: kGreenGaussSkewCorrectionSweeps 4 -> 3 (interior skew correction
# truncated one sweep earlier). Its interior gradients differ from any 4-sweep operator for a reason
# that has nothing to do with the boundary coupling, so C10-A2(b) (repo vs mutant) must report
# deep-cell differences and bound violations on the skewed meshes.
# usage: mutant_c10.sh <log-prefix>
set -u
R=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
P=$R/results/p12-grad-002/a3
M=$HOME/g2/mutc10
PREFIX=${1:-dry}
LOG=$P/logs/${PREFIX}_mutant_c10.log
{
  echo "# A3 operator mutant $(date -u +%Y-%m-%dT%H:%M:%SZ)"
  mkdir -p $M
  rsync -a --delete --exclude build $HOME/g2/nograd/src_tree/ $M/src_tree/
  sed -i 's/^inline constexpr Index kGreenGaussSkewCorrectionSweeps = 4;/inline constexpr Index kGreenGaussSkewCorrectionSweeps = 3;  \/\/ A3 MUTANT/' $M/src_tree/include/cfd/discretization/Gradient.hpp
  grep -n "A3 MUTANT" $M/src_tree/include/cfd/discretization/Gradient.hpp || { echo "mutation not applied"; exit 1; }
  echo "mutant differs from nograd in: $(cd $M/src_tree && for f in $(find src include -type f); do cmp -s $f $HOME/g2/nograd/src_tree/$f || echo $f; done)"
  D=$R/build/release/_deps
  [ -f $M/build/CMakeCache.txt ] || cmake -S $M/src_tree -B $M/build -DCMAKE_BUILD_TYPE=Release -DCFDAPP_BUILD_GUI=OFF \
    -DFETCHCONTENT_FULLY_DISCONNECTED=ON -DFETCHCONTENT_SOURCE_DIR_GOOGLETEST=$D/googletest-src \
    -DFETCHCONTENT_SOURCE_DIR_NLOHMANN_JSON=$D/nlohmann_json-src > $M/configure.txt 2>&1 || { echo configure failed; exit 1; }
  cmake --build $M/build --target cfdcore -j16 > $M/build.txt 2>&1 || { tail -20 $M/build.txt; echo build failed; exit 1; }
  LIB=$M/build/src/libcfdcore.a
  echo "mutant libcfdcore.a $(sha256sum $LIB | cut -d' ' -f1)"
  GEN="-I$R/build/release/generated/include -I$R/build/release/_deps/nlohmann_json-src/include"
  c++ -std=c++20 -O2 -DNDEBUG -I$M/src_tree/include -I$M/build/generated/include $GEN -I$R/tests/unit/discretization \
    $R/results/p12-grad-002/a2/tools/a2_c10.cpp $LIB -o $M/a2_c10.mutant || { echo compile failed; exit 1; }
  $M/a2_c10.mutant > $M/dump_mutant.txt
  echo "mutant dump lines $(wc -l < $M/dump_mutant.txt)"
  REPO_DUMP=$2
  echo "## C10-A2(b) evaluation, NEW = repo ($REPO_DUMP), OLD = mutant"
  python3 $R/results/p12-grad-002/a2/tools/compare_c10.py $REPO_DUMP $M/dump_mutant.txt | grep -E "^skewed|^translated" | \
    sed -E 's/^(.{48}).*depth>5: ([0-9]+)\/([0-9]+) differ \| coupling bound: worst ([0-9.]+) violations ([0-9]+).*/\1 deep \2\/\3 worst \4 violations \5/'
} > $LOG 2>&1
cat $LOG
