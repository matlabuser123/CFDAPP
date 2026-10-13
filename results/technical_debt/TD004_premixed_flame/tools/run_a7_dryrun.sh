#!/bin/bash
# TD-004 AMENDMENT A7 -- the DRY-RUN of C6a-ord-A7, on the UNCHANGED tree.
#
# §5.3: an amendment is frozen only AFTER its own dry-run and non-vacuity check. This is that
# dry-run. It changes no production file, no test and no frozen gate.
#
# NON-VACUITY IS THE WHOLE POINT OF RUNNING TWICE. The same binary must give OPPOSITE verdicts:
#   1e-3  ->  UNRESOLVED, reproducing GATE_A6's recorded n1 = 12.94, n2 = 2.00
#   1e-4  ->  RESOLVED,   predicted n1 = 103.6, n2 = 16.0, p in [2.593, 2.801] -> FAIL
# A criterion that resolves at BOTH tolerances is not measuring resolution, and the amendment would
# be withdrawn.
set -o pipefail
REPO=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
BUILD=$REPO/build/release
TOOLS=$REPO/results/technical_debt/TD004_premixed_flame/tools
BIN=$TOOLS/td004_c6a_a7
cd "$REPO" || exit 2

echo "=== ENVIRONMENT"
echo "date        $(date -Is)"
echo "nproc       $(nproc)   load $(cat /proc/loadavg)"
free -h | sed -n '2p' | awk '{print "mem         " $2 " total, " $7 " available"}'
grep -E '^CMAKE_BUILD_TYPE|^CFDAPP_ENABLE_CUDA' "$BUILD/CMakeCache.txt"
/usr/bin/c++ --version | head -1
echo

echo "=== NOTHING HEAVY MAY RUN ALONGSIDE (hardware policy)"
# `pgrep -c ... || echo 0` is deliberately NOT used: when nothing matches it prints a zero AND
# exits non-zero, so the fallback fires too and the count becomes two lines, breaking the test.
# Piping the PID list through wc -l always yields one integer. Self excluded by PID.
busy=$(pgrep -f 'ctest|CFDSpeciesTests|G8PremixedFlame|tools/td027_|tools/td004_' 2>/dev/null \
        | grep -vx "$$" | wc -l)
echo "competing heavy processes: $busy"
if [ "$busy" -gt 0 ]; then
  echo "*** ABORT: something heavy is running. This run would be unreliable and would degrade"
  echo "    whatever is in flight. Nothing was measured."
  exit 3
fi
echo

echo "=== PROVENANCE (recorded, per METHOD_library_hash_gates.md)"
sha256sum include/cfd/species/PremixedFlame.hpp src/species/PremixedFlame.cpp
sha256sum "$BUILD/src/libcfdcore.a"
sha256sum "$TOOLS/c6a_a7_probe.cpp" "$TOOLS/c6a_amended_probe.cpp"
echo "  NOTE: c6a_amended_probe.cpp is GATE_A6's evidence and is hashed here UNMODIFIED, so the"
echo "        six-line provenance of the A7 copy is auditable."
echo

echo "=== STALENESS GUARD (CLAUDE.md §8)"
NEWEST=$(find "$REPO/src" "$REPO/include" -name '*.cpp' -o -name '*.hpp' | xargs stat -c '%Y' | sort -rn | head -1)
LIB=$(stat -c '%Y' "$BUILD/src/libcfdcore.a")
echo "  newest source mtime: $NEWEST"
echo "  libcfdcore.a  mtime: $LIB"
if [ "$LIB" -lt "$NEWEST" ]; then
  echo "*** ABORT: the library is older than a production source. Rebuild first."
  exit 4
fi
echo "  OK: library is not stale."
echo

echo "=== COMPILE the A7 probe (this command is the reproducibility record)"
CXXFLAGS="-I$REPO/include -I$BUILD/generated/include -I$BUILD/_deps/nlohmann_json-src/include"
CXXFLAGS="$CXXFLAGS -O3 -DNDEBUG -std=c++20 -Wall -Wextra -Wpedantic"
echo "/usr/bin/c++ $CXXFLAGS $TOOLS/c6a_a7_probe.cpp -o $BIN $BUILD/src/libcfdcore.a"
rm -f "$BIN"
# shellcheck disable=SC2086
/usr/bin/c++ $CXXFLAGS "$TOOLS/c6a_a7_probe.cpp" -o "$BIN" "$BUILD/src/libcfdcore.a"
rc=$?
echo "compile exit: $rc"
if [ "$rc" -ne 0 ] || [ ! -x "$BIN" ]; then
  echo "*** ABORT: the probe did not compile, or produced no binary."
  exit 5
fi
sha256sum "$BIN"
echo

for TOL in 1.0e-3 1.0e-4; do
  echo "################################################################################"
  echo "=== RUN at tolerance $TOL      started $(date -Is)"
  if [ "$TOL" = "1.0e-3" ]; then
    echo "    NON-VACUITY CASE: must report UNRESOLVED and reproduce A6's"
    echo "    S_L 4.90527 / 4.94863 / 4.95533, n1 = 12.94, n2 = 2.00, C6a-bal PASS x3."
  else
    echo "    MUST report RESOLVED. Predicted n1 = 103.6, n2 = 16.0, p in [2.593, 2.801] -> FAIL."
  fi
  echo "################################################################################"
  "$BIN" "$TOL"
  echo "    finished $(date -Is)"
  echo
done

echo "=== DONE $(date -Is)"
