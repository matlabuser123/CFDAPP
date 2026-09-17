#!/usr/bin/env bash
# P12-GRAD-002, C14 re-verification of the criteria A2 carried over "in their A1 form"
# (C1-A1, C3(b), C4a/C4b, C5, C6-A1, C7, C9 translation) on the FINAL library.
#
# Why: A1 measured them on libcfdcore.a 51e82ee8..., before P12-DIFF-002 changed the library
# (143a1dda...). C14: "No source change after that hash without rebuilding and rerunning the
# affected evidence." Gradient.cpp is unchanged since A1 (d24882a9...), but C7 runs static PISO,
# whose wall flux DIFF-002 changed. No criterion, threshold or instrument changes here.
#
# Instrument identity: a1_envelope.cpp, diagnostics.cpp and run_a1.sh are hashed in A1's freeze log.
# a1_cavity.cpp, a1_convergence.cpp and a1_3d.cpp were not; each is rerun on the SAME preserved
# reference library as its A1 control log and must reproduce that log exactly (header excluded):
#   diagnostics    base    -> a1/logs/04_c5_fresh_base.log
#   a1_envelope    base    -> a1/logs/00_dryrun_baseline.log
#   a1_cavity      base    -> a1/logs/06_c7_cavity_base.log
#   a1_convergence base    -> a1/logs/08_convergence_base.log
#   a1_3d          grad001 -> a1/logs/13_3d_grad001.log
# Then each program runs on the final library (logs a3/logs/c14_<prog>_final.log) and is compared
# line by line with A1's run on 51e82ee8 (02, 03, 05, 07, 12).
set -u
R=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
P=$R/results/p12-grad-002
L=$P/a3/logs
W=$HOME/g2/c14; rm -rf $W; mkdir -p $W
GEN="-I$R/build/release/generated/include -I$R/build/release/_deps/nlohmann_json-src/include"
LOG=$L/c14_00_reverify.log
[ -e "$LOG" ] && { echo "REFUSED: $LOG exists"; exit 1; }
build_run() {  # prog which outlog
  local prog=$1 which=$2 out=$3 INC LIB
  case $which in
    base)    INC="-I$HOME/m7ref/base/include -I$HOME/m7ref/base/build/generated/include -I$HOME/m7ref/base/build/_deps/nlohmann_json-src/include"
             LIB=$HOME/m7ref/base/build/src/libcfdcore.a ;;
    grad001) INC="-I$HOME/m7ref/grad001/include $GEN"; LIB=$HOME/m7ref/grad001/libcfdcore.a ;;
    final)   INC="-I$R/include $GEN"; LIB=$R/build/release/src/libcfdcore.a ;;
  esac
  c++ -std=c++20 -O3 -DNDEBUG $INC $P/tools/$prog.cpp $LIB -o $W/${prog}_$which > $W/${prog}_$which.compile.txt 2>&1 \
    || { echo "  COMPILE FAILED $prog ($which)"; cat $W/${prog}_$which.compile.txt | head -10; return 1; }
  {
    echo "# P12-GRAD-002 C14 re-verification: $prog ($which library, sha256 $(sha256sum $LIB | cut -c1-16)); $(date -u +%Y-%m-%dT%H:%M:%SZ)"
    $W/${prog}_$which
    echo "exit $?"
  } > $out 2>&1
}
same_body() {  # a b -> 0 if identical after the first line
  cmp -s <(tail -n +2 "$1") <(tail -n +2 "$2")
}
{
  echo "# P12-GRAD-002 C14 re-verification of the A1-form criteria; $(date -u +%Y-%m-%dT%H:%M:%SZ); git HEAD $(cd $R && git rev-parse HEAD)"
  echo "# final library $(sha256sum $R/build/release/src/libcfdcore.a | cut -d' ' -f1)"
  echo "# Gradient.cpp $(sha256sum $R/src/discretization/Gradient.cpp | cut -d' ' -f1) (A1 freeze: d24882a9...)"
  echo "## instrument hashes"
  (cd $P && sha256sum tools/a1_envelope.cpp tools/diagnostics.cpp tools/run_a1.sh tools/a1_cavity.cpp tools/a1_convergence.cpp tools/a1_3d.cpp)
  echo "  A1 freeze: a1_envelope 1bb88bb3..., diagnostics 85927e9c..., run_a1.sh 344405ee..."
  echo "## instrument identity: rerun on the A1 control library must reproduce the A1 control log"
  ok=1
  for spec in "diagnostics base 04_c5_fresh_base.log" "a1_envelope base 00_dryrun_baseline.log" \
              "a1_cavity base 06_c7_cavity_base.log" "a1_convergence base 08_convergence_base.log" \
              "a1_3d grad001 13_3d_grad001.log"; do
    set -- $spec
    build_run $1 $2 $L/c14_$1_$2.log || { ok=0; continue; }
    if same_body $L/c14_$1_$2.log $P/a1/logs/$3; then echo "  IDENTICAL  $1 on $2 == a1/logs/$3"
    else ok=0; echo "  DIFFERENT  $1 on $2 vs a1/logs/$3:"; diff <(tail -n +2 $P/a1/logs/$3) <(tail -n +2 $L/c14_$1_$2.log) | head -6; fi
  done
  echo "instrument identity: $([ $ok = 1 ] && echo REPRODUCED || echo NOT REPRODUCED)"
  echo "## final library vs A1's run on 51e82ee8"
  for spec in "a1_envelope 02_a1_fresh_new.log" "diagnostics 03_c5_fresh_new.log" "a1_cavity 05_c7_cavity_new.log" \
              "a1_convergence 07_convergence_new.log" "a1_3d 12_3d_new.log"; do
    set -- $spec
    build_run $1 final $L/c14_$1_final.log || continue
    n=$(diff <(tail -n +2 $P/a1/logs/$2) <(tail -n +2 $L/c14_$1_final.log) | grep -c '^>')
    echo "  $1: $(grep -c 'PASS' $L/c14_$1_final.log) PASS / $(grep -c 'FAIL' $L/c14_$1_final.log) FAIL labels (A1: $(grep -c 'PASS' $P/a1/logs/$2) / $(grep -c 'FAIL' $P/a1/logs/$2)); lines differing from A1: $n; $(tail -1 $L/c14_$1_final.log)"
    diff <(tail -n +2 $P/a1/logs/$2) <(tail -n +2 $L/c14_$1_final.log) | head -12 | sed 's/^/      /'
  done
  echo "# end $(date -u +%Y-%m-%dT%H:%M:%SZ)"
} > $LOG 2>&1
cat $LOG
