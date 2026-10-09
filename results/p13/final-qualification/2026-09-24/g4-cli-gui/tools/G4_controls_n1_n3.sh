#!/usr/bin/env bash
# P13 FINAL QUALIFICATION -- G4: the frozen P13-003 controls N1 and N3.
#
# HARNESS AMENDMENT, recorded rather than silent. Two changes from
# results/p13/cli-gui/tools/controls.sh, which is preserved unmodified:
#
#  1. N1's PATCH TARGET. The frozen script perturbs
#         EXPECT_EQ(guiMetadata, readFile(cliRun.exportSummary->metadataPath));
#     That line no longer exists. The F4 provenance repair gave each arm its own
#     export snapshot, so E8 is now a comparison of two snapshots:
#         EXPECT_EQ(gui.metadata, cli.metadata);
#     N1's MEANING is unchanged -- perturb one exported value in the GUI arm and
#     E8 must fail. Only the line it lands on moved. A control whose patch no
#     longer applies silently reports nothing, which is why this is not left to
#     chance.
#
#  2. LOG DESTINATION. The frozen script writes into
#     results/p13/cli-gui/logs/, which is the committed P13-003 evidence, and
#     overwrites the 2026-09-21 baseline every time it runs. This copy writes
#     under the qualification's own tree instead.
#
# Everything else -- the criteria, the detection logic, the restore discipline --
# is the frozen script's.
set -uo pipefail
ROOT=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
L=$ROOT/results/p13/final-qualification/2026-09-24/g4-cli-gui/logs
cd "$ROOT"
mkdir -p "$L"

FILES=(apps/gui/tests/test_cli_gui_equivalence.cpp apps/gui/SimulationController.cpp)
BACKUP=$(mktemp -d /tmp/p13_g4_n1n3.XXXXXX)
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
    [ "$now" = "${SHA[$f]}" ] || { echo "RESTORE FAILED for $f"; bad=1; }
  done
  return $bad
}
trap 'echo "TRAP FIRED"; restore_and_verify || exit 99' EXIT

log=$L/G4_controls_n1_n3.log
{ echo "P13 G4 -- frozen controls N1 and N3"
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
n = text.count(old)
if n != 1:
    sys.stderr.write("PATCH TARGET NOT UNIQUE (%d) in %s\n" % (n, path))
    sys.exit(1)
open(path, "w", encoding="utf-8", newline="\n").write(text.replace(old, new, 1))
PY
}

run_gui_tests() {
  cmake --build build/debug -j20 > /tmp/p13_g4_n1n3_build.log 2>&1 || return 2
  ctest --test-dir build/debug -R "$1" > "$2" 2>&1
}

report() {  # report <criterion> <detection> <rc>
  say "  PLANTED DEFECT     yes"
  say "  CRITERION          $1"
  say "  DETECTION          $2"
  if restore_and_verify; then say "  RESTORE            VERIFIED by SHA-256"
  else say "  RESTORE            FAILED"; fail=1; fi
  say "  RETURN CODE        $3"
  say ""
}

# --- N1: perturb one exported value in the GUI arm --------------------------
say "=== N1 -- one byte is added to the GUI arm's exported metadata ==="
patch_once apps/gui/tests/test_cli_gui_equivalence.cpp \
  '  EXPECT_EQ(gui.metadata, cli.metadata);' \
  '  EXPECT_EQ(gui.metadata + " ", cli.metadata);  // N1 CONTROL' \
  || { say "  PATCH FAILED"; exit 1; }
run_gui_tests 'CliGuiEquivalenceTest' "$L/G4_N1-after.log"
rc=$?
if [ $rc -eq 2 ]; then say "  BUILD FAILED"; fail=1; restore_and_verify
elif [ $rc -ne 0 ]; then
  n=$(grep -cE '\(Failed\)' "$L/G4_N1-after.log" || true)
  report "FAILED as expected ($n tests)" "DETECTED" "nonzero ($rc), expected"
else
  say "  CRITERION          PASSED -- E8 does not compare the exported bytes"
  say "  DETECTION          UNDETECTED"
  restore_and_verify && say "  RESTORE            VERIFIED by SHA-256" || fail=1
  say "  RETURN CODE        0 -- NOT expected"; say ""; fail=1
fi

# --- N3: remove the GUI's checkpoint plumbing -------------------------------
say "=== N3 -- the GUI stops forwarding its checkpoint options ==="
patch_once apps/gui/SimulationController.cpp \
  '    ProjectRunOptions options = checkpointOptions;' \
  '    ProjectRunOptions options;  // N3 CONTROL -- the options are dropped' \
  || { say "  PATCH FAILED"; exit 1; }
run_gui_tests 'CliGuiCheckpointTest' "$L/G4_N3-after.log"
rc=$?
if [ $rc -eq 2 ]; then say "  BUILD FAILED"; fail=1; restore_and_verify
elif [ $rc -ne 0 ]; then
  n=$(grep -cE '\(Failed\)' "$L/G4_N3-after.log" || true)
  report "FAILED as expected ($n tests)" "DETECTED" "nonzero ($rc), expected"
else
  say "  CRITERION          PASSED -- the checkpoint tests pass without the plumbing"
  say "  DETECTION          UNDETECTED"
  restore_and_verify && say "  RESTORE            VERIFIED by SHA-256" || fail=1
  say "  RETURN CODE        0 -- NOT expected"; say ""; fail=1
fi

trap - EXIT
say "=== restored ==="
restore_and_verify || { say "  RESTORE FAILED"; exit 99; }
for f in "${FILES[@]}"; do
  now=$(sha256sum "$f" | cut -d' ' -f1)
  [ "$now" = "${SHA[$f]}" ] && say "  $now  $f  IDENTICAL" \
                           || { say "  $now  $f  DIFFERS"; fail=1; }
done
cmake --build build/debug -j20 > /dev/null 2>&1
ctest --test-dir build/debug -R 'CliGui' > /dev/null 2>&1 \
  && say "  all CliGui tests pass again: yes" \
  || { say "  THEY DO NOT PASS AFTER RESTORE"; fail=1; }
rm -rf "$BACKUP"
say ""
[ $fail -eq 0 ] && say "N1/N3: BOTH DETECTED" || say "N1/N3: FAILED"
exit $fail
