#!/usr/bin/env bash
# P13-001 gate, group D controls 4, 5 and 6.
#
# C6 (a different mesh with the same cell and face counts), C9 (a different
# case) and C11/C12 (a non-finite value) all pass today. That alone does not
# prove the NAMED check is what catches them -- something else might be. Each
# control here disables exactly one check in production, rebuilds, and
# requires the row it guards to FAIL.
#
# Safety, because this edits production source:
#   * the original file is copied and its sha256 recorded BEFORE any edit;
#   * the edit is applied by an exact, unique string replacement that is
#     asserted to have matched;
#   * the file is restored and its sha256 re-checked against the original,
#     and a mismatch is a loud failure of this script;
#   * a trap restores on any exit path, including an interrupt.
#
# Nothing here is evidence of the implementation working. It is evidence that
# the CRITERIA can fail.
set -uo pipefail
ROOT=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
T=$ROOT/results/p13/restart/tools
L=$ROOT/results/p13/restart/logs
BACKUP=/tmp/p13_d456_backup
cd "$ROOT"
mkdir -p "$BACKUP"

FILES=(src/pressure_velocity/SIMPLECheckpoint.cpp src/io/CheckpointReader.cpp)
declare -A ORIGINAL_SHA
for f in "${FILES[@]}"; do
  cp "$f" "$BACKUP/$(basename "$f")"
  ORIGINAL_SHA[$f]=$(sha256sum "$f" | cut -d' ' -f1)
done

restore() {
  for f in "${FILES[@]}"; do
    cp "$BACKUP/$(basename "$f")" "$f"
    local now
    now=$(sha256sum "$f" | cut -d' ' -f1)
    if [ "$now" != "${ORIGINAL_SHA[$f]}" ]; then
      echo "RESTORE FAILED for $f -- $now != ${ORIGINAL_SHA[$f]}"
      exit 99
    fi
  done
}
trap restore EXIT

fail=0
log=$L/D456_controls.log
{ echo "P13-001 group D controls 4/5/6 -- each disables one production check"
  echo "date: $(date -u '+%Y-%m-%dT%H:%M:%SZ')"
  echo "originals:"
  for f in "${FILES[@]}"; do echo "  ${ORIGINAL_SHA[$f]}  $f"; done
  echo ""; } > "$log"

# patch <file> <exact-unique-old> <new>
patch_once() {
  python3 - "$1" "$2" "$3" <<'PY'
import sys
path, old, new = sys.argv[1], sys.argv[2], sys.argv[3]
text = open(path, encoding="utf-8").read()
if text.count(old) != 1:
    sys.stderr.write("PATCH TARGET NOT UNIQUE (%d matches) in %s\n" % (text.count(old), path))
    sys.exit(1)
open(path, "w", encoding="utf-8", newline="\n").write(text.replace(old, new, 1))
PY
}

# run_control <id> <row-substring-that-must-now-fail> <description>
run_control() {
  local id=$1 row=$2 desc=$3
  echo "--- $id: $desc ---" | tee -a "$log"
  if ! cmake --build build/final -j20 > /tmp/p13_d456_build.log 2>&1; then
    echo "  BUILD FAILED" | tee -a "$log"; tail -10 /tmp/p13_d456_build.log | tee -a "$log"
    fail=1; return
  fi
  bash "$T/run.sh" negative > /dev/null 2>&1
  local rc=$?
  cp "$L/negative.log" "$L/D456-$id.log"
  if [ $rc -eq 0 ]; then
    echo "  UNDETECTED -- the harness still passed with the check disabled;" | tee -a "$log"
    echo "  the criterion is vacuous" | tee -a "$log"
    fail=1
    return
  fi
  if grep -q "FAIL $row" "$L/D456-$id.log"; then
    echo "  DETECTED -- '$row' now fails, so that row is guarded by this check" | tee -a "$log"
    grep -m2 "FAIL $row" "$L/D456-$id.log" | sed 's/^/    /' | tee -a "$log"
  else
    echo "  the harness failed, but NOT on '$row' -- the control did not isolate" | tee -a "$log"
    grep -m3 "  FAIL" "$L/D456-$id.log" | sed 's/^/    /' | tee -a "$log"
    fail=1
  fi
}

# --- D4: remove the mesh-fingerprint comparison ----------------------------
patch_once src/pressure_velocity/SIMPLECheckpoint.cpp \
  'if (checkpoint.meshFingerprint != fingerprint) {' \
  'if (false && checkpoint.meshFingerprint != fingerprint) {  // D4 CONTROL' || exit 1
run_control D4 "C6" "the mesh-fingerprint comparison is disabled"
restore

# --- D5: remove the case-fingerprint comparison ----------------------------
patch_once src/io/CheckpointReader.cpp \
  'if (checkpoint.caseFingerprint != caseFingerprint) {' \
  'if (false && checkpoint.caseFingerprint != caseFingerprint) {  // D5 CONTROL' || exit 1
run_control D5 "C9" "the case-fingerprint comparison is disabled"
restore

# --- D6: remove the finiteness checks --------------------------------------
# The reader's own JSON parse rejects a literal NaN before validation is
# reached, so C11/C12 are guarded by readJsonFile rather than by
# validateSIMPLECheckpoint. The check that validateSIMPLECheckpoint DOES guard
# is a non-finite value arriving through the in-memory API, which is row
# "C-zero"'s neighbour: RejectsInconsistentOrNonFiniteContent. Disabling the
# pressure finiteness loop must break it.
patch_once src/pressure_velocity/SIMPLECheckpoint.cpp \
  'if (!std::isfinite(checkpoint.pressure[i])) {' \
  'if (false && !std::isfinite(checkpoint.pressure[i])) {  // D6 CONTROL' || exit 1
echo "--- D6: the pressure finiteness check is disabled ---" | tee -a "$log"
if cmake --build build/final -j20 > /tmp/p13_d456_build.log 2>&1; then
  ctest --test-dir build/final -R 'SIMPLECheckpointTest.RejectsInconsistentOrNonFiniteContent' \
    > "$L/D456-D6.log" 2>&1
  if [ $? -ne 0 ]; then
    echo "  DETECTED -- RejectsInconsistentOrNonFiniteContent now fails" | tee -a "$log"
  else
    echo "  UNDETECTED -- the test still passes with the finiteness check disabled" | tee -a "$log"
    fail=1
  fi
else
  echo "  BUILD FAILED" | tee -a "$log"; fail=1
fi
restore

# --- prove the tree is exactly as it was, and that it still passes ----------
echo "" | tee -a "$log"
echo "--- restored ---" | tee -a "$log"
for f in "${FILES[@]}"; do
  now=$(sha256sum "$f" | cut -d' ' -f1)
  printf '  %s  %s  %s\n' "$now" "$f" \
    "$([ "$now" = "${ORIGINAL_SHA[$f]}" ] && echo IDENTICAL || echo CHANGED)" | tee -a "$log"
  [ "$now" = "${ORIGINAL_SHA[$f]}" ] || fail=1
done
cmake --build build/final -j20 > /dev/null 2>&1 || fail=1
bash "$T/run.sh" negative > /dev/null 2>&1 && echo "  A/C/E passes again: yes" | tee -a "$log" \
  || { echo "  A/C/E DOES NOT PASS AFTER RESTORE" | tee -a "$log"; fail=1; }

echo "" | tee -a "$log"
[ $fail -eq 0 ] && echo "D4/D5/D6: ALL DETECTED" | tee -a "$log" \
                || echo "D4/D5/D6: FAILED" | tee -a "$log"
exit $fail
