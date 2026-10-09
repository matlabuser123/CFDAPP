#!/usr/bin/env bash
# P14 item 13 -- the P13-001 SIMPLE fingerprint, unchanged.
#
# P14 adds a layer and edits TimeDerivative.hpp/.cpp (additively). SIMPLE must
# produce the SAME BITS. A passing CTest run does not prove that; this does.
cd /mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
export PATH=/usr/local/cuda-12.9/bin:$PATH
L=results/p14/transport-foundation/logs
g++ -std=c++20 -O2 -DNDEBUG -I include -I build/final/generated/include \
    -I /usr/local/cuda-12.9/include results/p13/restart/tools/solve_fingerprint.cpp \
    -o /tmp/p14_fingerprint -Wl,--start-group build/final/src/libcfdcore.a \
    build/final/cuda/libcfdcuda.a -Wl,--end-group -L/usr/local/cuda-12.9/lib64 -lcudart
rc=$?
[ $rc -eq 0 ] || { echo "COMPILE FAILED ($rc)"; exit 1; }
/tmp/p14_fingerprint > $L/fingerprint-p14.log 2>&1
echo "cases fingerprinted: $(grep -c FINGERPRINT $L/fingerprint-p14.log)"
echo "=== compare against the P13-001 frozen baseline (header lines excluded) ==="
BASE=results/p13/restart/logs/fingerprint-after-histories.log
# The baseline was written by run.sh's build_run, which prepends 6 "#" header
# lines (date, git HEAD, library hashes) plus a blank, and appends "# exit code".
# This binary is invoked directly, so it has neither. Strip every "#" line and
# any blank from BOTH sides rather than assuming a fixed offset -- an earlier
# version used `tail -n +8` on both and reported a spurious 7-line difference.
strip() { grep -v "^#" "$1" | grep -v "^$"; }
diff <(strip "$BASE") <(strip $L/fingerprint-p14.log) > $L/fingerprint-diff.txt 2>&1
if [ $? -eq 0 ]; then
  echo "IDENTICAL -- SIMPLE is bitwise unchanged by P14"
else
  echo "DIFFERS:"; head -20 $L/fingerprint-diff.txt
fi
