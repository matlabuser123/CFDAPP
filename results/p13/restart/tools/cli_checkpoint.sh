#!/usr/bin/env bash
# P13-001, group F -- the PRODUCTION path.
#
# CLAUDE.md section 9: a library API is not production capability. This script
# exercises checkpointing the way a user would, through the shipped `cfdapp`
# binary and real case directories, and compares the exported results.
#
# The genuine production flow, and the one that proves the case-fingerprint
# rule is right:
#
#   run A   cfdapp --case A                        max_iterations = N
#   run B1  cfdapp --case B --checkpoint ck.json   max_iterations = N1
#   run B2  cfdapp --case B --resume ck.json       max_iterations = N   <- edited
#
# B2 edits solver.json's max_iterations between the two runs, which is exactly
# the one key the case fingerprint excludes. If that exclusion were wrong, B2
# would be refused; if the fingerprint ignored anything else, a real
# configuration change would slip through -- which run D below checks.
#
# Every run is a separate PROCESS, so nothing is carried in memory.
set -uo pipefail
ROOT=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
CFDAPP=$ROOT/build/final/apps/cli/cfdapp
WORK=$(mktemp -d /tmp/p13cli.XXXXXX)
CASE=${1:-lid_driven_cavity}
TOTAL=${2:-400}
SPLIT=${3:-150}
fail=0

note() { printf '%s\n' "$*"; }
check() { # check <label> <condition-rc>
  if [ "$2" -eq 0 ]; then note "  PASS $1"; else note "  FAIL $1"; fail=1; fi
}

note "case      $CASE"
note "total     $TOTAL outer iterations"
note "split at  $SPLIT"
note "binary    $CFDAPP"
note "sha256    $(sha256sum "$CFDAPP" | cut -d' ' -f1)"
note ""

setup() { # setup <dir> <max_iterations>
  rm -rf "$1"; cp -r "$ROOT/cases/$CASE" "$1"; rm -rf "$1/results"
  python3 - "$1/solver.json" "$2" <<'PY'
import json, sys
p, n = sys.argv[1], int(sys.argv[2])
d = json.load(open(p))
d["max_iterations"] = n
json.dump(d, open(p, "w"), indent=2)
PY
}

# --- A: one uninterrupted run ------------------------------------------------
note "=== A: continuous run ==="
setup "$WORK/A" "$TOTAL"
"$CFDAPP" --case "$WORK/A" > "$WORK/A.out" 2>&1
note "  exit $?  iterations $(grep -m1 '^Iterations:' "$WORK/A.out" | awk '{print $2}')"

# --- B: stop at SPLIT, then resume to TOTAL in a second process --------------
note "=== B: checkpoint at $SPLIT, resume to $TOTAL ==="
setup "$WORK/B" "$SPLIT"
"$CFDAPP" --case "$WORK/B" --checkpoint "$WORK/ck.json" > "$WORK/B1.out" 2>&1
b1rc=$?
note "  segment 1 exit $b1rc  iterations $(grep -m1 '^Iterations:' "$WORK/B1.out" | awk '{print $2}')"
check "a checkpoint file was written" "$([ -s "$WORK/ck.json" ] && echo 0 || echo 1)"

# The one edit a resume is allowed to make: raise the budget and finish.
python3 - "$WORK/B/solver.json" "$TOTAL" <<'PY'
import json, sys
p, n = sys.argv[1], int(sys.argv[2])
d = json.load(open(p)); d["max_iterations"] = n
json.dump(d, open(p, "w"), indent=2)
PY
"$CFDAPP" --case "$WORK/B" --resume "$WORK/ck.json" > "$WORK/B2.out" 2>&1
b2rc=$?
note "  segment 2 exit $b2rc  iterations $(grep -m1 '^Iterations:' "$WORK/B2.out" | awk '{print $2}')"
check "the resumed run reported resuming" \
      "$(grep -q '^Resumed from iteration: '"$SPLIT" "$WORK/B2.out" && echo 0 || echo 1)"

# --- F2: the exported results must agree -------------------------------------
note "=== F2: exported results, continuous vs resumed ==="
for f in fields.csv residuals.csv metadata.json; do
  if [ ! -f "$WORK/A/results/$f" ] || [ ! -f "$WORK/B/results/$f" ]; then
    note "  FAIL $f missing from one of the runs"; fail=1; continue
  fi
  if diff -q "$WORK/A/results/$f" "$WORK/B/results/$f" > /dev/null; then
    note "  PASS $f identical"
  else
    note "  FAIL $f differs:"
    diff "$WORK/A/results/$f" "$WORK/B/results/$f" | head -8 | sed 's/^/        /'
    fail=1
  fi
done

# --- C: --checkpoint-every, segmenting the whole run -------------------------
note "=== C: --checkpoint-every $((SPLIT / 3)) over the full run ==="
setup "$WORK/C" "$TOTAL"
"$CFDAPP" --case "$WORK/C" --checkpoint "$WORK/ckC.json" --checkpoint-every $((SPLIT / 3)) \
  > "$WORK/C.out" 2>&1
note "  exit $?  $(grep -m1 '^Checkpoints written:' "$WORK/C.out")"
for f in fields.csv metadata.json; do
  diff -q "$WORK/A/results/$f" "$WORK/C/results/$f" > /dev/null
  check "segmented run matches continuous run: $f" $?
done

# --- D: the rejection matrix, through the CLI --------------------------------
note "=== D: rejections through the CLI ==="

expect_fail() { # expect_fail <label> <expected-substring> <args...>
  local label=$1 expect=$2; shift 2
  "$CFDAPP" "$@" > "$WORK/reject.out" 2>&1
  local rc=$?
  if [ $rc -eq 0 ]; then
    note "  FAIL $label -- exited 0"; fail=1; return
  fi
  if grep -qi -- "$expect" "$WORK/reject.out"; then
    note "  PASS $label (exit $rc)"
  else
    note "  FAIL $label -- message did not mention \"$expect\":"
    head -3 "$WORK/reject.out" | sed 's/^/        /'
    fail=1
  fi
}

setup "$WORK/D" "$TOTAL"
expect_fail "missing checkpoint file"      "could not resume"  --case "$WORK/D" --resume "$WORK/nope.json"
printf '{ "format_version": 1, "identi' > "$WORK/trunc.json"
expect_fail "truncated checkpoint"         "could not resume"  --case "$WORK/D" --resume "$WORK/trunc.json"
python3 - "$WORK/ck.json" "$WORK/badver.json" <<'PY'
import json, sys
d = json.load(open(sys.argv[1])); d["format_version"] = 99
json.dump(d, open(sys.argv[2], "w"))
PY
expect_fail "unsupported format version"   "version"           --case "$WORK/D" --resume "$WORK/badver.json"
python3 - "$WORK/ck.json" "$WORK/nokey.json" <<'PY'
import json, sys
d = json.load(open(sys.argv[1])); del d["fields"]["mass_flux"]
json.dump(d, open(sys.argv[2], "w"))
PY
expect_fail "missing required key"         "mass_flux"         --case "$WORK/D" --resume "$WORK/nokey.json"
python3 - "$WORK/ck.json" "$WORK/short.json" <<'PY'
import json, sys
d = json.load(open(sys.argv[1])); d["fields"]["pressure"] = d["fields"]["pressure"][:-1]
json.dump(d, open(sys.argv[2], "w"))
PY
expect_fail "truncated field array"        "pressure"          --case "$WORK/D" --resume "$WORK/short.json"
python3 - "$WORK/ck.json" "$WORK/nan.json" <<'PY'
import json, sys
d = json.load(open(sys.argv[1]))
# NaN is not valid JSON, so it has to arrive the way a corrupt file would.
text = json.dumps(d).replace('"pressure": [', '"pressure": [NaN,', 1)
open(sys.argv[2], "w").write(text)
PY
expect_fail "NaN in a field"               "could not resume"  --case "$WORK/D" --resume "$WORK/nan.json"

# A different case: same solver, different physics -> the case fingerprint must
# refuse it even though the mesh is identical.
setup "$WORK/E" "$TOTAL"
python3 - "$WORK/E/physics.json" <<'PY'
import json, sys
p = sys.argv[1]; d = json.load(open(p))
d["dynamic_viscosity"] = d["dynamic_viscosity"] * 2.0
json.dump(d, open(p, "w"), indent=2)
PY
expect_fail "different case configuration" "case identity mismatch" \
            --case "$WORK/E" --resume "$WORK/ck.json"

# A different mesh, same case shape.
setup "$WORK/F" "$TOTAL"
python3 - "$WORK/F/mesh.json" <<'PY'
import json, sys
p = sys.argv[1]; d = json.load(open(p))
d["nx"] = int(d["nx"]) + 2
json.dump(d, open(p, "w"), indent=2)
PY
expect_fail "different mesh"               "mesh identity mismatch" \
            --case "$WORK/F" --resume "$WORK/ck.json"

# --- C14: a transport-equation turbulence model is refused, both ways --------
#
# The refusal must be specific to CHECKPOINTING. If the same case also failed
# to run normally, this row would prove nothing about the refusal -- so the
# plain run is required to succeed first.
note "=== C14: transport-equation turbulence ==="
setup "$WORK/T" 5
python3 - "$WORK/T/physics.json" <<'PY'
import json, sys
p = sys.argv[1]; d = json.load(open(p))
d["turbulence"] = {"model": "k_epsilon", "initial_k": 0.02, "initial_epsilon": 0.005}
json.dump(d, open(p, "w"), indent=2)
PY
"$CFDAPP" --case "$WORK/T" > "$WORK/T.out" 2>&1
trc=$?
if [ $trc -eq 0 ] || [ $trc -eq 3 ]; then
  note "  PASS the k_epsilon case itself runs normally (exit $trc) -- the rows below are specific"
else
  note "  FAIL the k_epsilon case does not even run (exit $trc); the refusal rows prove nothing"
  head -3 "$WORK/T.out" | sed 's/^/        /'
  fail=1
fi
expect_fail "--checkpoint is refused for k_epsilon" "turbulence model" \
            --case "$WORK/T" --checkpoint "$WORK/ckT.json"
expect_fail "--resume is refused for k_epsilon"     "turbulence model" \
            --case "$WORK/T" --resume "$WORK/ck.json"
check "the refusal names the models" \
      "$(grep -q 'k_epsilon, k_omega or sst' "$WORK/reject.out" && echo 0 || echo 1)"
check "no checkpoint file was created for the refused case" \
      "$([ ! -e "$WORK/ckT.json" ] && echo 0 || echo 1)"

# --- F4: flag handling --------------------------------------------------------
note "=== F4: flags ==="
"$CFDAPP" --help > "$WORK/help.out" 2>&1
check "--help documents --checkpoint"       "$(grep -q -- '--checkpoint ' "$WORK/help.out" && echo 0 || echo 1)"
check "--help documents --resume"           "$(grep -q -- '--resume' "$WORK/help.out" && echo 0 || echo 1)"
check "--help documents --checkpoint-every" "$(grep -q -- '--checkpoint-every' "$WORK/help.out" && echo 0 || echo 1)"
expect_fail "--checkpoint-every without --checkpoint" "requires --checkpoint" \
            --case "$WORK/D" --checkpoint-every 10
expect_fail "--checkpoint-every with a non-number"    "positive integer" \
            --case "$WORK/D" --checkpoint "$WORK/x.json" --checkpoint-every ten
expect_fail "--checkpoint-every 0"                    "positive integer" \
            --case "$WORK/D" --checkpoint "$WORK/x.json" --checkpoint-every 0
expect_fail "--resume with no argument"               "requires an argument" \
            --case "$WORK/D" --resume

# --- F3: one execution path, asserted structurally ---------------------------
#
# CLAUDE.md section 9 and ProjectRunner.hpp's own "ONE SOLVER BACKEND" rule.
# Comparing CLI and GUI outputs after the fact would only show they agree on
# the cases compared; what is required is that there IS no second path.
note "=== F3: one execution path ==="
impl=$(grep -rl "runSimpleWithCheckpointing" "$ROOT/src" "$ROOT/apps" 2>/dev/null | wc -l)
check "the checkpoint orchestration exists in exactly one file (found $impl)" \
      "$([ "$impl" -eq 1 ] && echo 0 || echo 1)"
check "that file is src/app/ProjectRunner.cpp" \
      "$(grep -rl 'runSimpleWithCheckpointing' "$ROOT/src" "$ROOT/apps" 2>/dev/null \
         | grep -q 'src/app/ProjectRunner.cpp$' && echo 0 || echo 1)"
guiCheckpoint=$(grep -rl "CheckpointWriter\|CheckpointReader\|SIMPLECheckpoint\|setContinuation" \
                  "$ROOT/apps/gui" 2>/dev/null | wc -l)
check "the GUI contains no checkpoint implementation of its own (found $guiCheckpoint files)" \
      "$([ "$guiCheckpoint" -eq 0 ] && echo 0 || echo 1)"
cliCheckpoint=$(grep -c "CheckpointWriter\|CheckpointReader\|setContinuation" \
                  "$ROOT/apps/cli/main.cpp" 2>/dev/null)
check "the CLI contains none either -- it only sets ProjectRunOptions (found $cliCheckpoint)" \
      "$([ "$cliCheckpoint" -eq 0 ] && echo 0 || echo 1)"

note ""
if [ $fail -eq 0 ]; then note "GROUP F: PASS"; else note "GROUP F: FAIL"; fi
note "work dir kept at $WORK"
exit $fail
