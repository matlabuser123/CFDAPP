#!/usr/bin/env bash
# P13-002 -- the regression that says the item actually worked.
#
# Control N1b ran the SAME experiment on the tree BEFORE this item: rename the
# one token the GUI's scraper reads (`field "` -> `key "`) and the GUI's field
# extraction broke, with no test noticing. That result is preserved in
# logs/N1b_control_BEFORE_FIX.log.
#
# This runs the identical experiment on the tree AFTER, and requires the
# OPPOSITE outcome: nothing breaks, because section and field now come from
# the error's own cfd::CaseDiagnostic instead of from its message text.
#
# The two scripts are kept separate on purpose. Reusing N1b with its verdict
# inverted would make the historical control's meaning depend on which tree it
# last ran against, and a control whose pass condition moves is not a control.
#
# Same safety discipline: sha256 before, unique-string patch asserted to
# match, restore VERIFIED afterwards.
set -uo pipefail
ROOT=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
T=$ROOT/results/p13/final-qualification/2026-09-24/tools/case-validation
L=$ROOT/results/p13/final-qualification/2026-09-24/case_validation_logs
FILE=src/io/case/JsonUtil.cpp
BACKUP=/tmp/p13_rename_backup.cpp
# Every GUI test that reads a validation section or field. Widened from N1b's
# single test, because the claim is now "nothing depends on the wording", and
# one test cannot say that.
TESTS='CaseEditingTest.ValidateDraft'
cd "$ROOT"

cp "$FILE" "$BACKUP"
ORIGINAL=$(sha256sum "$FILE" | cut -d' ' -f1)
restore_and_verify() {
  cp "$BACKUP" "$FILE"
  local now
  now=$(sha256sum "$FILE" | cut -d' ' -f1)
  [ "$now" = "$ORIGINAL" ] || { echo "RESTORE FAILED: $now != $ORIGINAL"; return 1; }
  return 0
}
trap 'restore_and_verify || exit 99' EXIT

log=$L/regression_marker_rename.log
{ echo "P13-002 regression -- renaming the message marker must no longer break the GUI"
  echo "date:   $(date -u '+%Y-%m-%dT%H:%M:%SZ')"
  echo "tree:   AFTER the structured-diagnostic change"
  echo "sha256: $ORIGINAL"
  echo "tests:  $TESTS"
  echo ""; } > "$log"
say() { echo "$*" | tee -a "$log"; }

say "--- 1. the tests pass on the unrenamed tree ---"
cmake --build build/debug -j20 > /tmp/p13_rename_build.log 2>&1 || {
  say "  BUILD FAILED"; tail -5 /tmp/p13_rename_build.log | tee -a "$log"; exit 1; }
if ctest --test-dir build/debug -R "$TESTS" > "$L/rename-before.log" 2>&1; then
  n=$(grep -c "Passed" "$L/rename-before.log")
  say "  PASS -- $n validation tests green"
else
  say "  the tests do not pass before the rename; this regression cannot say anything"; exit 1
fi

say "--- 2. rename the marker the OLD scraper read ---"
python3 "$T/patch_field_marker.py" "$FILE" || { say "  could not apply the rename"; exit 1; }
say "  patched sha256 $(sha256sum "$FILE" | cut -d' ' -f1)"
cmake --build build/debug -j20 > /tmp/p13_rename_build.log 2>&1 || {
  say "  BUILD FAILED after the rename"; exit 1; }

if ctest --test-dir build/debug -R "$TESTS" > "$L/rename-after.log" 2>&1; then
  say "  the tests STILL PASS after the rename"
  say ""
  say "REGRESSION: PASS -- the GUI no longer depends on the message wording."
  say "  Before this item, the identical rename broke it (N1b_control_BEFORE_FIX.log)."
  verdict=0
else
  say "  the tests FAIL after the rename -- something still reads the message text"
  grep -m6 -E "Failure|Expected|Actual|Value of|\(Failed\)" "$L/rename-after.log" \
    | sed 's/^/      /' | tee -a "$log"
  say ""
  say "REGRESSION: FAIL -- the coupling this item set out to remove is still there."
  verdict=1
fi

trap - EXIT
say "--- 3. restored ---"
restore_and_verify || { say "  RESTORE FAILED"; exit 99; }
say "  sha256 $(sha256sum "$FILE" | cut -d' ' -f1) IDENTICAL"
cmake --build build/debug -j20 > /dev/null 2>&1
ctest --test-dir build/debug -R "$TESTS" > /dev/null 2>&1 \
  && say "  the tests pass again: yes" \
  || { say "  THE TESTS DO NOT PASS AFTER RESTORE"; exit 1; }
exit $verdict
