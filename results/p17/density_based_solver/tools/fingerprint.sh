#!/usr/bin/env bash
# P17.8 criterion A3 (frozen gate Group A) and gate G1 -- the P13-001 SIMPLE fingerprint must be
# BITWISE identical.
#
# The procedure is copied VERBATIM from results/p17/hllc/tools/fingerprint.sh, which copied it from
# P17.6/P17.3/P17.2, all of which use P13-001's own unmodified probe. Reusing the instrument
# unchanged is what makes "bitwise identical" comparable across P17.1 .. P17.8; only the phase label
# and the output paths differ.
#
# AND THE PART EVERY EARLIER P17 PHASE LEFT OUT. G1's own text says:
#     "the fingerprint instrument must be shown able to FAIL: P13 proved this by planting a defect.
#      Any P17 item that claims G1 must show the instrument detecting a deliberate perturbation,
#      not merely reporting 'identical'."
# No P17 phase before this one ran that demonstration -- a grep for a perturbation across
# results/p17/*/tools/fingerprint.sh finds none. So section 2 below plants a one-character change in
# the SIMPLE velocity relaxation, rebuilds the core library, and requires the diff to become
# NON-EMPTY. Without it, "identical" is a number with no demonstrated ability to be anything else.
set -uo pipefail
ROOT=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
P17=$ROOT/results/p17/density_based_solver
B=build/qual-final
SIMPLE=src/pressure_velocity/SIMPLE.cpp
cd "$ROOT" || exit 1
export PATH="/usr/local/cuda-12.9/bin:$PATH"
mkdir -p "$P17/logs" "$P17/controls"

JSON=$B/_deps/nlohmann_json-src/include
REF=results/p13/restart/logs/fingerprint-after-histories.log
strip() { grep -v '^#' "$1" | grep -v '^$'; }

build_probe() {
  g++ -std=c++20 -O2 -DNDEBUG -fopenmp -I include -I "$B/generated/include" -I "$JSON" \
      -I /usr/local/cuda-12.9/include results/p13/restart/tools/solve_fingerprint.cpp \
      -o /tmp/p17_8_fp -Wl,--start-group "$B/src/libcfdcore.a" "$B/cuda/libcfdcuda.a" \
      -Wl,--end-group -L/usr/local/cuda-12.9/lib64 -lcudart 2>"$P17/logs/11_fp_build.log"
}

echo "P17.8 -- A3 / G1: the P13-001 SIMPLE fingerprint"
echo "date: $(date -u +%FT%TZ)"
echo "probe    : results/p13/restart/tools/solve_fingerprint.cpp (unmodified)"
echo "reference: $REF"
echo ""

echo "### 0. the library under test is rebuilt from the current source ###"
cmake --build "$B" --target cfdcore -j 16 > "$P17/logs/11_core_build.log" 2>&1
echo "  cfdcore build exit: $?"
echo "  libcfdcore.a sha256: $(sha256sum $B/src/libcfdcore.a | awk '{print $1}')"
echo "  (nvcc is not bit-reproducible, so libcfdcuda.a's hash is NOT used as an identity check --"
echo "   the fingerprint OUTPUT is the instrument, not the archive hash.)"
echo ""

echo "### 1. the identity claim ###"
if ! build_probe; then
  echo "  COMPILE FAILED"
  tail -12 "$P17/logs/11_fp_build.log" | sed 's/^/    /'
  exit 2
fi
OMP_NUM_THREADS=1 /tmp/p17_8_fp > "$P17/logs/11_fp_run.log" 2>&1
echo "  cases: $(grep -c FINGERPRINT "$P17/logs/11_fp_run.log")   (P13-001 records 10)"
echo "  lines: $(grep -vc '^$' "$P17/logs/11_fp_run.log")   (P13-001 records 130)"
if diff <(strip "$REF") <(strip "$P17/logs/11_fp_run.log") > "$P17/logs/11_fp_diff.txt" 2>&1; then
  echo "  BITWISE IDENTICAL -- P17.8 moved no bit of the incompressible solver"
  echo "  the diff file is EMPTY and is preserved as evidence ($(wc -c < "$P17/logs/11_fp_diff.txt") bytes)"
  identical=1
else
  echo "  *** DIFFERS -- A3 / G1 FAILED ***"
  head -20 "$P17/logs/11_fp_diff.txt" | sed 's/^/    /'
  identical=0
fi
echo ""

echo "### 2. NON-VACUITY: the instrument must be able to FAIL (G1's own requirement) ###"
cp "$SIMPLE" "$P17/controls/SIMPLE.cpp.pristine"
PRISTINE=$(sha256sum "$SIMPLE" | awk '{print $1}')
echo "  pristine $SIMPLE sha256: $PRISTINE"
# A one-line perturbation of the velocity relaxation actually USED by the iteration. Small enough
# that the solve still converges, so what the diff shows is a moved solution, not a crash.
sed -i 's|    const cfd::solver::RelaxationFactors relaxation = monitor.relaxation();|    cfd::solver::RelaxationFactors relaxation = monitor.relaxation();\n    relaxation.velocity *= 0.999;  // G1 NON-VACUITY PLANT|' "$SIMPLE"
PLANTED=$(sha256sum "$SIMPLE" | awk '{print $1}')
if [ "$PLANTED" = "$PRISTINE" ]; then
  echo "  *** THE PLANT DID NOT APPLY -- the demonstration below would be vacuous."
  vacuous=1
else
  echo "  planted  $SIMPLE sha256: $PLANTED"
  cmake --build "$B" --target cfdcore -j 16 > "$P17/logs/11_core_build_planted.log" 2>&1
  echo "  cfdcore rebuild exit: $?"
  if build_probe; then
    OMP_NUM_THREADS=1 /tmp/p17_8_fp > "$P17/logs/11_fp_run_planted.log" 2>&1
    if diff <(strip "$REF") <(strip "$P17/logs/11_fp_run_planted.log") \
            > "$P17/logs/11_fp_diff_planted.txt" 2>&1; then
      echo "  *** THE PERTURBED SOLVER STILL MATCHES -- THE INSTRUMENT IS BLIND ***"
      vacuous=1
    else
      echo "  DETECTED: the perturbed solver's fingerprint DIFFERS from the reference"
      echo "  differing lines: $(grep -c '^[<>]' "$P17/logs/11_fp_diff_planted.txt")"
      echo "  first difference:"
      grep -m4 '^[<>]' "$P17/logs/11_fp_diff_planted.txt" | sed 's/^/    /'
      vacuous=0
    fi
  else
    echo "  the plant did not compile -- the demonstration is inconclusive"
    tail -6 "$P17/logs/11_fp_build.log" | sed 's/^/    /'
    vacuous=1
  fi
fi

cp "$P17/controls/SIMPLE.cpp.pristine" "$SIMPLE"
RESTORED=$(sha256sum "$SIMPLE" | awk '{print $1}')
echo "  restored $SIMPLE sha256: $RESTORED"
if [ "$RESTORED" != "$PRISTINE" ]; then
  echo "  *** RESTORE FAILED -- ABORTING so the plant cannot reach the regression"
  exit 3
fi
cmake --build "$B" --target cfdcore -j 16 > "$P17/logs/11_core_build_restored.log" 2>&1
echo "  cfdcore rebuilt from the restored source, exit: $?"
rm -f "$P17/controls/SIMPLE.cpp.pristine"
echo ""

echo "### 3. the incompressible path must remain EOS-free and solver-free (structural) ###"
EOSREF=$(grep -rn 'EquationOfState\|IdealGas\|ThermodynamicProperties' src/pressure_velocity/ include/cfd/pressure_velocity/ 2>/dev/null | wc -l)
DBREF=$(grep -rn 'DensityBasedSolver' src/pressure_velocity/ include/cfd/pressure_velocity/ src/app/ProjectRunner.cpp 2>/dev/null | wc -l)
echo "  EOS references under the pressure-velocity path      : $EOSREF   (must be 0)"
echo "  DensityBasedSolver references from SIMPLE / dispatch : $DBREF   (must be 0)"
echo ""

echo "### verdict ###"
echo "  A3 identity        : $([ "$identical" -eq 1 ] && echo 'BITWISE IDENTICAL' || echo '*** FAILED ***')"
echo "  G1 non-vacuity     : $([ "$vacuous" -eq 0 ] && echo 'INSTRUMENT DETECTS A PLANTED DEFECT' || echo '*** NOT DEMONSTRATED ***')"
echo "  structural         : $([ "$EOSREF" -eq 0 ] && [ "$DBREF" -eq 0 ] && echo 'clean' || echo '*** REFERENCES FOUND ***')"
echo "done: $(date -u +%FT%TZ)"
[ "$identical" -eq 1 ] && [ "$vacuous" -eq 0 ] && [ "$EOSREF" -eq 0 ] && [ "$DBREF" -eq 0 ]
