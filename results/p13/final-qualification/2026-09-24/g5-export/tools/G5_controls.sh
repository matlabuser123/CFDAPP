#!/usr/bin/env bash
# P13 FINAL QUALIFICATION -- G5, group N. The frozen definitions from
# results/p13/export/acceptance_gate.md, used as written:
#
#   N1  Remove the write-failure check from one writer   -> W1 for that writer
#   N2  Make one added metadata field vary per run       -> M6
#   N3  Point the /dev/full probe at a normal file       -> must report 0
#
# N1 had a log at P13-004 (results/p13/export/logs/N1-after.log). N2 appears
# ONLY in acceptance_gate.md -- no script, no log, nowhere else in
# results/p13/export/ -- so it is implemented here for the first time. N3 was
# discharged at P13-004 by the permanent test AHealthyWriteStillSucceeds, which
# covers one writer; here the same instrument that measures W1 is pointed at a
# real directory, so all six writers are covered by it.
#
# Every mutating control: sha256 before, patch target asserted UNIQUE, restore
# VERIFIED by sha256 here rather than trusted to the trap, trap kept as a
# backstop that exits 99 if it ever has to act.
set -uo pipefail
ROOT=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
G5=$ROOT/results/p13/final-qualification/2026-09-24/g5-export
L=$G5/logs
JSON=build/debug/_deps/nlohmann_json-src/include
CLI=build/debug/apps/cli/cfdapp
cd "$ROOT"
mkdir -p "$L"

FILES=(src/io/CSVWriter.cpp src/io/JSONWriter.cpp)
BACKUP=$(mktemp -d /tmp/p13_g5_controls.XXXXXX)
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
trap 'echo "TRAP FIRED -- restoring"; restore_and_verify || exit 99' EXIT

log=$L/G5_controls.log
{ echo "P13 G5 -- controls N1, N2, N3"
  echo "date: $(date -u '+%Y-%m-%dT%H:%M:%SZ')"
  echo "baseline sha256:"
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
    sys.stderr.write("PATCH TARGET NOT UNIQUE (%d occurrences) in %s\n" % (n, path))
    sys.exit(1)
open(path, "w", encoding="utf-8", newline="\n").write(text.replace(old, new, 1))
PY
}

build_lib() { cmake --build build/debug -j20 > /tmp/p13_g5_build.log 2>&1; }
build_probe() {
  g++ -std=c++20 -O0 -g -I include -I "$JSON" \
      "$G5/tools/all_writers_probe.cpp" build/debug/src/libcfdcore.a -o /tmp/g5_probe 2>/dev/null
}

restore_report() {
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
}

# ------------------------------------------------------------------ N1 -------
say "=== N1 -- the write-failure check is removed from CSVWriter::writeResiduals ==="
# `detail::requireDeterministicOutputWritten(out, path);` occurs three times in
# this file (writeFields, writeFields3D, writeResiduals), so the anchor includes
# the globalMassImbalance line, which is unique to writeResiduals. patch_once
# asserts the uniqueness rather than assuming it.
patch_once src/io/CSVWriter.cpp \
  "        << result.globalMassImbalance << '\\n';
  }
  // P13-004: opening the file proved nothing about whether the bytes
  // reached it. See DeterministicOstream.hpp.
  detail::requireDeterministicOutputWritten(out, path);" \
  "        << result.globalMassImbalance << '\\n';
  }
  // N1 CONTROL -- the write-failure check is removed" \
  || { say "  PATCH FAILED -- target not unique"; exit 1; }
if ! build_lib || ! build_probe; then
  say "  BUILD FAILED"; fail=1; restore_and_verify; say ""
else
  /tmp/g5_probe > "$L/G5_N1-after_probe.log" 2>&1
  silent=$?
  ctest --test-dir build/debug -R 'ExportWriteFailure' --output-on-failure \
        > "$L/G5_N1-after_tests.log" 2>&1
  trc=$?
  nf=$(grep -cE '\(Failed\)' "$L/G5_N1-after_tests.log" || true)
  say "  PLANTED DEFECT     yes"
  if [ "$silent" -ge 1 ] || [ $trc -ne 0 ]; then
    say "  CRITERION          FAILED as expected"
    say "                     probe: $silent writer(s) silently succeeded (baseline 0)"
    say "                     tests: $nf ExportWriteFailure test(s) failed"
    say "  DETECTION          DETECTED"
    restore_report
    say "  RETURN CODE        probe=$silent ctest=$trc, both expected non-clean"
  else
    say "  CRITERION          PASSED -- removing the check changed nothing"
    say "  DETECTION          UNDETECTED -- W1 is vacuous for this writer"
    restore_report
    say "  RETURN CODE        probe=0 ctest=0 -- NOT expected"
    fail=1
  fi
  say ""
fi

# ------------------------------------------------------------------ N2 -------
say "=== N2 -- an added metadata field is made to vary per run ==="
say "  frozen definition: 'Make one added metadata field vary per run (e.g. a"
say "  timestamp)' -> M6 (metadata.json byte-identical across runs) must fail."
say "  Never implemented before this gate."
patch_once src/io/JSONWriter.cpp '#include <cmath>' '#include <chrono>
#include <cmath>' || { say "  PATCH A FAILED"; exit 1; }
patch_once src/io/JSONWriter.cpp \
  '  doc["version"] = cfd::core::versionString();' \
  '  doc["version"] = cfd::core::versionString();
  // N2 CONTROL -- a per-run-varying field, exactly what M6 forbids
  doc["n2_control_timestamp"] =
      std::chrono::steady_clock::now().time_since_epoch().count();' \
  || { say "  PATCH B FAILED"; exit 1; }
if ! build_lib; then
  say "  BUILD FAILED"; fail=1; restore_and_verify; say ""
else
  W=$(mktemp -d /tmp/g5_n2.XXXXXX)
  cp -r tests/data/cases/valid_cavity "$W/a"; cp -r "$W/a" "$W/b"
  "$CLI" --case "$W/a" > /dev/null 2>&1
  "$CLI" --case "$W/b" > /dev/null 2>&1
  ha=$(sha256sum "$W/a/results/metadata.json" 2>/dev/null | cut -d' ' -f1)
  hb=$(sha256sum "$W/b/results/metadata.json" 2>/dev/null | cut -d' ' -f1)
  say "  PLANTED DEFECT     yes"
  say "  run A metadata.json sha256 ${ha:0:24}"
  say "  run B metadata.json sha256 ${hb:0:24}"
  if [ -n "$ha" ] && [ "$ha" != "$hb" ]; then
    say "  CRITERION          FAILED as expected -- M6 broken, the two runs differ"
    say "  DETECTION          DETECTED"
    restore_report
    say "  RETURN CODE        n/a (a byte comparison, not an exit code)"
  else
    say "  CRITERION          PASSED -- a per-run field did NOT break byte-identity"
    say "  DETECTION          UNDETECTED -- M6 is vacuous"
    restore_report
    say "  RETURN CODE        n/a"
    fail=1
  fi
  rm -rf "$W"
  say ""
fi

# ------------------------------------------------------------------ N3 -------
say "=== N3 -- the same probe pointed at a normal directory ==="
say "  frozen definition: 'Point the /dev/full probe at a normal file' -> it must"
say "  report 0, i.e. it must NOT fire on a healthy path. A check that threw"
say "  everywhere would satisfy W1 while breaking every real export."
if ! build_lib || ! build_probe; then
  say "  BUILD FAILED"; fail=1
else
  H=$(mktemp -d /tmp/g5_n3.XXXXXX)
  /tmp/g5_probe "$H" > "$L/G5_N3-after.log" 2>&1
  threw=$?
  wrote=$(find "$H" -type f -size +0c | wc -l)
  say "  PLANTED DEFECT     n/a (this control guards the INSTRUMENT, not the code)"
  if [ "$threw" -eq 0 ] && [ "$wrote" -ge 6 ]; then
    say "  CRITERION          0 writers threw on a healthy path, $wrote non-empty files written"
    say "  DETECTION          INSTRUMENT SOUND -- the probe does not fire on healthy paths"
    say "  RESTORE            n/a (no source was mutated)"
    say "  RETURN CODE        0, expected"
  else
    say "  CRITERION          $threw writer(s) threw on a healthy path, $wrote non-empty files"
    say "  DETECTION          INSTRUMENT UNSOUND -- W1 may be passing for the wrong reason"
    say "  RESTORE            n/a"
    say "  RETURN CODE        $threw -- NOT expected"
    fail=1
  fi
  rm -rf "$H"
  say ""
fi

# -------------------------------------------------------------- closeout -----
trap - EXIT
say "=== restored, and the tree proved back at baseline ==="
restore_and_verify || { say "  RESTORE FAILED"; exit 99; }
for f in "${FILES[@]}"; do
  now=$(sha256sum "$f" | cut -d' ' -f1)
  [ "$now" = "${SHA[$f]}" ] && say "  $now  $f  IDENTICAL" \
                           || { say "  $now  $f  DIFFERS"; fail=1; }
done
say "  rebuilding at baseline and re-running the export suites"
build_lib || { say "  BASELINE REBUILD FAILED"; exit 1; }
if ctest --test-dir build/debug -R 'Export|CsvWriter|JsonWriter|VtkWriter|ResultExport' \
     > "$L/G5_after_controls.log" 2>&1; then
  say "  all export tests pass again: yes"
else
  say "  THEY DO NOT PASS AFTER RESTORE"; fail=1
fi
build_probe && /tmp/g5_probe > "$L/G5_all_writers_probe.log" 2>&1
say "  W1 re-measured at baseline: $? writer(s) silently succeeded (0 required)"
rm -rf "$BACKUP"
say ""
[ $fail -eq 0 ] && say "G5 GROUP N: ALL DETECTED / SOUND, ALL RESTORED" \
                || say "G5 GROUP N: SEE ABOVE"
exit $fail
