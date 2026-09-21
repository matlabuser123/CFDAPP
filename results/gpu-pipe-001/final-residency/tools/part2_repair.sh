#!/usr/bin/env bash
# GPU-PIPE-001 Final Residency, Part 2 repair.
#
# WHY THIS EXISTS, stated plainly: this session launched a SECOND copy of the
# resident-pressure-solve finalise script while the previous session's copy was
# still running. Both write the same evidence files. The duplicate was stopped
# within two minutes, but it had already truncated
#
#     results/gpu-pipe-001/gpu-resident-pressure-solve/regression/known-debt.log
#
# to zero bytes -- the known-BiCGSTAB-debt probe's output for that gate. The
# probe's exit code had already been recorded by the original run; only the log
# was lost. It is re-run here, against the SAME binaries, and the log records
# both the re-run and the reason for it.
#
# Nothing else was damaged: the CUDA diagnostics logs (00:24-00:26) and the
# CPU-backend log predate the duplicate, and the performance and regression
# phases had not started.
set -uo pipefail
ROOT=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
E=$ROOT/results/gpu-pipe-001/gpu-resident-pressure-solve
F=$ROOT/results/gpu-pipe-001/final-residency
BUILD=$ROOT/build/final
CUDA=/usr/local/cuda-12.9
cd "$ROOT"

echo "=== 1. preserve this generation's benchmark CSV before anything overwrites it ==="
# performance_benchmark.cpp writes to a hardcoded path under
# results/gpu-disc-001/performance/raw/, so the next phase to run it would
# destroy this one. See performance/generations/README.md.
mkdir -p "$F/performance/generations"
cp "$ROOT/results/gpu-disc-001/performance/raw/runs-cavity.csv" \
   "$F/performance/generations/gen25-resident-pressure-runs-cavity.csv"
cp "$E/performance/cavity.log" \
   "$F/performance/generations/gen25-resident-pressure-cavity.log"
sha256sum "$F/performance/generations/gen25-resident-pressure-runs-cavity.csv" | sed 's/^/  /'
wc -l "$F/performance/generations/gen25-resident-pressure-runs-cavity.csv" | sed 's/^/  /'

echo ""
echo "=== 2. re-run the known-debt probe, same binaries ==="
sha256sum "$BUILD/cuda/libcfdcuda.a" "$BUILD/src/libcfdcore.a" | sed 's/^/  /'
/usr/bin/c++ -I"$ROOT/include" -I"$BUILD/generated/include" \
  -I"$BUILD/_deps/nlohmann_json-src/include" -isystem "$CUDA/include" \
  -O2 -DNDEBUG -std=c++20 -o /tmp/repair_debt \
  "$ROOT/results/gpu-disc-001/full-regression/tools/known_debt_probe.cpp" \
  -Wl,--start-group "$BUILD/cuda/libcfdcuda.a" "$BUILD/src/libcfdcore.a" -Wl,--end-group \
  -L"$CUDA/lib64" -lcudart || exit 1
{
  echo "# known GPU BiCGSTAB restart asymmetry -- RE-RUN"
  echo "#"
  echo "# The original run of this probe (2026-09-21, within the resident-pressure-solve"
  echo "# finalise script) completed and its exit code was recorded. Its LOG was then"
  echo "# truncated to zero bytes by a duplicate launch of the same script from a second"
  echo "# session. This is a re-run against the same binaries, recorded rather than"
  echo "# presented as the original."
  echo "#"
  echo "# date: $(date -u '+%Y-%m-%dT%H:%M:%SZ')"
  echo "# libcfdcuda.a: $(sha256sum "$BUILD/cuda/libcfdcuda.a" | cut -d' ' -f1)"
  echo "# libcfdcore.a: $(sha256sum "$BUILD/src/libcfdcore.a" | cut -d' ' -f1)"
  echo ""
} > "$E/regression/known-debt.log"
/tmp/repair_debt >> "$E/regression/known-debt.log" 2>&1
rc=$?
echo "# exit code: $rc" >> "$E/regression/known-debt.log"
cp "$E/regression/known-debt.log" "$F/known-bicgstab/gate-a-known-debt.log"
grep -E "KNOWN DEBT|status|iterations|breakdown|PASS|FAIL" "$E/regression/known-debt.log" \
  | tail -14 | sed 's/^/  /'
echo "  -> rc=$rc"
exit $rc
