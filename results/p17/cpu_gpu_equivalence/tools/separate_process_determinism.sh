#!/usr/bin/env bash
# P17.10 R3 -- GPU determinism across SEPARATE PROCESSES.
#
# R2 measures repeats inside one process, which leaves open whether anything process-scoped (a
# context, a cached module, an allocator's layout) changes the answer. This runs the equivalence
# binary as two independent processes and compares their COMPLETE stdout byte for byte.
#
# WHY THAT IS A REAL MEASUREMENT AND NOT A TAUTOLOGY: the suite prints GPU-computed quantities at
# full precision -- the global dt (17 significant digits), every per-step dt through the comparison,
# the detected shock position, the per-criterion differing counts. If the GPU produced anything
# different between processes, those lines would differ. It needs no code change, which is the
# point: the binary under test is exactly the one the regression ran.
#
# THE FAIL-OPEN GUARD: two EMPTY outputs are also identical. The script therefore requires the
# output to be non-trivial and to contain the specific high-precision lines it is relying on,
# before it is allowed to report agreement.
set -uo pipefail
cd /mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp || exit 1
export PATH="/usr/local/cuda-12.9/bin:$PATH"
P17=results/p17/cpu_gpu_equivalence
BIN=build/qual-final/tests/unit/gpu/CFDGpuTests
A=$P17/logs/40_r3_process_a.log
B=$P17/logs/40_r3_process_b.log

test -x "$BIN" || { echo "FATAL: $BIN missing"; exit 1; }

echo "=== two independent processes, same filter ==="
OMP_NUM_THREADS=1 "$BIN" --gtest_filter='DensityBasedGpuEquivalence*' > "$A" 2>&1
echo "process A exit: $?"
OMP_NUM_THREADS=1 "$BIN" --gtest_filter='DensityBasedGpuEquivalence*' > "$B" 2>&1
echo "process B exit: $?"

# gtest prints elapsed times per test, which legitimately differ. Strip ONLY the timing parenthetical
# and the summary duration; everything else -- every number the GPU computed -- must match.
strip() { sed -E 's/\([0-9]+ ms( total)?\)//g' "$1"; }

echo ""
echo "--- non-vacuity: the output must actually contain the GPU-computed numbers ---"
for pat in 'global dt cpu=' 'D1 step  50' 'D4: shock cpu=' 'A7 recovery:'; do
  n=$(grep -ac -- "$pat" "$A")
  if [ "$n" -gt 0 ]; then
    echo "  PRESENT ($n): $pat"
  else
    echo "  *** MISSING: $pat -- two outputs could match by both being empty. FAILING CLOSED. ***"
    exit 1
  fi
done
LINES=$(wc -l < "$A")
echo "  process A stdout: $LINES lines"
if [ "$LINES" -lt 40 ]; then echo "  *** too little output to mean anything. FAILING CLOSED. ***"; exit 1; fi

echo ""
echo "--- byte comparison (timings stripped) ---"
if diff <(strip "$A") <(strip "$B") > "$P17/logs/40_r3_diff.log" 2>&1; then
  echo "  IDENTICAL: every GPU-computed value agrees across two separate processes"
  echo "  sha256 A (stripped): $(strip "$A" | sha256sum | cut -d' ' -f1)"
  echo "  sha256 B (stripped): $(strip "$B" | sha256sum | cut -d' ' -f1)"
  RC=0
else
  echo "  *** DIFFER -- separate-process determinism does NOT hold ***"
  head -40 "$P17/logs/40_r3_diff.log" | sed 's/^/    /'
  RC=1
fi

echo ""
echo "--- the control: the comparison must be able to report a difference ---"
# A one-character perturbation of one copy must be detected. If diff reports these identical, the
# comparison above proves nothing.
sed '1s/^/X/' "$B" > "$P17/logs/40_r3_control.log"
if diff <(strip "$A") <(strip "$P17/logs/40_r3_control.log") > /dev/null 2>&1; then
  echo "  *** CONTROL FAILED: a perturbed copy compared EQUAL, so the diff is not measuring ***"
  RC=1
else
  echo "  CONTROL OK: a one-character perturbation is detected"
fi
exit "$RC"
