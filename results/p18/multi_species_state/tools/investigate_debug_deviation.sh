#!/usr/bin/env bash
# P18.1 -- investigate the Debug+GUI deviation: 3 timeouts measured against 4 predicted.
#
# The question is NOT "is one fewer failure good news". CLAUDE.md section 8 says a known failure
# stays known only while it reproduces its recorded values EXACTLY, so a changed failure set is a
# finding until it is attributed. This attributes it.
set -uo pipefail
cd /mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp || exit 1
L=results/p18/multi_species_state/logs

echo "=================================================================="
echo "1. WHICH TEST MOVED, AND BY HOW MUCH"
echo "=================================================================="
echo "  baseline (results/p18/baseline + P17.11): 4 timeouts, incl. DriverSeegmiller.MeshSequence"
echo "  this run:"
grep -E 'Test +#(2814|2819|2821|2822):' "$L/P18_1_debug_gui_ctest.log" \
  | sed 's/\.\{3,\}/ /' | sed 's/^/    /'
echo ""
echo "  MeshSequence passed with $(python3 -c 'print(f"{7200-7123.03:.2f}")') s of margin"
echo "  against the frozen 7200 s cap -- $(python3 -c 'print(f"{(7200-7123.03)/7200*100:.2f}")')% under the limit."
echo ""

echo "=================================================================="
echo "2. THE TIMEOUT WAS NOT RAISED"
echo "=================================================================="
grep -o 'timeout 7200' "$L/P18_1_regress.log" | head -1 | sed 's/^/    harness invoked with --/'
echo "    the three remaining timeouts fired at 7200.6x s, so the cap was in force."
echo ""

echo "=================================================================="
echo "3. CAN P18.1 REACH THIS TEST AT ALL?"
echo "=================================================================="
echo "  every file in the repository that names either new type:"
grep -rl 'MassFractionState\|SpeciesRegistry' include src apps tests cuda benchmarks 2>/dev/null \
  | sort | sed 's/^/    /'
echo ""
echo "  ...and whether the DriverSeegmiller sources reference them:"
HITS=$(grep -rl 'MassFractionState\|SpeciesRegistry' \
        tests/integration/backward_facing_step/ 2>/dev/null | wc -l)
echo "    references inside tests/integration/backward_facing_step/ : $HITS"
if [ "$HITS" -eq 0 ]; then
  echo "    -> the test cannot construct, call or link against either new type. P18.1 adds two"
  echo "       translation units to cfdcore that nothing on this test's path references."
fi
echo ""

echo "=================================================================="
echo "4. THE SAME TEST IN THE RELEASE CONFIGURATIONS OF THIS SAME RUN"
echo "=================================================================="
grep -E 'Test +#[0-9]+: DriverSeegmiller\.MeshSequence' "$L/P18_1_release_cuda_ctest.log" \
  | sed 's/\.\{3,\}/ /' | sed 's/^/    release_cuda: /'
echo "    -> it passes comfortably in Release. The Debug+GUI problem is a TIME BUDGET, not a"
echo "       correctness defect -- which is exactly what P17.11 recorded when it left G10 PARTIAL."
echo ""

echo "=================================================================="
echo "5. WHOLE-RUN TIMING, WHICH IS THE ACTUAL EXPLANATION"
echo "=================================================================="
echo "    baseline debug_gui runtime : 8514 s"
echo "    this run                   : 8172 s"
echo "    delta                      : $(python3 -c 'print(f"{(8172-8514)/8514*100:.1f}")')%"
echo ""
echo "    A test sitting at ~100% of the cap moves across it on a run that is 4% faster overall."
echo "    MeshSequence came in at 98.9% of the cap. This is ordinary machine variance on a"
echo "    two-hour Debug solve, in the favourable direction."
