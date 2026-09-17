#!/usr/bin/env bash
# P12-MESH-007 G9.2 (A3): CLI backward compatibility, the procedure of
# results/p12-mesh-005/tools/compat.sh with the two binaries as arguments:
#   g9_compat.sh <label> <reference cfdapp> <candidate cfdapp>
# Every committed case with a case.json and every CLI fixture is run with both binaries. The
# exported files (fields.csv, solution.vtk, residuals.csv) are compared byte for byte, metadata.json
# as parsed JSON (all keys), stdout line by line (lines naming the per-run directory removed), and
# the exit codes.
# The same script runs the controls: C1 = the same binary twice, N3 = a binary with known different
# numerics.
#
# Fail closed (added in the A3 dry-run, before the freeze; the first dry-run is log 32 run1): two
# runs that agree only because neither ran is INVALID, never IDENTICAL. Each side must show the
# expected outcome:
#   - a committed case: exit 0, with metadata.json, residuals.csv, fields.csv and solution.vtk
#     written (a committed example must run; ResultExporter writes all four for a finite result);
#   - a CLI fixture: the exit code the test suite asserts for it (table below), and for exit 0 the
#     same four files, for exit 3 metadata.json and residuals.csv (ProjectRunner exports after every
#     solve; ResultExporter always writes those two);
#   - a fixture missing from the table is INVALID.
# G9_ONLY (optional): an extended regular expression; only matching names (committed case names,
# data_<fixture> for fixtures) are run -- used by the instrument self-test only.
set -u
R=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
LABEL=$1; REF=$2; NEW=$3
W=$HOME/m7g9/compat_$LABEL; rm -rf $W; mkdir -p $W
echo "# G9.2 $LABEL $(date -u +%Y-%m-%dT%H:%M:%SZ); reference $(sha256sum $REF | cut -c1-16) ($REF); candidate $(sha256sum $NEW | cut -c1-16) ($NEW)"
# expected exit code of each CLI fixture, as the test suite asserts it:
#   tests/CMakeLists.txt: valid_cavity_cli_smoke "Converged: yes"; the five invalid fixtures exit 2
#   and does_not_converge exits 3 ("section 32"); mesh_quality_invalid_cli and
#   mesh_quality_disconnected_cli are "rejected before any solver runs" (CaseReader/CaseBuilder ->
#   ProjectRunStatus::InvalidCase -> 2); mesh_quality_warning_cli "Converged: yes"; the two 3D smoke
#   fixtures EXPECT_EXIT=0 and the three malformed 3D fixtures EXPECT_EXIT=2;
#   tests/integration/case/test_case_lid_driven_cavity.cpp: runCaseAndAssertConverged(valid_cavity).
declare -A EXPECT=(
  [data_valid_cavity]=0 [data_valid_cavity_cli_smoke]=0 [data_valid_duct3d_cli_smoke]=0
  [data_valid_cube3d_cli_smoke]=0 [data_mesh_quality_warning_cli]=0 [data_does_not_converge]=3
  [data_missing_mesh]=2 [data_bad_physics]=2 [data_bad_boundary]=2 [data_bad_solver]=2
  [data_malformed_json]=2 [data_invalid_3d_box_without_nz]=2
  [data_invalid_3d_two_component_velocity]=2 [data_invalid_3d_thermal]=2
  [data_mesh_quality_invalid_cli]=2 [data_mesh_quality_disconnected_cli]=2
)
runone() { # binary tag src name
  mkdir -p $W/$2; rm -rf $W/$2/$4; cp -r $3 $W/$2/$4; rm -rf $W/$2/$4/results
  (cd $HOME && $1 --case $W/$2/$4 > $W/$2/$4.stdout 2>&1; echo "exit $?" >> $W/$2/$4.stdout)
}
clean_stdout() { grep -v "$W" $1; }
invalid_reason() { # name -> prints nothing if both sides show the expected outcome
  local name=$1 exp side ec f need
  if [[ $name == data_* ]]; then exp=${EXPECT[$name]-unlisted}; else exp=0; fi
  [ "$exp" = unlisted ] && { echo "fixture not in the expected-exit table"; return; }
  for side in ref new; do
    ec=$(tail -1 $W/$side/$name.stdout | awk '{print $2}')
    [ "$ec" = "$exp" ] || { echo "$side exit $ec, expected $exp"; return; }
    case $exp in
      0) need="metadata.json residuals.csv fields.csv solution.vtk" ;;
      3) need="metadata.json residuals.csv" ;;
      *) need="" ;;
    esac
    for f in $need; do
      [ -s $W/$side/$name/results/$f ] || { echo "$side wrote no results/$f"; return; }
    done
  done
}
compare() { # name
  # (the inherited version started from v="IDENTICAL" and appended " DIFFERENT(metadata)" or
  # " DIFFERENT(stdout)" to it, so identical files with a different metadata.json or stdout still
  # matched IDENTICAL*; the differences are now collected first)
  local d=""
  local files=0
  for f in fields.csv solution.vtk residuals.csv; do
    if [ ! -e $W/ref/$1/results/$f ] && [ ! -e $W/new/$1/results/$f ]; then continue; fi
    files=$((files+1))
    cmp -s $W/ref/$1/results/$f $W/new/$1/results/$f || d="$d DIFFERENT($f)"
  done
  python3 - "$W/ref/$1/results/metadata.json" "$W/new/$1/results/metadata.json" <<'PY' || d="$d DIFFERENT(metadata)"
import json, sys
def load(p):
    try:
        return json.load(open(p))
    except FileNotFoundError:
        return None
sys.exit(0 if load(sys.argv[1]) == load(sys.argv[2]) else 1)
PY
  a=$(clean_stdout $W/ref/$1.stdout | md5sum | cut -c1-8); b=$(clean_stdout $W/new/$1.stdout | md5sum | cut -c1-8)
  [ "$a" = "$b" ] || d="$d DIFFERENT(stdout)"
  local v=${d# }
  [ -z "$v" ] && v="IDENTICAL"
  local why; why=$(invalid_reason $1)
  [ -n "$why" ] && v="INVALID($why) $v"
  echo "ref vs new  $1: $v | files compared $files | $(tail -1 $W/ref/$1.stdout)/$(tail -1 $W/new/$1.stdout)"
  case "$v" in IDENTICAL*) ;; *)
    diff <(clean_stdout $W/ref/$1.stdout) <(clean_stdout $W/new/$1.stdout) | head -6 | sed 's/^/      /'
    python3 - "$W/ref/$1/results/metadata.json" "$W/new/$1/results/metadata.json" <<'PY' | head -6 | sed 's/^/      metadata /'
import json, sys
def leaves(n, p=""):
    if isinstance(n, dict):
        for k, v in n.items(): yield from leaves(v, p + "/" + k)
    elif isinstance(n, list):
        for i, v in enumerate(n): yield from leaves(v, p + f"[{i}]")
    else:
        yield p, n
try:
    a = dict(leaves(json.load(open(sys.argv[1])))); b = dict(leaves(json.load(open(sys.argv[2]))))
except FileNotFoundError:
    sys.exit(0)
for k in sorted(set(a) | set(b)):
    if a.get(k) != b.get(k): print(f"{k}: {a.get(k)!r} -> {b.get(k)!r}")
PY
    ;;
  esac
}
only() { [ -z "${G9_ONLY:-}" ] || [[ $1 =~ $G9_ONLY ]]; }
CASES=""
for c in $R/cases/*; do [ -e $c/case.json ] && only "$(basename $c)" && CASES="$CASES $(basename $c)"; done
FIXTURES=""
for c in $R/tests/data/cases/*; do [ -e $c/case.json ] && only "data_$(basename $c)" && FIXTURES="$FIXTURES $(basename $c)"; done
[ -n "${G9_ONLY:-}" ] && echo "# G9_ONLY='$G9_ONLY' (instrument self-test subset)"
# at most G9_JOBS (default 16) cases at a time (local resource policy); each case runs the two
# binaries one after the other; the outputs are compared byte for byte, so the schedule cannot
# change them
export W REF NEW
export -f runone
{
  for c in $CASES; do echo "$R/cases/$c $c"; done
  for c in $FIXTURES; do echo "$R/tests/data/cases/$c data_$c"; done
} | xargs -P "${G9_JOBS:-16}" -L1 bash -c 'runone "$NEW" new "$0" "$1"; runone "$REF" ref "$0" "$1"'
OUT=$W/compare.txt
{
  echo "=== committed cases ($(echo $CASES | wc -w))"
  for c in $CASES; do compare $c; done
  echo "=== CLI data fixtures ($(echo $FIXTURES | wc -w))"
  for c in $FIXTURES; do compare data_$c; done
} > $OUT
cat $OUT
TOTAL=$(( $(echo $CASES | wc -w) + $(echo $FIXTURES | wc -w) ))
echo "=== totals: $(grep -c ': IDENTICAL' $OUT) IDENTICAL of $TOTAL; INVALID $(grep -c ': INVALID(' $OUT); DIFFERENT (valid runs) $(grep ': DIFFERENT' $OUT | grep -vc ': INVALID(')"
