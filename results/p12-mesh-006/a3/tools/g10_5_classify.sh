#!/usr/bin/env bash
# P12-MESH-006 G10.5: classify every regenerated output (results/, cases/*/results/,
# tests/data/cases/*/results/; this phase's own evidence excluded) against BASE ($HOME/m6ref/base, the
# pre-MESH-006 working tree), after every test run of the phase (focused, full regression, ASan).
#   pass 1: classification only                                   -> a3/logs/19_g10_5_generated_outputs.log
#   pass 2: --restore (RUNTIME-ONLY files copied back from BASE;  -> a3/logs/19b_g10_5_restore.log
#           VALUES and NEW never touched) -- only if pass 1 has no VALUES file
#   pass 3: classification again (expected: RUNTIME-ONLY 0)      -> a3/logs/19c_g10_5_after_restore.log
R=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
BT=$HOME/m6ref/base
L=$R/results/p12-mesh-006/a3/logs
TOOL=$R/results/p12-mesh-006/a3/tools/classify_generated_outputs.py
cd $R
{
  echo "# P12-MESH-006 G10.5 generated-output classification vs BASE ($BT), $(date -u +%Y-%m-%dT%H:%M:%SZ)"
  echo "# after: focused tests (logs/15), full regression Release/Debug+GUI/ASan (logs/16-18), ASan reruns (18a, 18b)"
  python3 $TOOL $BT
} > $L/19_g10_5_generated_outputs.log 2>&1
tail -1 $L/19_g10_5_generated_outputs.log
if grep -q "^VALUES" $L/19_g10_5_generated_outputs.log; then
  echo "VALUES differences present: not restoring; stop and inspect"
  exit 1
fi
{
  echo "# P12-MESH-006 G10.5 restore of RUNTIME-ONLY files from BASE, $(date -u +%Y-%m-%dT%H:%M:%SZ)"
  python3 $TOOL $BT --restore
} > $L/19b_g10_5_restore.log 2>&1
grep -E "^restored" $L/19b_g10_5_restore.log
{
  echo "# P12-MESH-006 G10.5 classification after the restore, $(date -u +%Y-%m-%dT%H:%M:%SZ)"
  python3 $TOOL $BT
} > $L/19c_g10_5_after_restore.log 2>&1
tail -1 $L/19c_g10_5_after_restore.log
