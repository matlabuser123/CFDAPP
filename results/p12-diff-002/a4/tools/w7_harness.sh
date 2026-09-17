#!/usr/bin/env bash
# P12-DIFF-002 A4-1: trustworthy verification harness.
#
# The defect it replaces: results/p12-diff-002/tools/run_w7.sh resolved each suite's executable and
# rebuilt it ONLY IF THE BINARY WAS MISSING -- it inferred freshness from mere existence. A1's and
# A2's W7 runs therefore executed stale binaries, and A2's reported 970/966/4 understated the
# failures (CFDThermalTests reported 92/92 where a forced rebuild gives 79/92).
#
# The correction: configure -> build every requested target -> VERIFY the build succeeded -> only
# then execute, recording the provenance of what actually ran. The harness FAILS CLOSED: a target
# whose build step returns non-zero, or whose executable cannot be located after a successful build,
# is reported as BUILD FAILED and is never run, and the harness exits non-zero.
#
# Usage: w7_harness.sh <log-path> <target> [<target> ...]
set -u
R=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
B=$R/build/release
LOG=$1; shift
TARGETS="$*"
CONFIGURE_CMD="cmake -S $R -B $B"
cd $R

locate() { find $B -type f -name "$1" -perm -u+x 2>/dev/null | head -1; }
stamp() { date -u -r "$1" +%Y-%m-%dT%H:%M:%SZ 2>/dev/null || echo "(no mtime)"; }

FAILED_BUILDS=0
{
  echo "# P12-DIFF-002 A4 W7 HARNESS; $(date -u +%Y-%m-%dT%H:%M:%SZ)"
  echo "# source HEAD            $(git rev-parse HEAD)"
  echo "# configure command      $CONFIGURE_CMD"
  echo "# build command          cmake --build $B --target <target> -j\$(nproc)"
  echo "# targets                $TARGETS"
  echo
  echo "## configure"
  if $CONFIGURE_CMD > /dev/null 2>&1; then
    echo "  cmake reconfigure OK"
  else
    echo "  cmake reconfigure FAILED"
  fi
  echo
  echo "## build (every target is built before anything runs; failure is fatal for that target)"
  for t in $TARGETS; do
    BEFORE=$(locate "$t"); HB="(absent)"
    [ -n "$BEFORE" ] && HB=$(sha256sum "$BEFORE" | cut -c1-16)
    if cmake --build $B --target "$t" -j"$(nproc)" > /dev/null 2>&1; then
      BIN=$(locate "$t")
      if [ -z "$BIN" ]; then
        echo "  $t BUILD OK BUT EXECUTABLE NOT FOUND -> FAIL CLOSED"
        FAILED_BUILDS=$((FAILED_BUILDS + 1))
      else
        echo "  $t built OK | binary $(sha256sum "$BIN" | cut -c1-16) (was $HB) | mtime $(stamp "$BIN")"
      fi
    else
      echo "  $t BUILD FAILED -> FAIL CLOSED, not executed"
      FAILED_BUILDS=$((FAILED_BUILDS + 1))
    fi
  done
  echo
  echo "## provenance of what runs"
  echo "  production library     $(sha256sum $B/src/libcfdcore.a | cut -c1-16)  mtime $(stamp $B/src/libcfdcore.a)"
  echo
  echo "## execute (freshly built binaries only)"
  for t in $TARGETS; do
    BIN=$(locate "$t")
    if [ -z "$BIN" ]; then
      echo "$t: NOT RUN (build failed)"
      continue
    fi
    echo "  test command           $BIN"
    OUT=$("$BIN" 2>&1)
    RUN=$(echo "$OUT" | grep -oE '^\[==========\] [0-9]+ tests from' | grep -oE '[0-9]+' | head -1)
    PASS=$(echo "$OUT" | grep -oE '^\[  PASSED  \] [0-9]+' | grep -oE '[0-9]+' | head -1)
    FAIL=$(echo "$OUT" | grep -cE '^\[  FAILED  \] [A-Za-z0-9_]+\.[A-Za-z0-9_]+ \(')
    DISABLED=$(echo "$OUT" | grep -oE 'YOU HAVE [0-9]+ DISABLED TEST' | grep -oE '[0-9]+' | head -1)
    echo "$t: binary $(sha256sum "$BIN" | cut -c1-16) | run ${RUN:-0} pass ${PASS:-0} fail ${FAIL:-0} disabled ${DISABLED:-0}"
    echo "$OUT" | grep -E '^\[  FAILED  \] [A-Za-z0-9_]+\.[A-Za-z0-9_]+$' | sed 's/^/    FAILED: /' | sort -u
  done
  echo
  echo "## harness verdict"
  if [ "$FAILED_BUILDS" -eq 0 ]; then
    echo "  all requested targets rebuilt and executed"
  else
    echo "  $FAILED_BUILDS target(s) failed to build -- HARNESS FAILED CLOSED"
  fi
} > "$LOG" 2>&1
cat "$LOG"
[ "$FAILED_BUILDS" -eq 0 ]
