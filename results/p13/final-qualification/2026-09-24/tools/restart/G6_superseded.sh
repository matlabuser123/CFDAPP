#!/usr/bin/env bash
# P13 Final Qualification 2026-09-24 -- G6 handler.
#
# G6 watches the "known GPU BiCGSTAB restart asymmetry" and asks whether it has
# BROADENED. On this tree the probe reports NOT REPRODUCED, because the defect
# was REPAIRED on 2026-09-23 (results/p16/turbulence/gpu_equivalence/
# krylov_blocker/, fix in cuda/kernels/GpuLinearSolverCuda.cpp). Its recorded
# pre-fix signature was status=3, outer=1845, "Breakdown after 74 iterations";
# it now reads MaxIterations at 3000, matching the recorded post-fix state.
#
# So G6 is an OBSOLETE INSTRUMENT, not a regression: it cannot fire, and it
# correctly refuses to report "unchanged" rather than passing vacuously.
#
# THIS SCRIPT DOES NOT SKIP IT. It:
#   * runs the probe and preserves its output verbatim;
#   * tolerates ONLY exit 2 (not reproduced), and only when a CUDA device was
#     actually present -- the probe returns 2 for "no CUDA device" as well, and
#     tolerating that blindly would silently skip the entire check;
#   * treats exit 1 (BROADENED) as a hard failure, unchanged;
#   * requires the SUPERSEDING gates to pass in its place;
#   * requires those gates to be NON-VACUOUS, by insisting the control that
#     proves Breakdown is still reachable actually ran.
#
# The replacement passes the test that disqualified the original: G6 cannot
# fail, whereas GpuBiCGSTABRestartTest.BreakdownIsStillReachable demonstrates
# the replacement's criterion can still detect a Breakdown.
set -uo pipefail
ROOT=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
Q=$ROOT/results/p13/final-qualification/2026-09-24
T=$Q/tools/restart
L=$Q/restart_logs
BIN=$ROOT/build/qual-final/tests/unit/gpu/CFDGpuTests
cd "$ROOT"

bash "$T/known_debt.sh"
rc=$?

case "$rc" in
  0)
    echo "G6: debt UNCHANGED -- reproduced and not broadened."
    exit 0
    ;;
  2)
    if grep -q "no CUDA device" "$L/G6_known_debt.log" 2>/dev/null; then
      echo "G6 FAILED: the probe exited 2 because NO CUDA DEVICE was present."
      echo "  That is not 'superseded' -- the check did not run at all."
      exit 1
    fi
    echo "G6: SUPERSEDED -- the reproducer no longer triggers because the defect"
    echo "    was repaired on 2026-09-23. The probe reports NOT REPRODUCED and"
    echo "    refuses to claim 'unchanged', which is correct behaviour."
    echo "    Replacement gates are required to pass in its place:"
    ;;
  *)
    echo "G6 FAILED: probe exit $rc (1 = the debt BROADENED)."
    exit 1
    ;;
esac

"$BIN" --gtest_filter='KrylovRestartParity.*:GpuBiCGSTABRestartTest.*' \
  > "$L/G6_replacement.log" 2>&1
grc=$?
grep -E '^\[       OK \]|^\[  FAILED  \]|^\[  PASSED  \]' "$L/G6_replacement.log" | sed 's/^/      /'
if [ "$grc" -ne 0 ]; then
  echo "G6 FAILED: the replacement gates did not pass."
  exit 1
fi

# NON-VACUITY: the replacement is only a replacement if it CAN fail. G6 was
# disqualified for being unable to fire, so a replacement that also cannot fire
# would be no better.
if ! grep -q 'OK \] GpuBiCGSTABRestartTest.BreakdownIsStillReachable' "$L/G6_replacement.log"; then
  echo "G6 FAILED: BreakdownIsStillReachable did not run, so the replacement is"
  echo "  not shown to be non-vacuous -- exactly the defect G6 was set aside for."
  exit 1
fi
echo "      non-vacuity: BreakdownIsStillReachable ran and passed -- the"
echo "      replacement criterion can still detect a Breakdown."
exit 0
