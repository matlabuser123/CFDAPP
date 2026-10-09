#!/usr/bin/env bash
# P17.3 gate criteria G3 -- the P13-001 SIMPLE fingerprint must be BITWISE identical.
# Procedure copied verbatim from results/p17/compressible/eos/tools/run_gate.sh (P17.3's
# G7), which in turn uses P13-001's own probe. Not re-derived.
#
# The fingerprint is a DETERMINISTIC solve, so running it alongside another ctest job
# cannot change its bits -- only its wall time, which is not recorded as evidence.
set -uo pipefail
ROOT=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
P17=$ROOT/results/p17/compressible_continuity
B=build/qual-final
cd "$ROOT" || exit 1
export PATH="/usr/local/cuda-12.9/bin:$PATH"
mkdir -p "$P17/logs"

out=$P17/logs/20_fingerprint.log
{
echo "P17.3 -- G3: the P13-001 SIMPLE fingerprint"
echo "date: $(date -u +%FT%TZ)"
echo "probe: results/p13/restart/tools/solve_fingerprint.cpp (unmodified)"
echo "reference: results/p13/restart/logs/fingerprint-after-histories.log"
echo ""
JSON=$B/_deps/nlohmann_json-src/include
g++ -std=c++20 -O2 -DNDEBUG -fopenmp -I include -I "$B/generated/include" -I "$JSON" \
    -I /usr/local/cuda-12.9/include results/p13/restart/tools/solve_fingerprint.cpp \
    -o /tmp/p17_1_fp -Wl,--start-group "$B/src/libcfdcore.a" "$B/cuda/libcfdcuda.a" \
    -Wl,--end-group -L/usr/local/cuda-12.9/lib64 -lcudart 2>"$P17/logs/20_fp_build.log"
rc=$?
if [ $rc -ne 0 ]; then
  echo "  COMPILE FAILED (rc=$rc)"
  tail -12 "$P17/logs/20_fp_build.log" | sed 's/^/    /'
else
  # OMP_NUM_THREADS=1, matching the frozen execution policy. P17.3's own G7 ran this
  # probe WITHOUT the prefix; for a bitwise-identity claim that is the wrong instrument,
  # because OpenMP reduction order can depend on thread count. Adding it makes the
  # measurement thread-count independent -- a stricter instrument than the one inherited.
  # It also avoids the oversubscription thrash that stalled attempt 1 of this step
  # (25 OpenMP threads spinning against a concurrent ctest -j 8).
  OMP_NUM_THREADS=1 /tmp/p17_1_fp > "$P17/logs/20_fp_run.log" 2>&1
  echo "  cases: $(grep -c FINGERPRINT "$P17/logs/20_fp_run.log")"
  echo "  lines: $(grep -vc '^$' "$P17/logs/20_fp_run.log")"
  strip() { grep -v '^#' "$1" | grep -v '^$'; }
  if diff <(strip results/p13/restart/logs/fingerprint-after-histories.log) \
          <(strip "$P17/logs/20_fp_run.log") > "$P17/logs/20_fp_diff.txt" 2>&1; then
    echo "  BITWISE IDENTICAL -- P17.3 moved no bit of the incompressible solver"
    echo "  diff file is EMPTY and is preserved as evidence ($(wc -c < "$P17/logs/20_fp_diff.txt") bytes)"
  else
    echo "  *** DIFFERS -- G3 FAILED ***"
    head -20 "$P17/logs/20_fp_diff.txt" | sed 's/^/    /'
  fi
fi
echo ""
echo "### C2: the incompressible path must remain EOS-free (structural) ###"
echo "  EOS references under src/ + include/cfd/pressure_velocity: $(grep -rn 'EquationOfState\|IdealGas\|ThermodynamicProperties' src/pressure_velocity/ include/cfd/pressure_velocity/ 2>/dev/null | wc -l)   (must be 0)"
echo ""
echo "done: $(date -u +%FT%TZ)"
} 2>&1 | tee "$out"
