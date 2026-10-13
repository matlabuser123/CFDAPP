#!/usr/bin/env bash
# P17.4 gate criteria G3 / L3 -- the P13-001 SIMPLE fingerprint must be BITWISE identical.
#
# Procedure copied VERBATIM from results/p17/compressible_continuity/tools/run_fingerprint.sh,
# which copied it from P17.2's G7, which uses P13-001's own unmodified probe. Not re-derived:
# reusing the instrument is what makes "bitwise identical" comparable across P17.1 .. P17.4.
# Only the phase label and output paths differ.
#
# OMP_NUM_THREADS=1, matching the frozen execution policy. For a bitwise-identity claim the
# prefix is not optional -- OpenMP reduction order can depend on thread count, so without it the
# instrument is thread-count dependent. It also avoids the oversubscription thrash that stalled a
# P17.1 probe for 13 minutes (25 OpenMP threads spinning against a concurrent ctest -j 8); the
# same probe finished in 12 seconds single-threaded.
set -uo pipefail
ROOT=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
P17=$ROOT/results/p17/compressible_momentum
B=build/qual-final
cd "$ROOT" || exit 1
export PATH="/usr/local/cuda-12.9/bin:$PATH"
mkdir -p "$P17/logs"

out=$P17/logs/13_fingerprint.log
{
echo "P17.4 -- G3/L3: the P13-001 SIMPLE fingerprint"
echo "date: $(date -u +%FT%TZ)"
echo "probe: results/p13/restart/tools/solve_fingerprint.cpp (unmodified)"
echo "reference: results/p13/restart/logs/fingerprint-after-histories.log"
echo "P17.3 recorded an EMPTY diff against the same reference; this must too."
echo ""
JSON=$B/_deps/nlohmann_json-src/include
g++ -std=c++20 -O2 -DNDEBUG -fopenmp -I include -I "$B/generated/include" -I "$JSON" \
    -I /usr/local/cuda-12.9/include results/p13/restart/tools/solve_fingerprint.cpp \
    -o /tmp/p17_4_fp -Wl,--start-group "$B/src/libcfdcore.a" "$B/cuda/libcfdcuda.a" \
    -Wl,--end-group -L/usr/local/cuda-12.9/lib64 -lcudart 2>"$P17/logs/13_fp_build.log"
rc=$?
if [ $rc -ne 0 ]; then
  echo "  COMPILE FAILED (rc=$rc)"
  tail -12 "$P17/logs/13_fp_build.log" | sed 's/^/    /'
else
  OMP_NUM_THREADS=1 /tmp/p17_4_fp > "$P17/logs/13_fp_run.log" 2>&1
  echo "  cases: $(grep -c FINGERPRINT "$P17/logs/13_fp_run.log")"
  echo "  lines: $(grep -vc '^$' "$P17/logs/13_fp_run.log")"
  strip() { grep -v '^#' "$1" | grep -v '^$'; }
  if diff <(strip results/p13/restart/logs/fingerprint-after-histories.log) \
          <(strip "$P17/logs/13_fp_run.log") > "$P17/logs/13_fp_diff.txt" 2>&1; then
    echo "  BITWISE IDENTICAL -- P17.4 moved no bit of the incompressible solver"
    echo "  diff file is EMPTY and is preserved as evidence ($(wc -c < "$P17/logs/13_fp_diff.txt") bytes)"
  else
    echo "  *** DIFFERS -- G3 FAILED ***"
    head -20 "$P17/logs/13_fp_diff.txt" | sed 's/^/    /'
  fi
fi
echo ""
echo "### L4: with the dilatational option OFF, the compressible path is unchanged too ###"
echo "  The option defaults to false and DefaultIsDisabledBehaviourally pins that as behaviour,"
echo "  not as a header string. The fingerprint above covers the INCOMPRESSIBLE solver; the"
echo "  compressible suite result in logs/12_gate_4b.log covers the compressible one."
echo ""
echo "### the incompressible path must remain EOS-free (structural) ###"
echo "  EOS references under src/ + include/cfd/pressure_velocity: $(grep -rn 'EquationOfState\|IdealGas\|ThermodynamicProperties' src/pressure_velocity/ include/cfd/pressure_velocity/ 2>/dev/null | wc -l)   (must be 0)"
echo ""
echo "done: $(date -u +%FT%TZ)"
} 2>&1 | tee "$out"
