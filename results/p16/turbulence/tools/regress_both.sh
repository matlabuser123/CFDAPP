#!/usr/bin/env bash
# P16 regression, BOTH configurations, at the canonical scope.
#
#     regress_both.sh            full scope   (DEFAULT -- unchanged behaviour)
#     regress_both.sh p16        P16 scope    (also excludes the deferred
#                                              separated-flow benchmark)
#
# TWO DELIBERATE DEVIATIONS FROM A BARE `ctest`, both required:
#
# 1. -E excludes the four P20 known-expected-failure suites. They assert the
#    adopted attached-flow gates against the three DEFERRED automatic wall
#    treatments, which are documented to fail them
#    (results/p16/turbulence/automatic_wall/SUMMARY.md). They are kept, not
#    disabled, and stay observable through run_p20_known_failures.sh. Leaving
#    them in a regression makes it permanently red and MASKS a real regression.
#    This is the same filter run_p14_regression.sh already applies.
#
# 2. --timeout 7200. DriverSeegmiller.CorrectedMeshSequence runs ~1100 s alone
#    and longer when ctest schedules it alongside the other heavy BFS cases;
#    ctest's default 1500 s times it out, which reads as a failure and is not
#    one.
#
# THE `p16` MODE, AND WHY IT IS A MODE RATHER THAN AN EDIT
# -------------------------------------------------------
# P16's scope explicitly does not include re-running Driver & Seegmiller, and
# `DriverSeegmiller.CorrectedMeshSequence` is a PRESERVED FAILING benchmark:
# it fails on physics (no mesh satisfies G1; refinement increases the
# reattachment error) and it failed that way BEFORE the P16 work, in the
# CPU-only build, which does not even compile the GPU solver
# (results/p16/turbulence/gpu_equivalence/krylov_blocker/fix/prior_run/).
#
# It is NOT removed from the default scope. `regress_both.sh` with no argument
# behaves exactly as it always did and still runs it, so the repository's own
# full-scope regression keeps reporting the failure. Only the explicitly named
# `p16` mode skips it, it announces the skip in the log, and it never reports
# the skipped test as passing. `run_p16_deferred.sh` is how it stays observable.
#
# IT DOES NOT SAVE MUCH TIME, and the mode is not offered as if it did.
# Measured on the 2026-09-23 run: the excluded benchmark takes 31.1 min, but
# ctest -j 8 is bounded by its LONGEST test, and DriverSeegmillerInlet.MediumGridAB
# (28.8 min) is right behind it. Excluding one buys about 2 min per configuration,
# not 30. The whole Driver & Seegmiller family is ~168 min of the 220 min of test
# work in a configuration, so only excluding the family would change the wall
# time -- and the rest of that family PASSES, so excluding it would weaken the
# regression rather than scope it. The p16 mode exists for CORRECTNESS of the
# verdict, not for speed.
set -u
cd /mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
export PATH=/usr/local/cuda-12.9/bin:$PATH

MODE=${1:-full}

# The four deferred automatic-wall suites, excluded in every mode.
EXCLUDE='SSTAutomaticWallChannel|SSTCompositeWallChannel|SSTMomentumFluxChannel|WallArchitectureDiagnostic'

DEFERRED_BFS='DriverSeegmiller.CorrectedMeshSequence'

case "$MODE" in
  full)
    echo "SCOPE: full -- every test except the four deferred automatic-wall suites."
    ;;
  p16)
    EXCLUDE="$EXCLUDE|$DEFERRED_BFS"
    echo "SCOPE: p16 -- all P16-applicable gates."
    echo "EXCLUDED -- preserved failing P20/deferred benchmark: $DEFERRED_BFS"
    echo "  It is NOT marked PASS. It is not run in this mode and it is not"
    echo "  fixed, re-thresholded or hidden. Observe it with run_p16_deferred.sh;"
    echo "  its pre-P16 failure is preserved under"
    echo "  results/p16/turbulence/gpu_equivalence/krylov_blocker/fix/prior_run/."
    ;;
  *)
    echo "usage: regress_both.sh [full|p16]" >&2
    exit 2
    ;;
esac
echo ""

for CFG in cuda final; do
  echo "=== build/$CFG ($MODE) ==="
  cmake --build "build/$CFG" -j 16 2>&1 | grep -E "error|FAILED" | head -20
  ctest --test-dir "build/$CFG" -j 8 --timeout 7200 -E "$EXCLUDE" --output-on-failure 2>&1 \
    | tail -40
  echo ""
done

if [ "$MODE" = "p16" ]; then
  echo "REMINDER: this run's verdict is 'all P16-applicable regression gates pass',"
  echo "NOT 'every test in the repository passes'. $DEFERRED_BFS remains failing"
  echo "and deferred to P20."
fi
