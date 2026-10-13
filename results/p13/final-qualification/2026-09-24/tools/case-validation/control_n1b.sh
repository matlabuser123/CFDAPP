#!/usr/bin/env bash
# P13-002, control N1b -- the CORRECTED control. See acceptance_gate_A1.md.
#
# N1 reworded "must satisfy" -> "must be" and the GUI test still passed. That
# was a badly-chosen reword, not a refuted premise: fieldForMessage() reads the
# `field "<name>"` marker and N1 changed a different clause. N1's result is
# preserved verbatim in N1_control.log; this control changes the marker the
# scraper ACTUALLY reads.
#
# The edit lives in patch_field_marker.py, not in a heredoc here: a C++ string
# literal full of escaped quotes does not survive a shell heredoc reliably, and
# the first attempt at this control was lost to exactly that -- it left the
# production file modified, which the caller then had to notice and repair.
# Hence the restore is VERIFIED here rather than trusted to the trap.
set -uo pipefail
ROOT=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
T=$ROOT/results/p13/final-qualification/2026-09-24/tools/case-validation
L=$ROOT/results/p13/final-qualification/2026-09-24/case_validation_logs
FILE=src/io/case/JsonUtil.cpp
BACKUP=/tmp/p13_n1b_backup.cpp
TEST=CaseEditingTest.ValidateDraftExtractsTheOffendingFieldName
cd "$ROOT"
mkdir -p "$L"

cp "$FILE" "$BACKUP"
ORIGINAL=$(sha256sum "$FILE" | cut -d' ' -f1)

restore_and_verify() {
  cp "$BACKUP" "$FILE"
  local now
  now=$(sha256sum "$FILE" | cut -d' ' -f1)
  if [ "$now" != "$ORIGINAL" ]; then
    echo "RESTORE FAILED: $now != $ORIGINAL -- repair $FILE by hand before continuing"
    return 1
  fi
  return 0
}
trap 'restore_and_verify || exit 99' EXIT

log=$L/N1b_control.log
{ echo "P13-002 control N1b -- the corrected control (N1 was mis-designed)"
  echo "date:     $(date -u '+%Y-%m-%dT%H:%M:%SZ')"
  echo "tree:     UNCHANGED (this runs before any P13-002 production change)"
  echo "file:     $FILE"
  echo "sha256:   $ORIGINAL"
  echo "edit:     fieldLabel  field \"<name>\"  ->  key \"<name>\""
  echo ""; } > "$log"

say() { echo "$*" | tee -a "$log"; }

# --- 1. the GUI test must pass first, or the control measures nothing -------
say "--- 1. the test passes on the unchanged tree ---"
if ! cmake --build build/debug -j20 > /tmp/p13_n1b_build.log 2>&1; then
  say "  BUILD FAILED"; tail -5 /tmp/p13_n1b_build.log | tee -a "$log"; exit 1
fi
if ctest --test-dir build/debug -R "$TEST" > "$L/N1b-before.log" 2>&1; then
  say "  PASS -- the GUI recovers field=nx today"
else
  say "  the test does not pass on the unchanged tree; N1b cannot say anything"; exit 1
fi

# --- 2. rename the marker ---------------------------------------------------
say "--- 2. rename the marker the scraper reads, change nothing else ---"
if ! python3 "$T/patch_field_marker.py" "$FILE"; then
  say "  could not apply the rename"; exit 1
fi
say "  patched sha256 $(sha256sum "$FILE" | cut -d' ' -f1)"
if ! cmake --build build/debug -j20 > /tmp/p13_n1b_build.log 2>&1; then
  say "  BUILD FAILED after the rename"; tail -5 /tmp/p13_n1b_build.log | tee -a "$log"; exit 1
fi

if ctest --test-dir build/debug -R "$TEST" > "$L/N1b-after.log" 2>&1; then
  say "  the test STILL PASSES after renaming the marker"
  say ""
  say "N1b: FRAGILITY NOT DEMONSTRATED -- the scrape survives even a rename of"
  say "     the token it reads. Fragility is therefore NOT claimed anywhere in"
  say "     this item's evidence; it proceeds on absence and coupling alone."
  verdict=1
else
  say "  the test FAILS after the rename -- the GUI lost the field"
  grep -m4 -E "Failure|Expected|Actual|Which is|Value of" "$L/N1b-after.log" \
    | sed 's/^/      /' | tee -a "$log"
  say ""
  say "N1b: FRAGILITY DEMONSTRATED -- an ordinary wording change to one token"
  say "     silently breaks the GUI's field extraction, with no test of the"
  say "     coupling itself. Carrying the diagnostic as data removes the coupling."
  verdict=0
fi

# --- 3. restore, and PROVE it ----------------------------------------------
trap - EXIT
say "--- 3. restored ---"
if ! restore_and_verify; then say "  RESTORE FAILED"; exit 99; fi
say "  sha256 $(sha256sum "$FILE" | cut -d' ' -f1) IDENTICAL"
cmake --build build/debug -j20 > /dev/null 2>&1
if ctest --test-dir build/debug -R "$TEST" > /dev/null 2>&1; then
  say "  the test passes again: yes"
else
  say "  THE TEST DOES NOT PASS AFTER RESTORE"; exit 1
fi

# Either verdict is a legitimate outcome of this control -- see
# acceptance_gate_A1.md section 5, which fixed the reading of both in advance.
exit 0
