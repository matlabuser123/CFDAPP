#!/usr/bin/env bash
# P13 FINAL QUALIFICATION -- G4, finding F3: the frozen N controls that were
# specified in results/p13/cli-gui/acceptance_gate.md and never implemented.
#
# The frozen definitions are used as written. They are NOT redefined:
#
#   N2  "Compare the GUI arm against itself instead of against the CLI arm"
#       -> "the harness must declare the comparison vacuous and fail, not pass"
#   N4  "Point the CLI-flag ctests at a binary without the flags"  -> V3 must fail
#
# N2 is run in two halves because the authorization requires both:
#   N2a  the frozen definition, literally: the CLI arm is replaced by the GUI
#        arm and the harness must refuse.
#   N2b  the added requirement: the hasResults() / non-vacuity protection must
#        be shown to fail when the GUI's results are ABSENT, rather than the
#        control passing through a vacuous path.
#
# N5 is not from the frozen gate. It is the non-vacuity control for the
# E-thermal row this gate just added (F2): pointed at a case with no thermal
# block, ThermalExpectation::MustHaveRun must fire. A new criterion ships with
# its own control or it is not evidence.
#
# Every control: sha256 before, patch target asserted UNIQUE, restore VERIFIED
# by sha256 here rather than trusted to the trap, and the trap kept as a
# backstop that exits 99 if it ever has to act.
set -uo pipefail
ROOT=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
L=$ROOT/results/p13/final-qualification/2026-09-24/g4-cli-gui/logs
cd "$ROOT"
mkdir -p "$L"

FILES=(apps/gui/tests/test_cli_gui_equivalence.cpp
       apps/gui/SimulationController.cpp
       apps/cli/main.cpp)
BACKUP=$(mktemp -d /tmp/p13_g4_controls.XXXXXX)
declare -A SHA
for f in "${FILES[@]}"; do
  cp "$f" "$BACKUP/$(basename "$f")"
  SHA[$f]=$(sha256sum "$f" | cut -d' ' -f1)
done

restore_and_verify() {
  local bad=0
  for f in "${FILES[@]}"; do
    cp "$BACKUP/$(basename "$f")" "$f"
    local now; now=$(sha256sum "$f" | cut -d' ' -f1)
    if [ "$now" != "${SHA[$f]}" ]; then echo "RESTORE FAILED for $f" ; bad=1; fi
  done
  return $bad
}
trap 'echo "TRAP FIRED -- restoring"; restore_and_verify || exit 99' EXIT

log=$L/G4_controls_n2_n4.log
{ echo "P13 G4 -- controls N2a, N2b, N4, N5"
  echo "date: $(date -u '+%Y-%m-%dT%H:%M:%SZ')"
  echo "baseline sha256:"
  for f in "${FILES[@]}"; do echo "  ${SHA[$f]}  $f"; done
  echo ""; } > "$log"
say() { echo "$*" | tee -a "$log"; }
fail=0

patch_once() {  # patch_once <file> <old> <new>
  python3 - "$1" "$2" "$3" <<'PY'
import sys
path, old, new = sys.argv[1], sys.argv[2], sys.argv[3]
text = open(path, encoding="utf-8").read()
n = text.count(old)
if n != 1:
    sys.stderr.write("PATCH TARGET NOT UNIQUE (%d occurrences) in %s\n" % (n, path))
    sys.exit(1)
open(path, "w", encoding="utf-8", newline="\n").write(text.replace(old, new, 1))
PY
}

build() { cmake --build build/debug -j20 > /tmp/p13_g4_build.log 2>&1; }

# report_control <id> <criterion-result> <detection> <rc>
report() {
  say "  PLANTED DEFECT     yes"
  say "  CRITERION          $2"
  say "  DETECTION          $3"
  if restore_and_verify; then
    local ok=1
    for f in "${FILES[@]}"; do
      [ "$(sha256sum "$f" | cut -d' ' -f1)" = "${SHA[$f]}" ] || ok=0
    done
    [ $ok -eq 1 ] && say "  RESTORE            VERIFIED by SHA-256" \
                  || { say "  RESTORE            FAILED"; fail=1; }
  else
    say "  RESTORE            FAILED"; fail=1
  fi
  say "  RETURN CODE        $4"
  say ""
}

# ---------------------------------------------------------------- N2a --------
say "=== N2a -- the CLI arm is replaced by the GUI arm (frozen definition) ==="
# PATCH TARGET MOVED by the F4 repair. Until 2026-09-25 this substituted
# `ProjectRunResult cliRun = ProjectRunner::run(...)` with `= guiRun`. Results
# now travel as a tagged ArmResult, so the substitution is of the ARM, which is
# what carries provenance. The control's meaning is unchanged: one client's
# result stands in for the other's.
patch_once apps/gui/tests/test_cli_gui_equivalence.cpp \
  '  ASSERT_NO_FATAL_FAILURE(runCliArm(caseDirectory, cli));' \
  '  cli = gui;  // N2a CONTROL -- the CLI arm is replaced by the GUI arm' \
  || { say "  PATCH FAILED"; exit 1; }
if ! build; then
  say "  BUILD FAILED"; say ""; fail=1; restore_and_verify
else
  ctest --test-dir build/debug -R 'CliGuiEquivalenceTest' --output-on-failure \
    > "$L/N2a-after.log" 2>&1
  rc=$?
  n=$(grep -cE '\(Failed\)' "$L/N2a-after.log" || true)
  # The failure must come from the PROVENANCE assertion, not from some unrelated
  # numeric or filesystem difference that happens to fail at the same time.
  if [ $rc -ne 0 ] && grep -q 'self-comparison' "$L/N2a-after.log"; then
    report N2a "FAILED as expected ($n tests)" \
      "DETECTED -- the provenance assertion fired by name ('self-comparison')" \
      "nonzero ($rc), expected"
  elif [ $rc -ne 0 ]; then
    say "  PLANTED DEFECT     yes"
    say "  CRITERION          FAILED ($n tests) -- but NOT via the provenance assertion"
    say "  DETECTION          AMBIGUOUS -- the planted self-comparison was caught by"
    say "                     something other than client provenance. Not accepted."
    restore_and_verify && say "  RESTORE            VERIFIED by SHA-256" \
                       || { say "  RESTORE            FAILED"; fail=1; }
    say "  RETURN CODE        nonzero ($rc)"
    say ""
    fail=1
  else
    say "  PLANTED DEFECT     yes"
    say "  CRITERION          PASSED -- the harness did NOT refuse"
    say "  DETECTION          UNDETECTED"
    restore_and_verify && say "  RESTORE            VERIFIED by SHA-256" \
                       || { say "  RESTORE            FAILED"; fail=1; }
    say "  RETURN CODE        0 -- NOT expected"
    say ""
    say "  N2a IS A CRITERION DEFECT, PRESERVED, NOT REDEFINED."
    say "  The frozen gate requires the harness to 'declare the comparison"
    say "  vacuous and fail'. It does not. The harness header claims 'Every case"
    say "  asserts the two results came from different clients before comparing"
    say "  a single value' -- the assertions present (hasResults(), the"
    say "  completed() signal) prove the GUI arm RAN, not that the second arm is"
    say "  a DIFFERENT client. Comparing the GUI arm with itself therefore"
    say "  satisfies every E row for free. Reported as UNDETECTED."
    say ""
    fail=1
  fi
fi

# ---------------------------------------------------------------- N2b --------
say "=== N2b -- the GUI produces no results (the protection must fire) ==="
patch_once apps/gui/SimulationController.cpp \
  '    snapshot_ = std::move(snapshot);' \
  '    snapshot_ = VisualizationSnapshot{};  // N2b CONTROL -- results dropped' \
  || { say "  PATCH FAILED"; exit 1; }
if ! build; then
  say "  BUILD FAILED"; say ""; fail=1; restore_and_verify
else
  ctest --test-dir build/debug -R 'CliGuiEquivalenceTest' > "$L/N2b-after.log" 2>&1
  rc=$?
  n=$(grep -cE '\(Failed\)' "$L/N2b-after.log" || true)
  if [ $rc -ne 0 ] && grep -q 'nothing to.*compare\|vacuous\|hasResults' "$L/N2b-after.log"; then
    report N2b "FAILED as expected ($n tests)" "DETECTED -- the vacuity guard fired by name" "nonzero ($rc), expected"
  elif [ $rc -ne 0 ]; then
    report N2b "FAILED as expected ($n tests)" "DETECTED (guard message not matched by name)" "nonzero ($rc), expected"
  else
    say "  PLANTED DEFECT     yes"
    say "  CRITERION          PASSED -- absent results were not noticed"
    say "  DETECTION          UNDETECTED"
    restore_and_verify && say "  RESTORE            VERIFIED by SHA-256" \
                       || { say "  RESTORE            FAILED"; fail=1; }
    say "  RETURN CODE        0 -- NOT expected"
    say ""
    fail=1
  fi
fi

# ---------------------------------------------------------------- N4 ---------
say "=== N4 -- the CLI binary loses its --checkpoint-every validation ==="
say "  frozen definition: 'point the CLI-flag ctests at a binary without the"
say "  flags'. The ctests resolve their binary through \$<TARGET_FILE:cfdapp> at"
say "  configure time, so they cannot be repointed at another file without"
say "  reconfiguring the project. The equivalent, and the failure mode V3's own"
say "  comment names, is to build cfdapp WITHOUT the flag's validation so a bad"
say "  interval becomes a silent 0. Recorded here rather than done silently."
patch_once apps/cli/main.cpp \
  "      if (end == value.c_str() || *end != '\\0' || parsed < 1) {" \
  '      if (false) {  // N4 CONTROL -- a bad interval becomes a silent 0' \
  || { say "  PATCH FAILED"; exit 1; }
if ! build; then
  say "  BUILD FAILED"; say ""; fail=1; restore_and_verify
else
  ctest --test-dir build/debug -R 'CFDAppCliCheckpoint' > "$L/N4-after.log" 2>&1
  rc=$?
  n=$(grep -cE '\(Failed\)' "$L/N4-after.log" || true)
  if [ $rc -ne 0 ]; then
    report N4 "FAILED as expected ($n tests)" "DETECTED" "nonzero ($rc), expected"
  else
    say "  PLANTED DEFECT     yes"
    say "  CRITERION          PASSED -- V3 did not notice a silently-ignored flag"
    say "  DETECTION          UNDETECTED"
    restore_and_verify && say "  RESTORE            VERIFIED by SHA-256" \
                       || { say "  RESTORE            FAILED"; fail=1; }
    say "  RETURN CODE        0 -- NOT expected"
    say ""
    fail=1
  fi
fi

# ---------------------------------------------------------------- N5 ---------
say "=== N5 -- the new E-thermal row is pointed at a case with no thermal block ==="
patch_once apps/gui/tests/test_cli_gui_equivalence.cpp \
  '  CaseFixtureCopy fixture("cases/heated_cavity");' \
  '  CaseFixtureCopy fixture("tests/data/cases/valid_cavity");  // N5 CONTROL' \
  || { say "  PATCH FAILED"; exit 1; }
if ! build; then
  say "  BUILD FAILED"; say ""; fail=1; restore_and_verify
else
  ctest --test-dir build/debug -R 'CliGuiEquivalenceTest.AThermalCaseAgreesBitwise' \
    > "$L/N5-after.log" 2>&1
  rc=$?
  n=$(grep -cE '\(Failed\)' "$L/N5-after.log" || true)
  if [ $rc -ne 0 ]; then
    report N5 "FAILED as expected ($n tests)" "DETECTED -- MustHaveRun fired" "nonzero ($rc), expected"
  else
    say "  PLANTED DEFECT     yes"
    say "  CRITERION          PASSED -- a non-thermal case satisfied E-thermal"
    say "  DETECTION          UNDETECTED -- the row is VACUOUS"
    restore_and_verify && say "  RESTORE            VERIFIED by SHA-256" \
                       || { say "  RESTORE            FAILED"; fail=1; }
    say "  RETURN CODE        0 -- NOT expected"
    say ""
    fail=1
  fi
fi

# ------------------------------------------------------------- closeout ------
trap - EXIT
say "=== restored, and the tree proved back at baseline ==="
restore_and_verify || { say "  RESTORE FAILED"; exit 99; }
for f in "${FILES[@]}"; do
  now=$(sha256sum "$f" | cut -d' ' -f1)
  if [ "$now" = "${SHA[$f]}" ]; then say "  $now  $f  IDENTICAL"
  else say "  $now  $f  DIFFERS -- baseline was ${SHA[$f]}"; fail=1; fi
done
say "  rebuilding at baseline and re-running the whole G4 harness"
build || { say "  BASELINE REBUILD FAILED"; exit 1; }
if ctest --test-dir build/debug -R 'CliGui|CFDAppCliCheckpoint' > "$L/G4_after_controls.log" 2>&1; then
  say "  all G4 tests pass again: yes"
else
  say "  THEY DO NOT PASS AFTER RESTORE -- see G4_after_controls.log"; fail=1
fi
rm -rf "$BACKUP"
say ""
[ $fail -eq 0 ] && say "G4 GROUP N: ALL DETECTED, ALL RESTORED" \
                || say "G4 GROUP N: SEE ABOVE -- at least one control did not behave as the gate requires"
exit $fail
