#!/usr/bin/env bash
# =====================================================================
# DEFERRED SEPARATED-FLOW BENCHMARK -- excluded from `regress_both.sh p16`.
# =====================================================================
#
# `DriverSeegmiller.CorrectedMeshSequence` is a PRESERVED FAILING benchmark. It
# is deliberately kept, deliberately NOT disabled, and deliberately NOT part of
# the P16 qualification scope. This command is how it stays observable --
# nothing about it is hidden.
#
# Expected result: RED.
#
# It fails on physics, not on tolerance bookkeeping, on two counts:
#
#   test_driver_seegmiller.cpp:552   validMeshes > 0  -->  0
#       no mesh in the sequence satisfies G1, so the accuracy gate would
#       otherwise be evaluated on nothing
#   test_driver_seegmiller.cpp:582   E_x_r must not grow under refinement
#       0.2113 -> 0.4010: the sequence walks AWAY from the reference
#
# Preserved pre-P16 values, for comparison (see
# results/p16/turbulence/gpu_equivalence/krylov_blocker/fix/prior_run/):
#
#   mesh      cells     x_r/H    E_x_r   y+ mean   out
#   coarse     1080    5.6461   0.0981    276.67    60
#   medium     2430    7.5829   0.2113    184.50     3
#   fine       4320    8.7700   0.4010    136.55     3
#
# A GREEN result here is itself a finding. So is any change in those numbers:
# the benchmark is deterministic, it runs on the CPU solver, and it failed the
# same way in the CUDA-OFF build, which does not compile the GPU solver at all.
# Reproducing the values above is what makes the GPU BiCGSTAB restart repair
# demonstrably INERT on this path. Differing values mean something moved and
# must be attributed before anything is concluded.
#
# DO NOT fix this in P16. DO NOT adjust its thresholds. It is deferred to P20.
#
# It costs about 30 minutes.
set -u
cd /mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
export PATH=/usr/local/cuda-12.9/bin:$PATH

CFG=${1:-final}
B="./build/$CFG/tests/integration/backward_facing_step/CFDBackwardFacingStepTests"
[ -x "$B" ] || { echo "not built -- run a build of build/$CFG first"; exit 1; }

echo "=== DEFERRED P20 BENCHMARK, build/$CFG (red is the expected outcome) ==="
timeout 5400 "$B" --gtest_filter='DriverSeegmiller.CorrectedMeshSequence' 2>&1 \
  | grep -v "^\[INFO\]" | tail -45
