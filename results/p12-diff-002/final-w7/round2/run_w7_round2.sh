#!/usr/bin/env bash
# P12-DIFF-002 final authoritative W7 (post UC-001 + UF-001).
#
# No previous W7 aggregate is reused. The sequence is
#   configure -> CLEAN-FIRST rebuild -> prove the binaries were rebuilt -> verify production
#   hashes -> run the complete suite
# and it FAILS CLOSED: if the build fails, nothing is executed.
#
# "Demonstrably corresponds to the current source" is established two ways: every test binary's
# mtime must be newer than the newest production source file, and the library hash is recorded
# before and after. Executable-bit guard included (freshly linked binaries on the Windows mount
# can lose the x bit; UC-001 hit this).
set -u
R=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
B=$R/build/release
P=$R/results/p12-diff-002/final-w7/round2
mkdir -p "$P/logs"
LOG=$P/logs/01_W7.log
cd "$R" || exit 1
{
  echo "# P12-DIFF-002 FINAL authoritative W7; $(date -u +%Y-%m-%dT%H:%M:%SZ)"
  echo "# git HEAD $(git rev-parse HEAD)"
  echo
  echo "## before"
  echo "  libcfdcore.a $( [ -f "$B/src/libcfdcore.a" ] && sha256sum "$B/src/libcfdcore.a" | cut -d' ' -f1 || echo '(absent)')"
  echo "  test binaries present: $(find "$B" -type f -name 'CFD*Tests' 2>/dev/null | wc -l)"
  echo
  echo "## configure"
  if cmake -S "$R" -B "$B" > "$P/logs/01_configure.log" 2>&1; then echo "  OK"; else echo "  FAILED"; tail -20 "$P/logs/01_configure.log"; exit 1; fi
  echo
  echo "## clean-first rebuild of EVERYTHING"
  START=$(date +%s)
  if cmake --build "$B" -j"$(nproc)" --clean-first > "$P/logs/01_build.log" 2>&1; then
    echo "  BUILD OK ($(wc -l < "$P/logs/01_build.log") lines, $(( $(date +%s) - START )) s)"
  else
    echo "  BUILD FAILED -- fail closed, nothing executed"
    tail -40 "$P/logs/01_build.log"; exit 1
  fi
  echo
  echo "## proof the binaries correspond to the current source"
  NEWEST_SRC=$(find "$R/src" "$R/include" -type f \( -name '*.cpp' -o -name '*.hpp' \) -printf '%T@ %p\n' | sort -rn | head -1)
  NEWEST_SRC_T=${NEWEST_SRC%% *}
  echo "  newest production source: ${NEWEST_SRC#* }"
  echo "                            $(date -u -d @${NEWEST_SRC_T%.*} +%Y-%m-%dT%H:%M:%SZ)"
  STALE=0; TOTAL=0
  while read -r t bin; do
    TOTAL=$((TOTAL+1))
    awk -v a="$t" -v b="$NEWEST_SRC_T" 'BEGIN{exit !(a<b)}' && { STALE=$((STALE+1)); echo "  STALE: $bin"; }
  done < <(find "$B" -type f -name 'CFD*Tests' -printf '%T@ %p\n')
  echo "  test binaries: $TOTAL, older than the newest production source: $STALE"
  echo "  libcfdcore.a after rebuild $(sha256sum "$B/src/libcfdcore.a" | cut -d' ' -f1)"
  echo
  echo "## production source hashes (must equal the recorded post-fix values)"
  sha256sum src/discretization/NonOrthogonalDiffusion.cpp src/physics/MomentumEquation.cpp \
            src/thermal/EnergyEquation.cpp src/thermal/ThermalInterface.cpp \
            src/species/SpeciesEquation.cpp src/turbulence/KEpsilonEquation.cpp \
            src/solver/SolverRobustness.cpp | sed 's/^/  /'
  echo
  NOEXEC=$(find "$B" -type f -name 'CFD*Tests' ! -perm -u+x 2>/dev/null | wc -l)
  echo "## executable-bit guard: $NOEXEC binaries lacked the x bit; chmod applied"
  find "$B" -type f -name 'CFD*Tests' -exec chmod +x {} + 2>/dev/null
  echo "   remaining without x bit: $(find "$B" -type f -name 'CFD*Tests' ! -perm -u+x 2>/dev/null | wc -l)"
  echo
  echo "## ctest (complete suite)"
} > "$LOG" 2>&1
ctest --test-dir "$B" -j"$(nproc)" > "$P/logs/01_ctest_raw.log" 2>&1
{
  grep -E '^ *[0-9]+% tests passed|^Total Test time|tests failed out of' "$P/logs/01_ctest_raw.log" | sed 's/^/  /'
  echo "  ctest entries total: $(grep -cE '^\s+Test\s+#[0-9]+' "$P/logs/01_ctest_raw.log" 2>/dev/null || echo '?')"
  echo "  disabled/not-run entries: $(grep -cE '^\s+[0-9]+ - .*\(Disabled\)' "$P/logs/01_ctest_raw.log")"
  echo
  echo "## failing tests"
  sed -n '/The following tests FAILED:/,$p' "$P/logs/01_ctest_raw.log" | grep -E '^\s+[0-9]+ - ' | sed 's/^/  /'
} >> "$LOG" 2>&1
cat "$LOG"
