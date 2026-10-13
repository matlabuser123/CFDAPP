#!/usr/bin/env bash
# P13-001 -- Restart / checkpointing. One phase per invocation.
#
#   usage: run.sh <phase> [args...]
#
#   fingerprint  the PRESERVATION instrument: a bitwise fingerprint of a fixed
#                suite of default (no-resume) SIMPLE solves. Run before the
#                change, run after, require identity.
#   equiv        continuous vs checkpoint/resume equivalence
#   negative     the rejection matrix (corrupt / wrong mesh / wrong case / ...)
#   cli          the production CLI checkpoint + resume path
#   controls     the non-vacuity controls
#
# Every phase STOPS at its first failed check.
set -uo pipefail
ROOT=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
E=$ROOT/results/p13/restart
T=$E/tools
L=$E/logs
FINAL=$ROOT/build/final
CUDA=/usr/local/cuda-12.9
cd "$ROOT"
mkdir -p "$L" "$E/data"

phase=${1:-}
shift || true

# Build one harness against build/final and run it. Fails closed: a build
# failure is a phase failure, never a skipped check.
build_run() {  # build_run <source> <output-log> [args...]
  local src=$1 log=$2
  shift 2
  local exe="/tmp/p13_$(basename "$src" .cpp)"
  /usr/bin/c++ -I"$ROOT/include" -I"$FINAL/generated/include" \
    -I"$FINAL/_deps/nlohmann_json-src/include" -isystem "$CUDA/include" \
    -O2 -DNDEBUG -std=c++20 -Wall -Wextra \
    -o "$exe" "$src" \
    -Wl,--start-group "$FINAL/cuda/libcfdcuda.a" "$FINAL/src/libcfdcore.a" -Wl,--end-group \
    -L"$CUDA/lib64" -lcudart > "$log.build" 2>&1
  local brc=$?
  if [ $brc -ne 0 ]; then
    echo "BUILD FAILED: $src"
    tail -40 "$log.build"
    return 1
  fi
  { echo "# $(basename "$src") $*"
    echo "# date: $(date -u '+%Y-%m-%dT%H:%M:%SZ')"
    echo "# git HEAD: $(git -C "$ROOT" rev-parse HEAD)"
    echo "# libcfdcuda.a: $(sha256sum "$FINAL/cuda/libcfdcuda.a" | cut -d' ' -f1)"
    echo "# libcfdcore.a: $(sha256sum "$FINAL/src/libcfdcore.a" | cut -d' ' -f1)"
    echo "# SIMPLE.cpp:   $(sha256sum "$ROOT/src/pressure_velocity/SIMPLE.cpp" | cut -d' ' -f1)"
    echo ""; } > "$log"
  "$exe" "$@" >> "$log" 2>&1
  local rc=$?
  echo "# exit code: $rc" >> "$log"
  return $rc
}

rebuild() {
  echo "=== rebuilding build/final (Release + CUDA) ==="
  cmake --build "$FINAL" -j20 2>&1 | tail -3 || return 1
}

case "$phase" in
  fingerprint)
    tag=${1:-current}
    rebuild || exit 1
    build_run "$T/solve_fingerprint.cpp" "$L/fingerprint-$tag.log" || exit 1
    grep -c FINGERPRINT "$L/fingerprint-$tag.log" | sed 's/^/cases fingerprinted: /'
    tail -n +8 "$L/fingerprint-$tag.log"
    ;;
  equiv)
    rebuild || exit 1
    build_run "$T/restart_equivalence.cpp" "$L/equivalence.log" "$@" || exit 1
    tail -40 "$L/equivalence.log"
    ;;
  negative)
    rebuild || exit 1
    build_run "$T/restart_negative.cpp" "$L/negative.log" "$@" || exit 1
    tail -60 "$L/negative.log"
    ;;
  cli)
    rebuild || exit 1
    bash "$T/cli_checkpoint.sh" "$@" 2>&1 | tee "$L/cli.log"
    exit "${PIPESTATUS[0]}"
    ;;
  controls)
    rebuild || exit 1
    bash "$T/controls.sh" "$@" 2>&1 | tee "$L/controls.log"
    exit "${PIPESTATUS[0]}"
    ;;
  *)
    echo "usage: run.sh {fingerprint|equiv|negative|cli|controls} [args...]"
    exit 2
    ;;
esac
