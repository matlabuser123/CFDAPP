#!/usr/bin/env bash
# UF-001.14: fresh authoritative W7, on the A4 rule -- configure, build EVERYTHING, verify, and
# only then execute; fail closed on a build failure. Same shape as
# results/p12-diff-002/a4/tools/full_inventory.sh so the counts are directly comparable.
#
# Executable-bit guard (the issue that made UC-001's first Step 10 pass skip a suite): freshly
# linked binaries on the Windows mount can come out without the x bit, which makes ctest report
# "Unable to find executable". Every test binary is chmod'ed after the build and before ctest, and
# the count of binaries lacking the bit is recorded rather than passed over.
set -u
R=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
B=$R/build/release
P=$R/results/p12-diff-002/uf-001
LOG=$P/logs/17_W7_fresh.log
mkdir -p "$P/logs"
cd "$R" || exit 1
{
  echo "# UF-001.14 fresh authoritative W7; $(date -u +%Y-%m-%dT%H:%M:%SZ)"
  echo "# git HEAD $(git rev-parse HEAD 2>/dev/null || echo '(not a git repo)')"
  cmake -S "$R" -B "$B" > /dev/null 2>&1 && echo "# configure OK" || echo "# configure FAILED"
  if cmake --build "$B" -j"$(nproc)" > "$P/logs/17_W7_build.log" 2>&1; then
    echo "# BUILD OK ($(wc -l < "$P/logs/17_W7_build.log") lines)"
  else
    echo "# BUILD FAILED -- fail closed, nothing executed"
    tail -30 "$P/logs/17_W7_build.log"
    exit 1
  fi
  echo "# libcfdcore.a $(sha256sum "$B/src/libcfdcore.a" | cut -d' ' -f1)"
  NOEXEC=$(find "$B" -type f -name "CFD*Tests" ! -perm -u+x 2>/dev/null | wc -l)
  echo "# test binaries lacking the executable bit before chmod: $NOEXEC"
  find "$B" -type f -name "CFD*Tests" -exec chmod +x {} + 2>/dev/null
  echo "# after chmod: $(find "$B" -type f -name 'CFD*Tests' ! -perm -u+x 2>/dev/null | wc -l)"
  echo "# run: ctest --test-dir $B -j\$(nproc)"
  echo
} > "$LOG" 2>&1
ctest --test-dir "$B" -j"$(nproc)" > "$P/logs/17_W7_ctest_raw.log" 2>&1
{
  echo "## ctest summary"
  grep -E '^ *[0-9]+% tests passed|^Total Test time|tests failed out of' \
    "$P/logs/17_W7_ctest_raw.log"
  echo
  echo "## every failing ctest entry"
  grep -E '^\s+[0-9]+ - ' "$P/logs/17_W7_ctest_raw.log" | grep -v '(Disabled)' | sed 's/^/  /'
  echo
  echo "## disabled count"
  grep -cE '^\s+[0-9]+ - .*\(Disabled\)' "$P/logs/17_W7_ctest_raw.log"
} >> "$LOG" 2>&1
cat "$LOG"
