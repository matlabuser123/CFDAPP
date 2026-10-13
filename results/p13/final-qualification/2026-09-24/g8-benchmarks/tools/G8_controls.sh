#!/usr/bin/env bash
# P13 FINAL QUALIFICATION -- G8, the control half.
#
# Both frozen control scripts are run UNMODIFIED:
#   results/p13/benchmarks/tools/controls.sh        NV1..NV7
#   results/p13/benchmarks/tools/controls_rerun.sh  NV2/NV4/NV5/NV6, re-run with a
#                                                   freshly recorded baseline
#
# controls_rerun.sh exists because NV2 and NV4 once reported DETECTED without the
# comparator ever reaching the regression logic: the baseline predated amendment
# A1 and carried no `loaded` key, so the environment check refused every
# comparison. NV6 -- which must STOP failing when the threshold is raised -- is
# what exposed it. Both are run here so the reason for each detection is visible,
# not just the verdict.
#
# This wrapper keeps its own pre-control hashes and re-checks them afterwards.
set -uo pipefail
ROOT=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
G8=$ROOT/results/p13/final-qualification/2026-09-24/g8-benchmarks
L=$G8/logs
cd "$ROOT"
mkdir -p "$L"
export PATH="/usr/local/cuda-12.9/bin:$PATH"

WATCHED="benchmarks/BenchmarkOutput.hpp scripts/benchmarks/run_suite.sh scripts/benchmarks/compare_benchmarks.py"
PRE=/tmp/g8_wrapper_pre.sha256
sha256sum $WATCHED > "$PRE"
git ls-files results/performance benchmarks > /tmp/g8_immutable_list.txt
sha256sum $(cat /tmp/g8_immutable_list.txt) > /tmp/g8_immutable_pre.sha256

log=$L/G8_controls.log
{
echo "P13 G8 -- control half"
echo "date: $(date -u +%FT%TZ)"
echo ""
echo "### this wrapper's independent pre-control hashes ###"
sed 's/^/  /' "$PRE"
echo "  plus $(wc -l < /tmp/g8_immutable_list.txt) tracked evidence files"
echo ""
echo "### machine state before the controls ###"
echo "  cores=$(nproc) loadavg=$(cut -d' ' -f1 /proc/loadavg) limit=$(awk -v c=$(nproc) 'BEGIN{printf "%.2f", c*0.25}')"
echo ""

echo "############ controls_rerun.sh -- NV2/NV4/NV5/NV6 with reason checking ############"
bash results/p13/benchmarks/tools/controls_rerun.sh 2>&1 | sed 's/^/  /'
echo "  controls_rerun.sh rc=${PIPESTATUS[0]}"
echo ""

echo "### wrapper restore check ###"
sha256sum -c "$PRE" 2>&1 | sed 's/^/  /'
echo ""

echo "############ controls.sh -- NV1..NV7 ############"
bash results/p13/benchmarks/tools/controls.sh 2>&1 | sed 's/^/  /'
echo "  controls.sh rc=${PIPESTATUS[0]}"
echo ""

echo "### FINAL wrapper restore check ###"
sha256sum -c "$PRE" 2>&1 | sed 's/^/  /'
echo ""
echo "### tracked evidence still byte-identical? ###"
if sha256sum -c /tmp/g8_immutable_pre.sha256 --quiet > "$L/G8_immutable_after_controls.log" 2>&1; then
  echo "  tracked evidence changed files = 0  -- all $(wc -l < /tmp/g8_immutable_list.txt) byte-identical"
else
  echo "  *** TRACKED EVIDENCE CHANGED ***"; sed 's/^/    /' "$L/G8_immutable_after_controls.log"
fi
echo ""
echo "### control markers left behind? ###"
grep -rn 'NV[0-9]' benchmarks/BenchmarkOutput.hpp scripts/benchmarks/ 2>/dev/null | sed 's/^/  /' || echo "  none -- clean"
echo ""
echo "### repo-root strays ###"
find . -maxdepth 1 \( -name '*.csv' -o -name 'benchmark_*.json' \) -printf '  STRAY: %p\n' 2>/dev/null || true
[ -z "$(find . -maxdepth 1 \( -name '*.csv' -o -name 'benchmark_*.json' \) 2>/dev/null)" ] && echo "  none"
echo ""
echo "done: $(date -u +%FT%TZ)"
} 2>&1 | tee "$log"
