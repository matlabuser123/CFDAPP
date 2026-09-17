#!/usr/bin/env bash
# P12-ASAN-001: rebuild CFDMeshTests in the ASan+UBSan tree against the CURRENT sources and run
# MeshQualityReport.DisconnectedMeshIsFatal under CI's sanitizer settings.
# usage: asan_one.sh <log-name> [gtest filter]
# Fails closed if the build fails or the binary is older than the test source / library.
set -u
R=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
B=$R/build/asan
P=$R/results/p12-asan-001
FILTER=${2:-MeshQualityReport.DisconnectedMeshIsFatal}
mkdir -p "$P/logs"
LOG=$P/logs/$1
cd "$R" || exit 1
{
  echo "# P12-ASAN-001 $(date -u +%Y-%m-%dT%H:%M:%SZ)  git HEAD $(git rev-parse HEAD)"
  echo "# test source $(sha256sum tests/unit/mesh/test_mesh_quality_report.cpp)"
  echo "## configure (existing asan cache: CFDAPP_ENABLE_SANITIZERS=ON, Debug, Ninja)"
  cmake -S "$R" -B "$B" > "$P/logs/${1%.log}_configure.txt" 2>&1 || { echo "CONFIGURE FAILED"; exit 1; }
  echo "  OK"
  echo "## build CFDMeshTests"
  if cmake --build "$B" --target CFDMeshTests -j16 > "$P/logs/${1%.log}_build.txt" 2>&1; then
    echo "  OK ($(wc -l < "$P/logs/${1%.log}_build.txt") lines)"
  else
    echo "  BUILD FAILED -- fail closed"; tail -30 "$P/logs/${1%.log}_build.txt"; exit 1
  fi
  BIN=$(find "$B" -type f -name CFDMeshTests | head -1)
  [ "$BIN" -nt tests/unit/mesh/test_mesh_quality_report.cpp ] || { echo "STALE BINARY -- fail closed"; exit 1; }
  echo "  binary  $BIN"
  echo "  sha256  $(sha256sum "$BIN" | cut -d' ' -f1)  mtime $(stat -c %y "$BIN")"
  echo "  libcfdcore.a (asan) $(sha256sum "$B/src/libcfdcore.a" | cut -d' ' -f1)"
  echo "  -fsanitize flags: $(grep -o -- '-fsanitize=[a-z,]*' "$B/build.ninja" | sort -u | tr '\n' ' ')"
  echo
  echo "## run: $FILTER"
  echo "   ASAN_OPTIONS=detect_leaks=1:halt_on_error=0 UBSAN_OPTIONS=print_stacktrace=1:halt_on_error=0"
  ASAN_OPTIONS=detect_leaks=1:halt_on_error=0 UBSAN_OPTIONS=print_stacktrace=1:halt_on_error=0 \
    "$BIN" --gtest_filter="$FILTER" 2>&1
  echo "exit $?"
  echo "# end $(date -u +%Y-%m-%dT%H:%M:%SZ)"
} > "$LOG" 2>&1
grep -E 'ERROR: AddressSanitizer|SUMMARY|runtime error|^\[  (PASSED|FAILED) |exit |FAILED|STALE' "$LOG"
