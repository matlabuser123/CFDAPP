#!/usr/bin/env bash
# P13-007 -- the production benchmark suite driver.
#
# Before this there were seven benchmark executables and no suite: nothing ran
# them together, nothing compared them to anything, and nothing returned a
# verdict (results/p13/benchmarks/audit.md, B3/B4). Each wrote to a path
# relative to its working directory, so running one from the repository root
# overwrote committed evidence -- 34 tracked files across five directories.
#
# This driver:
#   * sends every benchmark's output into ONE directory it owns, via
#     --output-dir, so a run never touches the repository's evidence;
#   * captures the environment beside the results, because a timing without the
#     machine and build it came from cannot be compared to anything later;
#   * compares against a recorded baseline and EXITS NONZERO on a severe
#     regression.
#
# Usage:
#   scripts/benchmarks/run_suite.sh --output-dir <dir> [--build <dir>]
#                                   [--baseline <dir>] [--quick]
#
#   --quick    only the short CPU benchmark (about 25 s); the default runs the
#              GPU benchmarks too, which take considerably longer.
set -uo pipefail

BUILD_DIR="build/final"
OUTPUT_DIR=""
BASELINE_DIR=""
QUICK=0
ALLOW_LOADED=0

while [ $# -gt 0 ]; do
  case "$1" in
    --output-dir) OUTPUT_DIR="${2:?--output-dir requires a path}"; shift 2 ;;
    --build)      BUILD_DIR="${2:?--build requires a path}"; shift 2 ;;
    --baseline)   BASELINE_DIR="${2:?--baseline requires a path}"; shift 2 ;;
    --quick)      QUICK=1; shift ;;
    --allow-loaded) ALLOW_LOADED=1; shift ;;
    -h|--help)    sed -n '2,30p' "$0"; exit 0 ;;
    *) echo "Error: unrecognized argument \"$1\"" >&2; exit 2 ;;
  esac
done

if [ -z "$OUTPUT_DIR" ]; then
  echo "Error: --output-dir <dir> is required." >&2
  echo "       It is required rather than defaulted on purpose: a default would" >&2
  echo "       put a run back on top of the repository's committed evidence," >&2
  echo "       which is the defect this suite exists to close." >&2
  exit 2
fi

mkdir -p "$OUTPUT_DIR" || exit 2
OUTPUT_DIR=$(cd "$OUTPUT_DIR" && pwd)
REPO=$(cd "$(dirname "$0")/../.." && pwd)
cd "$REPO" || exit 2

# --- refuse to measure on a busy machine -------------------------------------
# Measured, and the reason this guard exists: the SAME binary recorded on this
# host four times gave 20x20 runtime_per_iteration_ms of 1.5030, 1.5358, 2.0648
# and 1.8631 ms -- a 1.37x spread, with the slow pair recorded while builds ran
# concurrently. Five runs back to back on a quiet machine spread only 3.5-7.9%.
#
# So the variable that dominates is machine load, not the code. The honest fix
# is to control it rather than to widen the tolerance until a loaded run fits:
# a threshold loose enough to tolerate 1.37x could not detect a 30% regression.
LOAD1=$(cut -d' ' -f1 /proc/loadavg 2>/dev/null || echo 0)
CORES=$(nproc 2>/dev/null || echo 1)
LOAD_LIMIT=$(awk -v c="$CORES" 'BEGIN{printf "%.2f", c*0.25}')
if [ "$ALLOW_LOADED" -eq 0 ] &&    awk -v l="$LOAD1" -v m="$LOAD_LIMIT" 'BEGIN{exit !(l>m)}'; then
  echo "Error: this machine is too busy to measure." >&2
  echo "       1-minute load average $LOAD1 exceeds $LOAD_LIMIT (25% of $CORES cores)." >&2
  echo "       A timing recorded under load is not comparable to one recorded quiet:" >&2
  echo "       the same binary measured 1.37x apart on this host for that reason." >&2
  echo "       Wait for the machine to settle, or pass --allow-loaded to record a" >&2
  echo "       run that is explicitly marked as not comparable." >&2
  exit 2
fi

echo "CFDApp benchmark suite"
echo "  build     : $BUILD_DIR"
echo "  output    : $OUTPUT_DIR"
echo "  mode      : $([ $QUICK -eq 1 ] && echo quick || echo full)"
echo

# --- environment, captured beside the results ------------------------------
# A timing with no record of the machine and build that produced it cannot be
# compared to anything later -- which is exactly why the committed baseline
# (0.1.0-dev, OpenMP off, CUDA off) is unusable against a current build.
ENV_FILE="$OUTPUT_DIR/suite_environment.txt"
{
  echo "captured: $(date -u +%FT%TZ)"
  echo "host    : $(uname -srm)"
  echo "cpu     : $(grep -m1 'model name' /proc/cpuinfo 2>/dev/null | cut -d: -f2- | sed 's/^ //')"
  echo "cores   : $(nproc 2>/dev/null)"
  echo "memory  : $(free -h 2>/dev/null | awk '/^Mem:/{print $2}')"
  echo "compiler: $(g++ --version 2>/dev/null | head -1)"
  echo "cfdapp  : $("$BUILD_DIR/apps/cli/cfdapp" --version 2>/dev/null)"
  echo "nvidia  : $(nvidia-smi --query-gpu=name,driver_version --format=csv,noheader 2>/dev/null | head -1)"
  echo "loadavg : $(cat /proc/loadavg 2>/dev/null)"
  echo "loaded  : $([ $ALLOW_LOADED -eq 1 ] && echo \"yes -- NOT COMPARABLE, recorded with --allow-loaded\" || echo no)"
} > "$ENV_FILE"
sed 's/^/  /' "$ENV_FILE"
echo

# --- run ---------------------------------------------------------------------
declare -a BENCHMARKS
BENCHMARKS+=("benchmarks/cpu/cfd_benchmark_cpu")
if [ $QUICK -eq 0 ]; then
  BENCHMARKS+=("benchmarks/gpu/cfd_benchmark_cuda_end_to_end")
  BENCHMARKS+=("benchmarks/gpu/cfd_benchmark_gpu_preconditioner")
fi

FAILED=0
for rel in "${BENCHMARKS[@]}"; do
  bin="$BUILD_DIR/$rel"
  name=$(basename "$rel")
  if [ ! -x "$bin" ]; then
    # Not silently skipped: a suite that quietly runs fewer benchmarks than it
    # claims still reports green.
    echo "  $name: MISSING ($bin) -- not built in this configuration"
    FAILED=$((FAILED + 1))
    continue
  fi
  start=$(date +%s)
  if "$bin" --output-dir "$OUTPUT_DIR" > "$OUTPUT_DIR/$name.log" 2>&1; then
    echo "  $name: ok ($(( $(date +%s) - start )) s)"
  else
    rc=$?
    echo "  $name: FAILED (exit $rc) -- see $OUTPUT_DIR/$name.log"
    FAILED=$((FAILED + 1))
  fi
done

if [ $FAILED -gt 0 ]; then
  echo
  echo "SUITE FAILED: $FAILED benchmark(s) did not run successfully."
  exit 1
fi

# --- compare -----------------------------------------------------------------
if [ -n "$BASELINE_DIR" ]; then
  echo
  echo "=== regression check against $BASELINE_DIR ==="
  python3 "$REPO/scripts/benchmarks/compare_benchmarks.py" \
    --baseline "$BASELINE_DIR" --current "$OUTPUT_DIR"
  rc=$?
  [ $rc -ne 0 ] && echo && echo "SUITE FAILED: performance regression." && exit $rc
else
  echo
  echo "no --baseline given: results recorded, nothing compared."
  echo "  (that is not a pass -- pass --baseline <dir> for a regression verdict)"
fi

echo
echo "suite complete: $OUTPUT_DIR"
