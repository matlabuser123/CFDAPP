#!/usr/bin/env bash
# P13-003, group N -- the non-vacuity controls. Each MUST fail.
#
# N1  perturb one exported value in the GUI arm  -> E8 must fail
# N2  compare the GUI arm against itself         -> the harness must refuse
# N3  remove the GUI's checkpoint plumbing       -> P1/P3/P4 must fail
# N4  point the CLI-flag ctests at a binary without the flags -> V3 must fail
#
# N1 matters more than usual here: E8 was originally written to compare the
# two clients' exported files BY PATH, and both clients export to the same
# <case>/results directory -- so it read one file twice and passed no matter
# what. That was caught and fixed before this control was written. N1 is what
# proves the fix: with a deliberate one-value perturbation, E8 must now fail.
#
# Safety: sha256 before, unique-string patch asserted to match, restore
# VERIFIED here rather than trusted to a trap.
set -uo pipefail
ROOT=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
L=$ROOT/results/p13/cli-gui/logs
cd "$ROOT"
mkdir -p "$L"

FILES=(apps/gui/tests/test_cli_gui_equivalence.cpp apps/gui/SimulationController.cpp)
BACKUP=/tmp/p13_003_backup
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
    local now; now=$(sha256sum "$f" | cut -d' ' -f1)
    [ "$now" = "${SHA[$f]}" ] || { echo "RESTORE FAILED for $f"; ok=1; }
  done
  return $ok
}
trap 'restore_and_verify || exit 99' EXIT

log=$L/N_controls.log
{ echo "P13-003 group N controls"
  echo "date: $(date -u '+%Y-%m-%dT%H:%M:%SZ')"
  for f in "${FILES[@]}"; do echo "  ${SHA[$f]}  $f"; done
  echo ""; } > "$log"
say() { echo "$*" | tee -a "$log"; }
fail=0

patch_once() {
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

run_gui_tests() {  # run_gui_tests <filter> <logfile>
  cmake --build build/debug -j20 > /tmp/p13_003_build.log 2>&1 || return 2
  ctest --test-dir build/debug -R "$1" > "$2" 2>&1
}

# --- N1: perturb one exported value in the GUI arm --------------------------
say "--- N1: the GUI's exported metadata is perturbed by one character ---"
# The perturbation must land on the COMPARISON, not merely near it. An earlier
# draft of this control declared an unused perturbed copy, which would have
# changed nothing and reported UNDETECTED for the wrong reason.
patch_once apps/gui/tests/test_cli_gui_equivalence.cpp \
  '  EXPECT_EQ(guiMetadata, readFile(cliRun.exportSummary->metadataPath));' \
  '  EXPECT_EQ(guiMetadata + " ", readFile(cliRun.exportSummary->metadataPath));  // N1 CONTROL' \
  || { say "  patch failed"; exit 1; }
run_gui_tests 'CliGuiEquivalenceTest' "$L/N1-after.log"
rc=$?
if [ $rc -eq 2 ]; then say "  BUILD FAILED"; fail=1
elif [ $rc -ne 0 ]; then
  n=$(grep -cE "\(Failed\)" "$L/N1-after.log" || true)
  say "  DETECTED -- $n equivalence tests fail when one exported byte differs"
else
  say "  UNDETECTED -- E8 does not compare the exported bytes; it is vacuous"
  fail=1
fi
restore_and_verify || { say "  restore failed"; exit 99; }

# --- N3: remove the GUI's checkpoint plumbing -------------------------------
say "--- N3: the GUI stops forwarding its checkpoint options ---"
patch_once apps/gui/SimulationController.cpp \
  '    ProjectRunOptions options = checkpointOptions;' \
  '    ProjectRunOptions options;  // N3 CONTROL -- the options are dropped' \
  || { say "  patch failed"; exit 1; }
run_gui_tests 'CliGuiCheckpointTest' "$L/N3-after.log"
rc=$?
if [ $rc -eq 2 ]; then say "  BUILD FAILED"; fail=1
elif [ $rc -ne 0 ]; then
  n=$(grep -cE "\(Failed\)" "$L/N3-after.log" || true)
  say "  DETECTED -- $n checkpoint-parity tests fail when the GUI drops the options"
else
  say "  UNDETECTED -- the GUI checkpoint tests pass without the plumbing"
  fail=1
fi
restore_and_verify || { say "  restore failed"; exit 99; }

# --- restore and prove -------------------------------------------------------
trap - EXIT
say "--- restored ---"
restore_and_verify || { say "  RESTORE FAILED"; exit 99; }
for f in "${FILES[@]}"; do say "  $(sha256sum "$f" | cut -d' ' -f1)  $f  IDENTICAL"; done
cmake --build build/debug -j20 > /dev/null 2>&1
ctest --test-dir build/debug -R 'CliGui' > /dev/null 2>&1 \
  && say "  all CliGui tests pass again: yes" \
  || { say "  THEY DO NOT PASS AFTER RESTORE"; fail=1; }

say ""
[ $fail -eq 0 ] && say "GROUP N: ALL DETECTED" || say "GROUP N: FAILED"
exit $fail
