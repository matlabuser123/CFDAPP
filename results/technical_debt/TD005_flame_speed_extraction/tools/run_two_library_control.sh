#!/usr/bin/env bash
# TD-005 -- THE TWO-LIBRARY CONTROL for C4d's companion requirement.
#
# CLAUDE.md section 6: "Attribution needs two libraries. Before blaming a change for a failure,
# build a baseline and measure both. The baseline is either an isolated copy with only the change
# removed, or a reconstruction whose hashes match the frozen ones."
#
# WHY THIS IS NEEDED AT ALL. C4d's companion requirement is that the computed answers be BITWISE
# unchanged by the fix. logs/02 (pre-fix) printed `flameSpeed` and `massFlux` at %.17e, so those two
# are already settled by direct comparison with logs/06. It printed the BALANCES at %.4e, and a
# five-figure agreement is not a bitwise claim -- reporting it as one would be exactly the kind of
# overstatement this item has already had to correct twice. So the pre-fix library is rebuilt and
# measured with the SAME probe binary source, and the full-precision values compared.
#
# THE RECONSTRUCTION IS PROVED, NOT ASSUMED. swap_c4d.py reverses the edit and then verifies both
# files against the sha256 recorded in logs/00_freeze.log, refusing (and restoring) on a mismatch.
# So the control library is provably built from the source the gate was frozen against.
#
# THE TREE IS ALWAYS RESTORED. The trap re-applies the fix and rebuilds even if a step fails, so a
# failure here cannot leave production reverted.
set -u

REPO=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
BUILD="$REPO/build/release"
EV="$REPO/results/technical_debt/TD005_flame_speed_extraction"
SWAP="/mnt/c/Users/Hasib/AppData/Local/Temp/claude/C--Users-Hasib-Desktop-CFDAPP/ee8e90df-238e-4c69-ad45-fe3ef00cba5d/scratchpad/swap_c4d.py"
BIN=/tmp/td005_prefix_control

cd "$REPO" || exit 1

restore() {
  echo
  echo "=== RESTORING the post-fix tree (runs even on failure)"
  python3 "$SWAP" in "$REPO"
  cmake --build "$BUILD" --target cfdcore -j 20 >/dev/null 2>&1
  echo "post-fix library rebuilt; libcfdcore.a $(sha256sum "$BUILD/src/libcfdcore.a" | cut -c1-16)..."
}
trap restore EXIT

echo "TD-005 -- PRE-FIX CONTROL LIBRARY, measured with the same probe"
echo "started $(date -Is)"
echo

echo "=== STEP 1  reconstruct the pre-fix sources and PROVE them against the frozen hashes"
python3 "$SWAP" out "$REPO"
if [ $? -ne 0 ]; then
  echo "*** ABORT: reconstruction failed its hash proof."
  exit 1
fi
echo

echo "=== STEP 2  build the PRE-FIX library"
cmake --build "$BUILD" --target cfdcore -j 20
rc=$?
echo "build exit: $rc"
if [ "$rc" -ne 0 ]; then
  echo "*** ABORT: the pre-fix library did not build."
  exit 1
fi
sha256sum "$BUILD/src/libcfdcore.a"
echo

echo "=== STEP 3  compile and run the SAME probe against it"
CXXFLAGS="-I$REPO/include -I$BUILD/generated/include -I$BUILD/_deps/nlohmann_json-src/include"
CXXFLAGS="$CXXFLAGS -O3 -DNDEBUG -std=c++20 -Wall -Wextra -Wpedantic"
# shellcheck disable=SC2086
/usr/bin/c++ $CXXFLAGS "$EV/tools/field_dump_probe.cpp" -o "$BIN" "$BUILD/src/libcfdcore.a"
rc=$?
echo "compile exit: $rc"
if [ "$rc" -ne 0 ]; then
  echo "*** ABORT: the probe did not compile against the pre-fix library."
  exit 1
fi
"$BIN"
echo
echo "probe exit: $?"
echo "finished $(date -Is)"
echo
echo "COMPARE the BITWISE-COMPANION blocks here against logs/06_verify_postfix.log."
echo "Everything except flamePosition must be IDENTICAL to the last digit."
