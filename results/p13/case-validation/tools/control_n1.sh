#!/usr/bin/env bash
# P13-002, control N1 -- does this item's premise hold?
#
# The claim: the GUI recovers the offending field by STRING-SCRAPING the error
# message (`fieldForMessage` searches for `field "`), so a change to the
# message wording silently breaks it with no test failing.
#
# If rewording a message does NOT break the GUI, the scraping is not fragile
# and P13-002's premise is wrong. So this is run against the UNCHANGED tree,
# BEFORE anything is built, and its result is recorded either way.
#
# The reword is the kind a maintainer would make without a second thought:
#   "must satisfy"  ->  "must be"
#
# Safety: the file's sha256 is recorded, the edit is a unique-string
# replacement asserted to have matched, and the file is restored and re-hashed
# on every exit path including an interrupt.
set -uo pipefail
ROOT=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
L=$ROOT/results/p13/case-validation/logs
FILE=src/io/case/JsonUtil.cpp
BACKUP=/tmp/p13_n1_backup.cpp
cd "$ROOT"
mkdir -p "$L"

cp "$FILE" "$BACKUP"
ORIGINAL=$(sha256sum "$FILE" | cut -d' ' -f1)
restore() {
  cp "$BACKUP" "$FILE"
  local now
  now=$(sha256sum "$FILE" | cut -d' ' -f1)
  [ "$now" = "$ORIGINAL" ] || { echo "RESTORE FAILED: $now != $ORIGINAL"; exit 99; }
}
trap restore EXIT

log=$L/N1_control.log
{ echo "P13-002 control N1 -- is the GUI's message scraping fragile?"
  echo "date:     $(date -u '+%Y-%m-%dT%H:%M:%SZ')"
  echo "tree:     UNCHANGED (this runs before any P13-002 production change)"
  echo "file:     $FILE"
  echo "sha256:   $ORIGINAL"
  echo ""; } > "$log"

# The GUI test that asserts the field is recovered. If the premise is right,
# rewording the message must make this test FAIL.
TEST=CaseEditingTest.ValidateDraftExtractsTheOffendingFieldName

echo "--- 1. the test passes on the unchanged tree ---" | tee -a "$log"
cmake --build build/debug -j20 > /tmp/p13_n1_build.log 2>&1 || {
  echo "  BUILD FAILED (build/debug is the GUI build)" | tee -a "$log"
  tail -5 /tmp/p13_n1_build.log | tee -a "$log"; exit 1; }
if ctest --test-dir build/debug -R "$TEST" > "$L/N1-before.log" 2>&1; then
  echo "  PASS -- the GUI recovers field=nx today" | tee -a "$log"
else
  echo "  the test does not pass on the unchanged tree; N1 cannot say anything" | tee -a "$log"
  exit 1
fi

echo "--- 2. reword the message, change nothing else ---" | tee -a "$log"
python3 - "$FILE" <<'PY'
import sys
path = sys.argv[1]
old = '"must satisfy "'
new = '"must be "'
text = open(path, encoding="utf-8").read()
if text.count(old) != 1:
    sys.exit("PATCH TARGET NOT UNIQUE (%d)" % text.count(old))
open(path, "w", encoding="utf-8", newline="\n").write(text.replace(old, new, 1))
PY
[ $? -eq 0 ] || { echo "  could not apply the reword" | tee -a "$log"; exit 1; }

cmake --build build/debug -j20 > /tmp/p13_n1_build.log 2>&1 || {
  echo "  BUILD FAILED after the reword" | tee -a "$log"; exit 1; }
if ctest --test-dir build/debug -R "$TEST" > "$L/N1-after.log" 2>&1; then
  echo "  the test STILL PASSES after rewording" | tee -a "$log"
  echo "" | tee -a "$log"
  echo "N1: PREMISE NOT SUPPORTED -- the scraping survived a message change," | tee -a "$log"
  echo "    so 'the GUI is fragile to wording' is not demonstrated." | tee -a "$log"
  verdict=1
else
  echo "  the test FAILS after rewording -- the GUI lost the field" | tee -a "$log"
  grep -m3 -E "Failure|Expected|Actual|Which is" "$L/N1-after.log" | sed 's/^/      /' | tee -a "$log"
  echo "" | tee -a "$log"
  echo "N1: PREMISE CONFIRMED -- a message reword silently breaks the GUI's" | tee -a "$log"
  echo "    field extraction. Carrying the diagnostic as data is worth doing." | tee -a "$log"
  verdict=0
fi

restore
trap - EXIT
echo "--- 3. restored ---" | tee -a "$log"
echo "  sha256 $(sha256sum "$FILE" | cut -d' ' -f1) IDENTICAL" | tee -a "$log"
cmake --build build/debug -j20 > /dev/null 2>&1
ctest --test-dir build/debug -R "$TEST" > /dev/null 2>&1 \
  && echo "  the test passes again: yes" | tee -a "$log" \
  || { echo "  THE TEST DOES NOT PASS AFTER RESTORE" | tee -a "$log"; exit 1; }
exit $verdict
