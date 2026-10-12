#!/bin/bash
# TD-027 AMENDMENT A4 -- the DRY-RUN of C3-A4 on the UNCHANGED baseline.
# Read-only measurement. No production file, no test and no gate is written by this script.
# Fail-closed: any step that cannot be verified aborts before the ~5 h run starts.
set -o pipefail
REPO=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
BUILD=$REPO/build/release
TOOLS=$REPO/results/technical_debt/TD027_cell_diffusion_cap/tools
BIN=$TOOLS/td027_c3a4
cd "$REPO" || exit 2

echo "=== ENVIRONMENT"
echo "date        $(date -Is)"
echo "nproc       $(nproc)   load $(cat /proc/loadavg)"
free -h | sed -n '2p' | awk '{print "mem         " $2 " total, " $7 " available"}'
grep -E '^CMAKE_BUILD_TYPE|^CFDAPP_ENABLE_CUDA' "$BUILD/CMakeCache.txt"
/usr/bin/c++ --version | head -1
echo

echo "=== NOTHING HEAVY MAY RUN ALONGSIDE (hardware policy)"
# Count heavy competitors. NOT `pgrep -c ... || echo 0`: when nothing matches, `pgrep -c`
# prints a zero AND exits non-zero, so the `|| echo 0` fires as well and $busy ends up as TWO
# lines, each a zero. The arithmetic test below then dies with 'integer expression expected'
# instead of comparing anything. Piping the PID list through `wc -l` always yields exactly one
# integer and ignores pgrep's exit status. Self is excluded by PID, not by relying on this
# pattern's lowercase td027_ failing to match the uppercase TD027_ in the script's own path,
# which was accidental. `pgrep -c` is avoided outright: during testing it also disagreed with
# `pgrep -af` on the same pattern, which is unexplained and reason enough not to depend on it.
busy=$(pgrep -f 'ctest|CFDSpeciesTests|G8PremixedFlame|tools/td027_' 2>/dev/null \
        | grep -vx "$$" | wc -l)
echo "competing heavy processes: $busy"
if [ "$busy" -gt 0 ]; then
  echo "*** ABORT: something heavy is running; this run would be unreliable and would"
  echo "    degrade whatever else is in flight."
  exit 3
fi
echo

echo "=== PROVENANCE (recorded, per METHOD_library_hash_gates.md)"
sha256sum "$REPO/src/species/PremixedFlame.cpp" "$REPO/include/cfd/species/PremixedFlame.hpp"
echo "  NOTE: PremixedFlame.cpp is 19db4b6b... after the TD-049 comment fix (commit a4b0b0e),"
echo "        not the fa815c97... recorded in earlier TD-027 logs. The change was COMMENT ONLY"
echo "        and proven BIT-IDENTICAL at object level; see TD049/verification_comment_fix.md."
sha256sum "$BUILD/src/libcfdcore.a"
sha256sum "$TOOLS/c3a4_probe.cpp"
echo

echo "=== STALENESS GUARD (CLAUDE.md §8): library newer than every production source?"
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

echo "=== COMPILE the probe (this command is the reproducibility record)"
CXXFLAGS="-I$REPO/include -I$BUILD/generated/include -I$BUILD/_deps/nlohmann_json-src/include"
CXXFLAGS="$CXXFLAGS -O3 -DNDEBUG -std=c++20 -Wall -Wextra -Wpedantic"
echo "/usr/bin/c++ $CXXFLAGS $TOOLS/c3a4_probe.cpp -o $BIN $BUILD/src/libcfdcore.a"
rm -f "$BIN"
# shellcheck disable=SC2086
/usr/bin/c++ $CXXFLAGS "$TOOLS/c3a4_probe.cpp" -o "$BIN" "$BUILD/src/libcfdcore.a"
rc=$?
echo "compile exit: $rc"
if [ "$rc" -ne 0 ] || [ ! -x "$BIN" ]; then
  echo "*** ABORT: the probe did not compile, or produced no binary."
  exit 5
fi
sha256sum "$BIN"
echo

echo "=== RUN. Four ladder levels plus three single-level B measurements; ~5 h expected."
echo "    Started $(date -Is)"
"$BIN"
echo
echo "=== DONE $(date -Is)"
