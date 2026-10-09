#!/usr/bin/env bash
# P13-002, controls N2 and N3 -- each disables one half of this item, and the
# criterion it guards must fail.
#
#   N2  throwConfigError stops carrying the diagnostic (message unchanged)
#       -> S1 must fail: the unit tests that read the payload
#       -> S4 must fail: the GUI falls back to scraping
#   N3  the manifest site reverts to its old message and no diagnostic
#       -> M2/M2b must regress: the 60 vague rows come back
#
# Without these, "0 vague rows" and "the diagnostic is present" could both be
# true for reasons unrelated to the change.
#
# Safety: sha256 before, unique-string patch asserted to match, restore
# VERIFIED by this script rather than trusted to a trap -- an earlier control
# in this item left production source modified when its trap did not fire.
set -uo pipefail
ROOT=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
T=$ROOT/results/p13/final-qualification/2026-09-24/tools/case-validation
L=$ROOT/results/p13/final-qualification/2026-09-24/case_validation_logs
cd "$ROOT"

FILES=(src/io/case/JsonUtil.cpp src/io/CaseReader.cpp)
BACKUP=/tmp/p13_n23_backup
mkdir -p "$BACKUP"
declare -A SHA
for f in "${FILES[@]}"; do
  cp "$f" "$BACKUP/$(basename "$f")"
  SHA[$f]=$(sha256sum "$f" | cut -d' ' -f1)
done
restore_and_verify() {
  local ok=0
  for f in "${FILES[@]}"; do
    cp "$BACKUP/$(basename "$f")" "$f"
    local now
    now=$(sha256sum "$f" | cut -d' ' -f1)
    [ "$now" = "${SHA[$f]}" ] || { echo "RESTORE FAILED for $f"; ok=1; }
  done
  return $ok
}
trap 'restore_and_verify || exit 99' EXIT

log=$L/N2_N3_controls.log
{ echo "P13-002 controls N2 and N3"
  echo "date: $(date -u '+%Y-%m-%dT%H:%M:%SZ')"
  for f in "${FILES[@]}"; do echo "  ${SHA[$f]}  $f"; done
  echo ""; } > "$log"
say() { echo "$*" | tee -a "$log"; }
fail=0

patch_once() {  # patch_once <file> <old> <new>
  python3 - "$1" "$2" "$3" <<'PY'
import sys
path, old, new = sys.argv[1], sys.argv[2], sys.argv[3]
text = open(path, encoding="utf-8").read()
if text.count(old) != 1:
    sys.stderr.write("PATCH TARGET NOT UNIQUE (%d) in %s\n" % (text.count(old), path))
    sys.exit(1)
open(path, "w", encoding="utf-8", newline="\n").write(text.replace(old, new, 1))
PY
}

# --- N2: the diagnostic is not carried --------------------------------------
say "--- N2: throwConfigError stops carrying the diagnostic ---"
patch_once src/io/case/JsonUtil.cpp \
  'throw CaseConfigurationError(message.str(),' \
  'throw CaseConfigurationError(message.str());  // N2 CONTROL
  if (false) throw CaseConfigurationError(message.str(),' || { say "  patch failed"; exit 1; }
if cmake --build build/final -j20 > /tmp/p13_n23_build.log 2>&1; then
  if ctest --test-dir build/final -R CaseDiagnosticTest > "$L/N2-after.log" 2>&1; then
    say "  UNDETECTED -- the diagnostic tests still pass without the payload"
    fail=1
  else
    n=$(grep -cE "\(Failed\)" "$L/N2-after.log" || true)
    say "  DETECTED -- $n diagnostic tests fail without the payload"
  fi
else
  say "  BUILD FAILED"; tail -6 /tmp/p13_n23_build.log | tee -a "$log"; fail=1
fi
restore_and_verify || { say "  restore failed"; exit 99; }

# --- N3: the manifest site reverts ------------------------------------------
say "--- N3: the manifest reference reverts to its old message ---"
patch_once src/io/CaseReader.cpp \
    'throw IOError(manifestPath.string() + ": field \"" + std::string(referenceField) +' \
    'throw IOError("required " + std::string(referenceField) +
                  " configuration not found: " + candidate.string());
    if (false) throw IOError(manifestPath.string() + ": field \"" + std::string(referenceField) +' \
  || { say "  patch failed"; exit 1; }
if cmake --build build/final -j20 > /tmp/p13_n23_build.log 2>&1; then
  python3 "$T/mutation_matrix.py" "$ROOT/results/p13/case-validation/data/baseline_case" \
    "$ROOT/build/final/apps/cli/cfdapp" > "$L/N3-after.log" 2>&1
  v=$(grep -m1 "^rejected but vague" "$L/N3-after.log" | awk '{print $4}')
  w=$(grep -m1 "^rejected but weak" "$L/N3-after.log" | awk '{print $4}')
  # Only `weak` (M2b) can move: see acceptance_gate_A3.md. `vague` (the AND
  # rule) is 0 on both trees and was withdrawn as a criterion precisely
  # because this control showed it cannot fail. It is still reported, so a
  # future rejection that names neither file nor field would be visible.
  say "  with the manifest site reverted: weak=$w (expected 5), vague=$v (expected 0, vacuous)"
  if [ "${w:-0}" -gt 0 ]; then
    say "  DETECTED -- M2b regresses when the fix is removed, so it measures the fix"
  else
    say "  UNDETECTED -- M2b did not move, so it is not measuring the fix"
    fail=1
  fi
  if [ "${v:-0}" -ne 0 ]; then
    say "  NOTE -- vague moved to $v; A3 says it cannot. Re-derive the baseline."
    fail=1
  fi
else
  say "  BUILD FAILED"; tail -6 /tmp/p13_n23_build.log | tee -a "$log"; fail=1
fi

# --- restore and prove -------------------------------------------------------
trap - EXIT
say "--- restored ---"
restore_and_verify || { say "  RESTORE FAILED"; exit 99; }
for f in "${FILES[@]}"; do
  say "  $(sha256sum "$f" | cut -d' ' -f1)  $f  IDENTICAL"
done
cmake --build build/final -j20 > /dev/null 2>&1
ctest --test-dir build/final -R CaseDiagnosticTest > /dev/null 2>&1 \
  && say "  the diagnostic tests pass again: yes" \
  || { say "  THEY DO NOT PASS AFTER RESTORE"; fail=1; }

say ""
[ $fail -eq 0 ] && say "N2/N3: BOTH DETECTED" || say "N2/N3: FAILED"
exit $fail
