#!/usr/bin/env bash
# P18.5 -- build and run the measurement probe, RECORDING THE EXACT COMMAND.
#
# P18.4's probe (results/p18/reaction_source/tools/reaction_probe.cpp) was never accompanied by
# its compile command, so its log cannot be reproduced from the evidence alone. This records the
# command, the compiler version and the library hash in the log it writes, so the measurement can
# be rebuilt from the evidence directory by itself.
set -u

cd "$(dirname "$0")/../../../.." || exit 1
TOOLS=results/p18/arrhenius_chemistry/tools
LIB=build/final/src/libcfdcore.a
OUT=${OUT:-/tmp/arrhenius_probe}

if [[ ! -f "$LIB" ]]; then
  echo "FAIL: $LIB is missing -- build the cfdcore target first"
  exit 1
fi

CXX=${CXX:-g++}
CMD="$CXX -std=c++20 -O2 -Wall -Wextra -I include $TOOLS/arrhenius_probe.cpp $LIB -o $OUT"

echo "=== P18.5 measurement probe ==="
echo "date      : $(date -u +%Y-%m-%dT%H:%M:%SZ)"
echo "compiler  : $($CXX --version | head -1)"
echo "library   : $LIB"
echo "lib sha256: $(sha256sum "$LIB" | cut -d' ' -f1)"
echo "probe     : $TOOLS/arrhenius_probe.cpp"
echo "probe sha : $(sha256sum "$TOOLS/arrhenius_probe.cpp" | cut -d' ' -f1)"
echo "command   : $CMD"
echo

# shellcheck disable=SC2086
if ! $CMD; then
  echo "FAIL: the probe did not compile"
  exit 1
fi

"$OUT"
status=$?
echo
echo "probe exit status: $status"
exit "$status"
