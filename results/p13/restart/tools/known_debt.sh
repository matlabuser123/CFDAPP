#!/usr/bin/env bash
# P13-001 gate, G6 -- the recorded GPU BiCGSTAB restart asymmetry, re-run on
# THIS item's binaries with the project's own already-qualified probe.
#
# CLAUDE.md section 8: "a known failure stays known only while it reproduces
# its recorded values exactly". The recorded value is
# PressureCorrectionFailure at 1845 iterations (TODO.md -> Technical Debt).
# Anything else is a regression, including an accidental improvement.
set -uo pipefail
ROOT=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
L=$ROOT/results/p13/restart/logs
BUILD=$ROOT/build/final
CUDA=/usr/local/cuda-12.9
cd "$ROOT"
/usr/bin/c++ -I include -I "$BUILD/generated/include" \
  -I "$BUILD/_deps/nlohmann_json-src/include" -isystem "$CUDA/include" \
  -O2 -DNDEBUG -std=c++20 -o /tmp/p13_known_debt \
  "$ROOT/results/gpu-disc-001/full-regression/tools/known_debt_probe.cpp" \
  -Wl,--start-group "$BUILD/cuda/libcfdcuda.a" "$BUILD/src/libcfdcore.a" -Wl,--end-group \
  -L"$CUDA/lib64" -lcudart || exit 1
{ echo "# known_debt_probe on the P13-001 binaries"
  echo "# date: $(date -u '+%Y-%m-%dT%H:%M:%SZ')"
  echo "# libcfdcuda.a: $(sha256sum "$BUILD/cuda/libcfdcuda.a" | cut -d' ' -f1)"
  echo "# libcfdcore.a: $(sha256sum "$BUILD/src/libcfdcore.a" | cut -d' ' -f1)"
  echo "# SIMPLE.cpp:   $(sha256sum src/pressure_velocity/SIMPLE.cpp | cut -d' ' -f1)"
  echo ""; } > "$L/G6_known_debt.log"
/tmp/p13_known_debt >> "$L/G6_known_debt.log" 2>&1
rc=$?
echo "# exit code: $rc" >> "$L/G6_known_debt.log"
sed -n '7,$p' "$L/G6_known_debt.log" | sed 's/^/  /'
exit $rc
