#!/usr/bin/env bash
# S1/S2/E1 -- the driver runs the suite into one directory it owns, captures
# the environment, and records this machine's baseline for the current build.
ROOT=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
cd $ROOT
R=$ROOT/results/p13/benchmarks
BASE=$R/data/baseline-linux-x64

echo "=== O2: are the repository's tracked results untouched by a suite run? ==="
BEFORE=$(git status --porcelain -- results/performance cases/ | wc -l)
echo "  tracked-file changes before: $BEFORE"

echo
echo "=== S1/S2/E1: record the baseline (quick mode) ==="
rm -rf $BASE
bash scripts/benchmarks/run_suite.sh --quick --output-dir $BASE 2>&1 | sed 's/^/  /'
echo "  driver exit: ${PIPESTATUS[0]}"

echo
AFTER=$(git status --porcelain -- results/performance cases/ | wc -l)
echo "  tracked-file changes after : $AFTER"
if [ "$BEFORE" -eq "$AFTER" ]; then echo "  O2: PASS -- the suite touched no tracked evidence"
else echo "  O2: FAIL -- $((AFTER-BEFORE)) tracked file(s) changed"; git status --porcelain -- results/performance cases/; fi

echo
echo "=== R3 via the driver: a fresh run against the recorded baseline ==="
RUN=/tmp/p13_suite_run; rm -rf $RUN
bash scripts/benchmarks/run_suite.sh --quick --output-dir $RUN --baseline $BASE 2>&1 | tail -14 | sed 's/^/  /'
echo "  driver exit: ${PIPESTATUS[0]}"

echo
echo "=== S1: the driver refuses to run without --output-dir ==="
bash scripts/benchmarks/run_suite.sh --quick > /tmp/p13_noout.log 2>&1
echo "  exit: $?"
head -4 /tmp/p13_noout.log | sed 's/^/    /'
