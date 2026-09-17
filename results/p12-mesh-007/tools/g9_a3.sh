#!/usr/bin/env bash
# P12-MESH-007 G9 as amended by A3 (acceptance_gate_A3.md): BASE-A3 = nom7 (the current tree minus
# MESH-007), NEW = the current tree.
#   usage: g9_a3.sh <dry|fresh> [stage...]      stages: nom7 g91 g92 g93 g94 (default: all)
# dry   -> logs 30-35 (the pre-freeze dry-run, including the non-vacuity controls N1-N3 and C1)
# fresh -> logs 40-45 (after the A3 freeze; fresh builds; frozen artifacts re-verified first)
# Every stage writes its own log and prints a one-line verdict. Nothing here edits the repository
# except generated outputs, which G9.4 does not touch (its suites run in isolated copies).
# Resource policy: builds -j20, ctest -j16, per-case CLI runs -P16, one heavy step at a time.
set -u
MODE=$1; shift
STAGES=${*:-nom7 g91 g92 g93 g94}
R=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
P=$R/results/p12-mesh-007
TL=$P/tools
L=$P/logs
H=$HOME/m7g9
T=$H/nom7
N=$H/new
BASE=/root/m7ref/base
D=$R/build/release/_deps
JSON_INC="-I$D/nlohmann_json-src/include"
case $MODE in
  dry)   n=3; tag=dry_ ;;
  fresh) n=4; tag= ;;
  *) echo "usage: g9_a3.sh <dry|fresh> [stage...]"; exit 2 ;;
esac
log() { echo "$L/${n}$1_a3_${tag}$2.log"; }
mkdir -p $H
cd $R || exit 1

if [ "$MODE" = fresh ]; then
  # the frozen artifacts must be exactly what the freeze log recorded
  F=$L/39_a3_freeze.log
  [ -f $F ] || { echo "FAIL CLOSED: no freeze log $F"; exit 1; }
  bad=0
  while read -r h f; do
    [ -e "$f" ] || { echo "FAIL CLOSED: frozen artifact missing: $f"; bad=1; continue; }
    [ "$(sha256sum "$f" | cut -d' ' -f1)" = "$h" ] || { echo "FAIL CLOSED: $f changed since the freeze"; bad=1; }
  done < <(sed -n 's/^FROZEN \([0-9a-f]\{64\}\)  \(.*\)$/\1 \2/p' $F)
  [ $bad = 0 ] || exit 1
  echo "frozen artifacts re-verified: $(grep -c '^FROZEN ' $F)"
  # the library NEW is (build/release) must be the one the freeze recorded
  want=$(sed -n 's/^FROZEN-LIB new \([0-9a-f]\{64\}\)$/\1/p' $F)
  have=$(sha256sum $R/build/release/src/libcfdcore.a | cut -d' ' -f1)
  [ -n "$want" ] && [ "$want" = "$have" ] || { echo "FAIL CLOSED: build/release libcfdcore.a $have, frozen '$want'"; exit 1; }
  echo "NEW libcfdcore.a re-verified: $have"
fi
frozen_nom7_lib() { sed -n 's/^FROZEN-LIB nom7 \([0-9a-f]\{64\}\)$/\1/p' $L/39_a3_freeze.log 2>/dev/null; }

probe_build() {  # source include-root generated-include lib exe
  c++ -std=c++20 -O3 -DNDEBUG -I$2/include -I$3 $JSON_INC $1 $4 -o $5
}
cases_list() { for c in $R/cases/*; do [ -e $c/case.json ] && printf '%s ' $c; done; }
# the MESH-005 / MESH-006 probes are 2D probes; MESH-006 ran them "on every 2D committed case"
# (the first dry-run passed every case, and both probes aborted at the first 3D one -- log 31 run1)
cases_2d() {
  for c in $R/cases/*; do
    [ -e $c/case.json ] || continue
    python3 -c "
import json, os, sys
d = sys.argv[1]
m = json.load(open(os.path.join(d, json.load(open(os.path.join(d, 'case.json')))['mesh'])))
sys.exit(0 if 'nz' not in m else 1)" $c && printf '%s ' $c
  done
}

# G9.1 fail-closed checks on a probe-output directory (p7_*, p5_*, p6_* for every variant);
# sets CHECK_BAD=1 on any failure. usage: check_outputs <dir> <2D case dirs...>
check_outputs() {
  local dir=$1; shift
  local names; names=$(for c in "$@"; do basename $c; done)
  CHECK_BAD=0
  # (1) every run of every variant exits 0 (the exit status is the last line of each output file)
  for f in $dir/p*.txt; do
    [ "$(tail -1 $f)" = "exit 0" ] || { echo "PROBE RUN FAILED: $(basename $f): $(tail -3 $f | tr '\n' ' ')"; CHECK_BAD=1; }
  done
  echo "probe runs with exit 0: $([ $CHECK_BAD = 0 ] && echo all || echo NOT ALL -- the G9.1 comparison is INVALID)"
  # (2) every run of every variant is complete: every expected item is printed
  local f item
  for f in $dir/p7_*.txt; do
    for item in "geometry   C16 " "geometry   G16 " "geometry   Q16 " "geometry   MB2 " "geometry   H8 " \
                "C16 cavity all steps" "G16 channel all steps" "Q16 channel all steps" "MB2 channel all steps" \
                "C16 cavity TransientSolver" "lid cube 8^3, 20 iterations" "thermal    2D C16" "thermal    3D H8"; do
      grep -qF "$item" $f || { echo "INCOMPLETE: $(basename $f) lacks '$item'"; CHECK_BAD=1; }
    done
  done
  for f in $dir/p5_*.txt $dir/p6_*.txt; do
    for item in "cartesian 4x3 (1 x 1)" "cartesian 13x7 (2.1 x 0.9)" $names; do
      grep -qF "$item" $f || { echo "INCOMPLETE: $(basename $f) lacks '$item'"; CHECK_BAD=1; }
    done
  done
  echo "probe outputs complete (every expected mesh, flow and case printed, all variants): $([ $CHECK_BAD = 0 ] && echo yes || echo NO)"
}

for s in $STAGES; do
  case $s in
  g91selftest)
    # the fail-closed checks must reject corrupted copies of the last g91 outputs (NOT a gate item)
    LOG=$(log 1 g91_failclosed_selftest)
    S=$H/probes_$MODE
    [ -d $S ] || { echo "run g91 first"; exit 1; }
    CASES=$(cases_2d)
    {
      echo "# G9.1 fail-closed self-test (A3 dry-run support); $(date -u +%Y-%m-%dT%H:%M:%SZ); source outputs $S"
      echo "## unmodified outputs (must be accepted)"
      check_outputs $S $CASES; echo "=> CHECK_BAD=$CHECK_BAD (must be 0)"; st=$CHECK_BAD
      for m in crash-like-run1 exit-nonzero missing-p7-item missing-p6-case; do
        M=$H/selftest_$m; rm -rf $M; cp -r $S $M
        case $m in
          crash-like-run1)  # every p5/p6 variant is the identical 2-line abort of dry-run 1
            for f in $M/p5_*.txt; do printf "terminate called after throwing an instance of 'cfd::InvalidArgumentError'\n  what():  interpolateInternalFaceSkewCorrected: a 3D velocity field needs gradientZ\nexit 134\n" > $f; done
            for f in $M/p6_*.txt; do printf "terminate called after throwing an instance of 'cfd::InvalidArgumentError'\n  what():  assembleGeometricPressureCorrection: a 3D mesh needs the w response coefficient\nexit 134\n" > $f; done ;;
          exit-nonzero)     sed -i '$ s/exit 0/exit 1/' $M/p7_nograd.txt ;;
          missing-p7-item)  sed -i '/thermal    3D H8/d' $M/p7_new_again.txt ;;
          missing-p6-case)  sed -i '/poiseuille_distorted/d' $M/p6_cur.txt ;;
        esac
        echo "## mutation $m (must be rejected)"
        check_outputs $M $CASES
        echo "=> CHECK_BAD=$CHECK_BAD (must be 1)"
        [ $CHECK_BAD = 1 ] || st=1
        # in the crash case the old comparison would still have said IDENTICAL: show it
        [ $m = crash-like-run1 ] && { cmp -s $M/p5_nom7.txt $M/p5_new.txt && echo "   (plain cmp of the crashed p5 outputs: identical -- the vacuity the check now rejects)"; }
      done
      echo "G9.1 FAIL-CLOSED SELF-TEST: $([ $st = 0 ] && echo PASS || echo FAIL)"
      echo "# end $(date -u +%Y-%m-%dT%H:%M:%SZ)"
    } > $LOG 2>&1
    grep -E '^## |=> CHECK_BAD|SELF-TEST' $LOG
    ;;
  g92selftest)
    # g9_compat.sh must not call vacuous or partial agreement IDENTICAL (NOT a gate item)
    LOG=$(log 2 g92_failclosed_selftest)
    NEWBIN=$R/build/release/apps/cli/cfdapp
    FK=$H/fakes; rm -rf $FK; mkdir -p $FK
    printf '#!/usr/bin/env bash\nkill -ABRT $$\n' > $FK/crash
    printf '#!/usr/bin/env bash\n%s "$@"; rc=$?; rm -f "$2/results/solution.vtk"; exit $rc\n' "$NEWBIN" > $FK/noexport
    printf '#!/usr/bin/env bash\n%s "$@"; rc=$?\n[ -f "$2/results/metadata.json" ] && python3 -c "import json,sys; p=sys.argv[1]; d=json.load(open(p)); d[\\"selftest\\"]=1; json.dump(d, open(p, \\"w\\"), indent=2)" "$2/results/metadata.json"\nexit $rc\n' "$NEWBIN" > $FK/tamper
    chmod +x $FK/*
    SUB='^(lid_driven_cavity|data_valid_cavity|data_bad_solver|data_does_not_converge)$'
    {
      echo "# G9.2 fail-closed self-test (A3 dry-run support); $(date -u +%Y-%m-%dT%H:%M:%SZ); g9_compat.sh $(sha256sum $TL/g9_compat.sh | cut -c1-16)"
      echo "# subset $SUB"
      for f in $FK/*; do echo "# fake $(basename $f):"; sed 's/^/#   /' $f; done
      st=0
      t() { # label ref new expect-identical expect-invalid
        local out; out=$(G9_ONLY="$SUB" bash $TL/g9_compat.sh st_$1 $2 $3)
        echo "## $1"; echo "$out" | grep -E '^ref vs new|^=== totals'
        local tot; tot=$(echo "$out" | grep '^=== totals')
        local i n; i=$(echo "$tot" | sed -n 's/.*totals: \([0-9]*\) IDENTICAL.*/\1/p'); n=$(echo "$tot" | sed -n 's/.*INVALID \([0-9]*\);.*/\1/p')
        if [ "$i" = "$4" ] && [ "$n" = "$5" ]; then echo "=> as required (IDENTICAL $i, INVALID $n)"; else echo "=> NOT AS REQUIRED (IDENTICAL $i expected $4, INVALID $n expected $5)"; st=1; fi
      }
      # (a) real runs are accepted
      t real $NEWBIN $NEWBIN 4 0
      # (b) both sides crash identically (the vacuity class of G9.1 run 1): nothing may be IDENTICAL
      t crash $FK/crash $FK/crash 0 4
      # (c) both sides lose solution.vtk: the two runs that must write it are INVALID; the invalid
      #     fixture (exit 2) and the non-converging one (exit 3: metadata + residuals) stay valid
      t noexport $FK/noexport $FK/noexport 2 2
      # (d) identical files, but metadata.json differs on one side: must not count as IDENTICAL
      t tamper $NEWBIN $FK/tamper 1 0
      echo "G9.2 FAIL-CLOSED SELF-TEST: $([ $st = 0 ] && echo PASS || echo FAIL)"
      echo "# end $(date -u +%Y-%m-%dT%H:%M:%SZ)"
    } > $LOG 2>&1
    grep -E '^## |^=>|SELF-TEST' $LOG
    ;;
  nom7)
    LOG=$(log 0 build_nom7)
    rm -rf $T   # always from an empty directory: no output of an earlier run can survive into G9.4
    bash $TL/build_nom7.sh "$(basename $LOG)" 20 > /dev/null
    echo "nom7: $(grep -E '^build OK|build failed|failed' $LOG | head -1); $(grep -E '^nom7 libcfdcore' $LOG)"
    echo "  $(grep -E '^FIDELITY' $LOG)"
    grep -q '^FIDELITY PASS' $LOG || { echo "FAIL CLOSED: nom7 fidelity"; exit 1; }
    [ -f $T/build/src/libcfdcore.a ] || { echo "FAIL CLOSED: no nom7 library"; exit 1; }
    if [ "$MODE" = fresh ]; then
      have=$(sha256sum $T/build/src/libcfdcore.a | cut -d' ' -f1)
      [ "$have" = "$(frozen_nom7_lib)" ] || { echo "FAIL CLOSED: nom7 libcfdcore.a $have differs from the frozen $(frozen_nom7_lib)"; exit 1; }
      echo "  nom7 libcfdcore.a equals the frozen hash" | tee -a $LOG
    fi
    ;;
  g91)
    LOG=$(log 1 g91_bitprobes)
    W=$H/probes_$MODE; rm -rf $W; mkdir -p $W
    {
      echo "# G9.1 bit identity (A3): nom7 vs NEW; $(date -u +%Y-%m-%dT%H:%M:%SZ); git HEAD $(git rev-parse HEAD)"
      echo "# nom7 libcfdcore.a $(sha256sum $T/build/src/libcfdcore.a | cut -d' ' -f1)"
      echo "# NEW  libcfdcore.a $(sha256sum $R/build/release/src/libcfdcore.a | cut -d' ' -f1)"
      echo "# probes: $(sha256sum $TL/bitprobe7.cpp $R/results/p12-mesh-005/tools/bitprobe.cpp $R/results/p12-mesh-006/tools/bitprobe6.cpp | awk '{printf "%s %s; ", substr($1,1,16), $2}')"
      set -e
      for p in "7 $TL/bitprobe7.cpp" "5 $R/results/p12-mesh-005/tools/bitprobe.cpp" "6 $R/results/p12-mesh-006/tools/bitprobe6.cpp"; do
        set -- $p
        probe_build $2 $T/src_tree $T/build/generated/include $T/build/src/libcfdcore.a $W/p$1_nom7
        probe_build $2 $R $R/build/release/generated/include $R/build/release/src/libcfdcore.a $W/p$1_new
        # non-vacuity N1: the GRAD-002 attribution pair (cur, nograd), which differ only in Gradient.cpp
        probe_build $2 $HOME/g2/cur/src_tree $HOME/g2/cur/build/generated/include $HOME/g2/cur/build/src/libcfdcore.a $W/p$1_cur
        probe_build $2 $HOME/g2/nograd/src_tree $HOME/g2/nograd/build/generated/include $HOME/g2/nograd/build/src/libcfdcore.a $W/p$1_nograd
      done
      set +e
      CASES=$(cases_2d)
      echo "# 2D committed cases for probes 5 and 6 ($(echo $CASES | wc -w) of $(echo $(cases_list) | wc -w)): $(for c in $CASES; do basename $c; done | tr '\n' ' ')"
      # every probe run must exit 0: its exit status is the last line of its output file
      run() { local dir=$1 out=$2; shift 2; (cd $dir && "$@" > $out 2>&1; echo "exit $?" >> $out); }
      # bitprobe7 reads cases/lid_driven_cavity_3d relative to the working directory: each tree's own copy
      run $T/src_tree $W/p7_nom7.txt $W/p7_nom7
      run $R $W/p7_new.txt $W/p7_new
      run $R $W/p7_new_again.txt $W/p7_new
      run $HOME/g2/cur/src_tree $W/p7_cur.txt $W/p7_cur
      run $HOME/g2/nograd/src_tree $W/p7_nograd.txt $W/p7_nograd
      echo "# lid_driven_cavity_3d inputs: nom7 vs current $(diff -r -x results $T/src_tree/cases/lid_driven_cavity_3d $R/cases/lid_driven_cavity_3d > /dev/null && echo identical || echo DIFFERENT)"
      for p in 5 6; do
        for v in nom7 new cur nograd; do run $R $W/p${p}_$v.txt $W/p${p}_$v $CASES; done
        run $R $W/p${p}_new_again.txt $W/p${p}_new $CASES
      done
      check_outputs $W $CASES
      bad=$CHECK_BAD
      for p in 7 5 6; do
        echo "## probe $p: NEW output ($(wc -l < $W/p${p}_new.txt) lines)"
        cat $W/p${p}_new.txt
        if cmp -s $W/p${p}_nom7.txt $W/p${p}_new.txt; then echo "VERDICT G9.1 probe $p nom7 vs NEW: BITWISE IDENTICAL"
        else echo "VERDICT G9.1 probe $p nom7 vs NEW: DIFFERENT"; diff $W/p${p}_nom7.txt $W/p${p}_new.txt | head -30; fi
        cmp -s $W/p${p}_new.txt $W/p${p}_new_again.txt && echo "N2 probe $p: NEW twice identical" || echo "N2 probe $p: NEW twice DIFFERENT"
        nd=$(diff $W/p${p}_cur.txt $W/p${p}_nograd.txt | grep -c '^>')
        echo "N1 probe $p: cur vs nograd -- $nd differing lines"
        diff $W/p${p}_cur.txt $W/p${p}_nograd.txt | grep '^>' | head -40 | sed 's/^/   /'
        cmp -s $W/p${p}_cur.txt $W/p${p}_new.txt && echo "   (cur == NEW for probe $p)" || echo "   (cur != NEW for probe $p)"
      done
      echo "## N1 requirement (A3 §4): nograd differs from cur only in Gradient.cpp. Each probe must resolve that change"
      echo "## (cur vs nograd output differs); for probe 7, geometry items must be identical and 'Q16 channel' must differ"
      g=$(diff <(grep '^geometry' $W/p7_cur.txt) <(grep '^geometry' $W/p7_nograd.txt) | grep -c '^>')
      q=$(diff <(grep 'Q16 channel' $W/p7_cur.txt) <(grep 'Q16 channel' $W/p7_nograd.txt) | grep -c '^>')
      n1=1
      for p in 7 5 6; do cmp -s $W/p${p}_cur.txt $W/p${p}_nograd.txt && { echo "N1 probe $p cannot resolve the control"; n1=0; }; done
      [ $g = 0 ] && [ $q -gt 0 ] || n1=0
      echo "N1 geometry lines differing: $g (must be 0); Q16 channel lines differing: $q (must be > 0); every probe resolves cur vs nograd -> $([ $n1 = 1 ] && echo AS REQUIRED || echo NOT AS REQUIRED)"
      ok=1
      [ $bad = 0 ] || ok=0
      [ $n1 = 1 ] || ok=0
      for p in 7 5 6; do
        cmp -s $W/p${p}_nom7.txt $W/p${p}_new.txt || ok=0
        cmp -s $W/p${p}_new.txt $W/p${p}_new_again.txt || ok=0
      done
      echo "G9.1 OVERALL: $([ $ok = 1 ] && echo PASS || echo 'FAIL/INVALID') (runs exit 0 and complete; nom7 == NEW and NEW == NEW for probes 7, 5, 6; N1 as required)"
      echo "# end $(date -u +%Y-%m-%dT%H:%M:%SZ)"
    } > $LOG 2>&1
    grep -E '^VERDICT|^N1 |^N2 |FAIL|error|^G9.1 OVERALL|^probe|INCOMPLETE|^# 2D' $LOG | grep -v '^   '
    ;;
  g92)
    LOG=$(log 2 g92_cli)
    NEWBIN=$R/build/release/apps/cli/cfdapp
    {
      echo "# G9.2 CLI (A3); $(date -u +%Y-%m-%dT%H:%M:%SZ); git HEAD $(git rev-parse HEAD); g9_compat.sh $(sha256sum $TL/g9_compat.sh | cut -c1-16)"
      echo "## control C1: NEW against itself (instrument determinism, all keys of metadata.json)"
      bash $TL/g9_compat.sh c1_$MODE $NEWBIN $NEWBIN
      echo "## non-vacuity N3: nograd's cfdapp against NEW (must flag poiseuille_distorted)"
      bash $TL/g9_compat.sh n3_$MODE $HOME/g2/nograd/build/apps/cli/cfdapp $NEWBIN
      echo "## G9.2: nom7 (reference) against NEW (candidate)"
      bash $TL/g9_compat.sh g92_$MODE $T/build/apps/cli/cfdapp $NEWBIN
      echo "# end $(date -u +%Y-%m-%dT%H:%M:%SZ)"
    } > $LOG 2>&1
    grep -E '^## |^=== totals' $LOG
    grep -E 'ref vs new  poiseuille_distorted' $LOG
    ;;
  g93)
    LOG=$(log 3 g93_inputs)
    {
      echo "# G9.3 (A3); $(date -u +%Y-%m-%dT%H:%M:%SZ)"
      python3 $TL/g93_inputs.py $BASE $T/src_tree $R; echo "exit $?"
      echo "## instrument self-test (planted changes must be flagged; NOT a gate item)"
      python3 $TL/g93_inputs.py $BASE $T/src_tree $R --self-test; echo "self-test exit $?"
    } > $LOG 2>&1
    grep -E '^## |^G9.3|UNATTRIBUTED|DIFFERENT|self-test exit' $LOG
    ;;
  g94)
    LOG=$(log 4 g94_outputs)
    {
      echo "# G9.4 (A3): nom7 and NEW full suites from the same generated-output snapshot; $(date -u +%Y-%m-%dT%H:%M:%SZ)"
      git ls-files -co --exclude-standard -- 'results/validation' ':(glob)cases/*/results/**' ':(glob)tests/data/cases/*/results/**' > $H/scope_$MODE.txt
      rm -rf $H/snapshot_$MODE; mkdir -p $H/snapshot_$MODE
      while read -r f; do mkdir -p "$H/snapshot_$MODE/$(dirname "$f")"; cp -p "$f" "$H/snapshot_$MODE/$f"; done < $H/scope_$MODE.txt
      echo "# snapshot of the current tree: $(find $H/snapshot_$MODE -type f | wc -l) files"
      # NEW: an isolated copy of the current tree, built like nom7 (Release, GUI off, same dependency sources)
      rm -rf $N; mkdir -p $N
      rsync -a --delete --exclude build --exclude results --exclude .git --exclude '__pycache__' --exclude '.venv' "$R/" "$N/src_tree/"
      echo "# NEW copy vs repository, src include tests apps cases: $(cd $N/src_tree && for f in $(find src include tests apps cases -type f); do cmp -s "$f" "$R/$f" || echo "$f"; done | wc -l) differing files"
      cmake -S $N/src_tree -B $N/build -DCMAKE_BUILD_TYPE=Release -DCFDAPP_BUILD_GUI=OFF -DFETCHCONTENT_FULLY_DISCONNECTED=ON \
        -DFETCHCONTENT_SOURCE_DIR_GOOGLETEST="$D/googletest-src" -DFETCHCONTENT_SOURCE_DIR_NLOHMANN_JSON="$D/nlohmann_json-src" > $N/configure.txt 2>&1 || { echo "NEW configure failed"; exit 1; }
      nice -n 10 cmake --build $N/build -j20 > $N/build.txt 2>&1 || { echo "NEW build failed"; exit 1; }
      echo "# NEW copy build OK, warnings $(grep -c 'warning:' $N/build.txt); libcfdcore.a $(sha256sum $N/build/src/libcfdcore.a | cut -d' ' -f1) (build/release: $(sha256sum $R/build/release/src/libcfdcore.a | cut -d' ' -f1))"
      for v in nom7 new; do
        V=$([ $v = nom7 ] && echo $T || echo $N)
        rsync -a $H/snapshot_$MODE/ $V/src_tree/
        c0=$(date +%s)
        (cd $V/build && nice -n 10 ctest -j16 --timeout 7200 > $H/ctest_${v}_$MODE.txt 2>&1)
        echo "## $v suite: ctest exit $? ($(( $(date +%s) - c0 )) s): $(grep -E 'tests passed|tests failed out of' $H/ctest_${v}_$MODE.txt)"
        sed -n '/The following tests FAILED:/,/^Errors while running/p' $H/ctest_${v}_$MODE.txt
        rm -rf $H/out_${v}_$MODE; mkdir -p $H/out_${v}_$MODE
        (cd $V/src_tree && find results/validation cases/*/results tests/data/cases/*/results -type f 2>/dev/null) | sort > $H/files_${v}_$MODE.txt
        while read -r f; do mkdir -p "$H/out_${v}_$MODE/$(dirname "$f")"; cp -p "$V/src_tree/$f" "$H/out_${v}_$MODE/$f"; done < $H/files_${v}_$MODE.txt
      done
      cat $H/files_nom7_$MODE.txt $H/files_new_$MODE.txt | sort -u > $H/files_all_$MODE.txt
      echo "## classification: NEW (candidate) vs nom7 (reference), MESH-006's classifier (via GRAD-002's classify_scope.py)"
      python3 $R/results/p12-grad-002/a2/tools/classify_scope.py $H/out_nom7_$MODE $H/out_new_$MODE $H/files_all_$MODE.txt
      echo "exit $?"
      echo "# end $(date -u +%Y-%m-%dT%H:%M:%SZ)"
    } > $LOG 2>&1
    grep -E '^## |^checked|^exit|failed' $LOG
    ;;
  esac
done
