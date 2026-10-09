#!/usr/bin/env bash
# P13 FINAL QUALIFICATION -- G6, the control half.
#
# The three frozen control scripts are run UNMODIFIED and in sequence:
#   results/p13/diagnostics/tools/controls.sh    N1 (C1), N2 (V4), N3, N4
#   results/p13/diagnostics/tools/controls2.sh   N1b (C2b)
#   results/p13/diagnostics/tools/control_N5.sh  N5
# Only their output is redirected here, so the committed P13-005 logs are not
# overwritten.
#
# All four patch anchors were verified present and unique BEFORE anything ran
# (a control whose patch does not apply reports nothing at all):
#   src/core/Logger.cpp        bool targetsStderr(LogLevel) noexcept ...   1
#   apps/cli/main.cpp          setLevel(cfd::LogLevel::Warning)            1
#   src/app/ProjectRunner.cpp  "solver start: "                            1
#   tests/cmake/RunCliStdoutOnly.cmake                                present
#
# Each frozen script verifies its own restore by sha256. This wrapper verifies
# it AGAIN afterwards, independently, because a script that restores and checks
# with the same broken assumption would report success either way.
set -uo pipefail
ROOT=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
G6=$ROOT/results/p13/final-qualification/2026-09-24/g6-diagnostics
L=$G6/logs
cd "$ROOT"
mkdir -p "$L"
export PATH="/usr/local/cuda-12.9/bin:$PATH"

WATCHED="src/core/Logger.cpp apps/cli/main.cpp src/app/ProjectRunner.cpp tests/cmake/RunCliStdoutOnly.cmake"
PRE=/tmp/g6_wrapper_pre.sha256
sha256sum $WATCHED > "$PRE"

log=$L/G6_controls.log
{
echo "P13 G6 -- control half"
echo "date: $(date -u +%FT%TZ)"
echo ""
echo "### independent pre-control hashes (this wrapper's own record) ###"
cat "$PRE" | sed 's/^/  /'
echo ""

echo "############ controls.sh -- N1 (C1), N2 (V4), N3, N4 ############"
bash results/p13/diagnostics/tools/controls.sh 2>&1 | sed 's/^/  /'
echo "  controls.sh rc=${PIPESTATUS[0]}"
echo ""

echo "### wrapper restore check after controls.sh ###"
sha256sum -c "$PRE" 2>&1 | sed 's/^/  /'
echo ""

echo "############ controls2.sh -- N1b (C2b) ############"
bash results/p13/diagnostics/tools/controls2.sh 2>&1 | sed 's/^/  /'
echo "  controls2.sh rc=${PIPESTATUS[0]}"
echo ""

echo "### wrapper restore check after controls2.sh ###"
sha256sum -c "$PRE" 2>&1 | sed 's/^/  /'
echo ""

echo "############ control_N5.sh -- the CLI stdout-only ctest ############"
bash results/p13/diagnostics/tools/control_N5.sh 2>&1 | sed 's/^/  /'
echo "  control_N5.sh rc=${PIPESTATUS[0]}"
echo ""

echo "### FINAL wrapper restore check -- all four watched files ###"
sha256sum -c "$PRE" 2>&1 | sed 's/^/  /'
echo ""
echo "### control markers left in the tree? ###"
grep -rn 'N1 CONTROL\|N2 CONTROL\|N4 CONTROL\|N5 CONTROL' src/ apps/ tests/ 2>/dev/null | sed 's/^/  /' \
  || echo "  none -- clean"
echo ""
echo "### the tree is back: C2/V4/C2b re-measured at baseline ###"
cmake --build build/final -j20 > /dev/null 2>&1 || echo "  BASELINE REBUILD FAILED"
O=/tmp/g6_after; rm -rf $O
bash results/p13/diagnostics/tools/cli_output_corpus.sh $O > /dev/null 2>&1
BASE=results/p13/diagnostics/data/baseline-output
ds=0; de=0
for f in "$BASE"/*.stdout; do diff -q "$f" "$O/$(basename "$f")" >/dev/null 2>&1 || ds=$((ds+1)); done
for f in "$BASE"/*.stderr; do diff -q "$f" "$O/$(basename "$f")" >/dev/null 2>&1 || de=$((de+1)); done
echo "  C2 stdout differing: $ds / 7    V4 stderr differing: $de / 7   (0 required)"
bash results/p13/diagnostics/tools/verify_c2b.sh 2>&1 | tail -1 | sed 's/^/  /'
echo ""
echo "done: $(date -u +%FT%TZ)"
} 2>&1 | tee "$log"
