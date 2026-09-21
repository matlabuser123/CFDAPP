#!/usr/bin/env bash
# GPU-PIPE-001 Phase 2, Gate 2: GPU linear solvers, pressure correction and the
# GPU-PCORR-001 regression, on the CUDA-enabled build.
set -u
cd /mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp

fail=0
run() { # path  label
  local bin="$1" label="$2"
  if [ ! -x "$bin" ]; then echo "  SKIP  $label (not built)"; return; fi
  "$bin" > "/tmp/gate2_$(basename "$bin").log" 2>&1
  local rc=$?
  local line
  line=$(grep -E '^\[==========\].*ran\.' "/tmp/gate2_$(basename "$bin").log" | tail -1)
  local passed
  passed=$(grep -cE '^\[       OK \]' "/tmp/gate2_$(basename "$bin").log")
  local failed
  failed=$(grep -cE '^\[  FAILED  \].*\.' "/tmp/gate2_$(basename "$bin").log")
  if [ "$rc" -eq 0 ]; then
    printf '  PASS  %-22s %s\n' "$label" "$line"
  else
    printf '  FAIL  %-22s rc=%d  (%s passed, %s failed lines)\n' "$label" "$rc" "$passed" "$failed"
    grep -E '^\[  FAILED  \]' "/tmp/gate2_$(basename "$bin").log" | head -8 | sed 's/^/          /'
    fail=1
  fi
}

echo "=== Gate 2: solver-level ==="
run build/cuda/tests/unit/gpu/CFDGpuTests            "GPU unit"
run build/cuda/tests/unit/algebra/CFDAlgebraTests    "algebra (CG/BiCGSTAB)"
run build/cuda/tests/solver/simple/CFDSimpleTests    "SIMPLE (incl. GPU)"

echo
echo "=== GPU-PCORR-001 specific regression ==="
if [ -x build/cuda/tests/solver/simple/CFDSimpleTests ]; then
  build/cuda/tests/solver/simple/CFDSimpleTests --gtest_filter='*Gpu*:*GPU*' \
    > /tmp/gate2_pcorr.log 2>&1
  rc=$?
  tail -3 /tmp/gate2_pcorr.log | sed 's/^/  /'
  [ "$rc" -ne 0 ] && fail=1
fi

echo
[ "$fail" -eq 0 ] && echo "GATE 2: PASS" || echo "GATE 2: FAIL"
exit "$fail"
