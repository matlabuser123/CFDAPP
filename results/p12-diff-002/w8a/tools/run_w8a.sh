#!/usr/bin/env bash
# P12-DIFF-002 amended W8, run from a FRESH authoritative build (results/p12-diff-002/w8a/acceptance_gate.md).
#
# Same sequence as final-w7/tools/run_w7.sh:
#   configure -> CLEAN-FIRST rebuild -> prove the binaries were rebuilt -> verify production and
#   frozen-gate hashes -> executable-bit guard -> run the two amended W8 tests
# FAILS CLOSED: if the build fails or any frozen hash differs, the tests are not executed.
set -u
R=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
B=$R/build/release
P=$R/results/p12-diff-002/w8a
mkdir -p "$P/logs"
LOG=$P/logs/02_W8A_build.log
RUN=$P/logs/03_W8A.log
FILTER='StructuredQuadProductionCase.DistortedPoiseuilleGridConvergence:MultiBlockProductionCase.CurvedChannelGridConvergence'
cd "$R" || exit 1

# Every value below is copied from logs/00_freeze.log (attempt 2), not retyped.
declare -A FROZEN=(
  [src/discretization/NonOrthogonalDiffusion.cpp]=20a02b16499fa7b0aca07667baae638714e1a176ed6eba33b414a194cf701e1c
  [src/discretization/Gradient.cpp]=d24882a96e5cbc0025121a71eb9f88e13ffe0ef9a8a09f2e2f9f4d40fd8c03b6
  [src/physics/MomentumEquation.cpp]=29af1927587558e52fd30a53b2c0bb03438b71044f370f902c414455e50ee7e3
  [src/pressure_velocity/SIMPLE.cpp]=d38c2bb3d86b62b693a625b30b002d2deac511b5c324b92c197af4a810c462cd
  [src/solver/SolverRobustness.cpp]=2b97f94a7dcf2bf943a01f9bdb871acfe4f01b83e9d7f1c0a4587308c596ab17
  [src/mesh/MeshGeometry.cpp]=04c964c24d6098dbe20def251fa0eba611a1725f17d9e352acf38631e67645b0
  [src/thermal/EnergyEquation.cpp]=fbd47a072fa221f071082ac17a0d40dc8f547909b0f874af007b199a6dba953a
  [src/thermal/ThermalInterface.cpp]=7d58766dfde9ba7fc28971d8a79172fc59e8268865760efdc8b9a4a46cf1dbe1
  [results/p12-mesh-001/summary.md]=13e08a6b809480798b05f25b0548c20228fe4ed8f5346798f97d64643451c34b
  [results/p12-mesh-003/acceptance_gate.md]=4b772e55be94defdc50b0624f671582f0c4cfbb2daa65911e9d3788989c0601a
  [results/p12-mesh-003/summary.md]=6e943b596b4c61900aa2d9cb384d1fc7216a3b0a01ac9c48e4de8360a3a00d62
  [results/p12-diff-002/w8a/acceptance_gate.md]=2707f72bfee7ab7096dca16486354ae071842d7897dab10d6f801d393a11af2f
  [results/p12-diff-002/w8-inv-001/summary.md]=a795cc4bb2ad07d2a0c337eb082503f127e83aed0cae95b08b94b291c322f4db
  [results/p12-diff-002/w8/summary.md]=96932c05f6665575b29b944f731075357c90d80b82c504abc86a7c53aec6eaa4
  [results/p12-diff-002/w8/logs/01_W8.log]=ff3fe9ced3dac6cb754384f47f18f475f1825b7a536373885397b5eab5c7ff01
)

{
  echo "# P12-DIFF-002 amended W8 -- fresh authoritative build; $(date -u +%Y-%m-%dT%H:%M:%SZ)"
  echo "# git HEAD $(git rev-parse HEAD)"
  echo
  echo "## frozen hashes (fail closed on any mismatch)"
  BAD=0
  for f in "${!FROZEN[@]}"; do
    got=$(sha256sum "$f" 2>/dev/null | cut -d' ' -f1)
    if [ "$got" = "${FROZEN[$f]}" ]; then echo "  OK        $f"; else echo "  MISMATCH  $f  got '${got}' want ${FROZEN[$f]}"; BAD=1; fi
  done
  [ "$BAD" = 0 ] || { echo "FROZEN HASH MISMATCH -- fail closed, nothing built or executed"; exit 1; }
  echo
  echo "## amended test sources"
  sha256sum tests/integration/case/test_structured_quad_production_case.cpp \
            tests/integration/case/test_multiblock_production_case.cpp | sed 's/^/  /'
  echo
  echo "## before"
  echo "  libcfdcore.a $( [ -f "$B/src/libcfdcore.a" ] && sha256sum "$B/src/libcfdcore.a" | cut -d' ' -f1 || echo '(absent)')"
  echo
  echo "## configure"
  if cmake -S "$R" -B "$B" > "$P/logs/02_configure.log" 2>&1; then echo "  OK"; else echo "  FAILED"; tail -20 "$P/logs/02_configure.log"; exit 1; fi
  echo
  echo "## clean-first rebuild of EVERYTHING"
  if cmake --build "$B" -j"$(nproc)" --clean-first > "$P/logs/02_build_raw.log" 2>&1; then
    echo "  BUILD OK ($(wc -l < "$P/logs/02_build_raw.log") lines)"
  else
    echo "  BUILD FAILED -- fail closed, nothing executed"
    tail -40 "$P/logs/02_build_raw.log"; exit 1
  fi
  echo "  warnings in the two amended test sources: $(grep -cE 'test_(structured_quad|multiblock)_production_case\.cpp:[0-9]+:[0-9]+: warning' "$P/logs/02_build_raw.log")"
  grep -E 'test_(structured_quad|multiblock)_production_case\.cpp:[0-9]+:[0-9]+: warning' "$P/logs/02_build_raw.log" | sed 's/^/    /'
  echo
  echo "## proof the binaries correspond to the current source"
  NEWEST=$(find "$R/src" "$R/include" "$R/tests/integration/case" -type f \( -name '*.cpp' -o -name '*.hpp' \) -printf '%T@ %p\n' | sort -rn | head -1)
  NEWEST_T=${NEWEST%% *}
  echo "  newest source: ${NEWEST#* }"
  STALE=0; TOTAL=0
  while read -r t bin; do
    TOTAL=$((TOTAL+1))
    awk -v a="$t" -v b="$NEWEST_T" 'BEGIN{exit !(a<b)}' && { STALE=$((STALE+1)); echo "  STALE: $bin"; }
  done < <(find "$B" -type f -name 'CFD*Tests' -printf '%T@ %p\n')
  echo "  test binaries: $TOTAL, older than the newest source: $STALE"
  [ "$STALE" = 0 ] || { echo "STALE BINARIES -- fail closed"; exit 1; }
  echo "  libcfdcore.a after rebuild $(sha256sum "$B/src/libcfdcore.a" | cut -d' ' -f1)"
  echo
  NOEXEC=$(find "$B" -type f -name 'CFD*Tests' ! -perm -u+x 2>/dev/null | wc -l)
  echo "## executable-bit guard: $NOEXEC binaries lacked the x bit; chmod applied"
  find "$B" -type f -name 'CFD*Tests' -exec chmod +x {} + 2>/dev/null
  echo "   remaining without x bit: $(find "$B" -type f -name 'CFD*Tests' ! -perm -u+x 2>/dev/null | wc -l)"
  BIN=$(find "$B" -type f -name CFDCaseIntegrationTests | head -1)
  echo
  echo "## binary"
  echo "  $BIN"
  echo "  sha256 $(sha256sum "$BIN" | cut -d' ' -f1)"
  # --gtest_list_tests lists DISABLED_ tests too; the run's own "YOU HAVE N DISABLED TESTS" line is
  # the authoritative disabled count for the filtered run.
  echo "  tests listed in binary (incl. DISABLED_): $("$BIN" --gtest_list_tests | grep -cE '^  ')"
  echo "  of which DISABLED_ by test name: $("$BIN" --gtest_list_tests | grep -cE '^  DISABLED_')"
  echo "  suites named DISABLED_: $("$BIN" --gtest_list_tests | grep -cE '^DISABLED_')"
} > "$LOG" 2>&1
cat "$LOG"
grep -q '^STALE BINARIES\|fail closed' "$LOG" && exit 1
BIN=$(find "$B" -type f -name CFDCaseIntegrationTests | head -1)
{
  echo "# P12-DIFF-002 amended W8 -- the two amended tests; $(date -u +%Y-%m-%dT%H:%M:%SZ)"
  echo "# libcfdcore.a  $(sha256sum "$B/src/libcfdcore.a" | cut -d' ' -f1)"
  echo "# test binary   $(sha256sum "$BIN" | cut -d' ' -f1)"
  echo "# amended test sources:"
  sha256sum tests/integration/case/test_structured_quad_production_case.cpp \
            tests/integration/case/test_multiblock_production_case.cpp | sed 's/^/#   /'
  echo
  "$BIN" --gtest_filter="$FILTER"
  echo "exit $?"
} > "$RUN" 2>&1
grep -E "^\[ RUN|^\[  FAILED|^\[       OK|^\[  PASSED|Failure|Expected|actual|W8A|ny [0-9]+:|pair|exit " "$RUN"
