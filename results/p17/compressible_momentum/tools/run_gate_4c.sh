#!/usr/bin/env bash
# P17.4 gate attempt 4b -- FOCUSED re-verification after the C2/C4 tests were added.
#
# Attempt 4 measured the 19-test suite and the P14/P15/P16 preservation groups. Adding three
# TESTs to tests/unit/compressible/test_compressible_momentum.cpp then changed the suite, so the
# groups that read it are re-measured here. P14/P15/P16 are NOT re-run in this script, for a
# reason that is CHECKED below rather than assumed: those tests live in different executables,
# so relinking the compressible unit-test binary cannot affect them -- and in any case the full
# mandated regression re-runs all 2607 registered tests in both build trees afterwards, which is
# the authoritative preservation measurement.
set -uo pipefail
ROOT=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
P17=$ROOT/results/p17/compressible_momentum
B=build/qual-final
cd "$ROOT" || exit 1
export PATH="/usr/local/cuda-12.9/bin:$PATH"
mkdir -p "$P17/logs"

FILES="include/cfd/compressible/DilatationalStress.hpp
src/compressible/DilatationalStress.cpp
include/cfd/compressible/CompressibleRelaxedMomentum.hpp
src/compressible/CompressibleRelaxedMomentum.cpp
tests/unit/compressible/test_compressible_momentum.cpp"

log=$P17/logs/14_gate_4c.log
{
echo "P17.4 GATE ATTEMPT 4c -- focused re-verification (B3 added)"
echo "date: $(date -u +%FT%TZ)"
echo "gate sha256: $(sha256sum "$P17/acceptance_gate.md" | awk '{print $1}')   (unchanged)"
echo "TESTs in test_compressible_momentum.cpp: $(grep -c '^TEST(' tests/unit/compressible/test_compressible_momentum.cpp)   (7 at the freeze)"
echo ""

echo "### G6: clang-format ###"
# shellcheck disable=SC2086
clang-format-18 -i $FILES
# shellcheck disable=SC2086
clang-format-18 --dry-run --Werror $FILES 2>&1 | head -6
echo "  clang-format exit after -i: ${PIPESTATUS[0]}  (0 = clean)"
echo ""

echo "### L5: physics::MomentumEquation must be UNTOUCHED ###"
FROZEN_MOM_CPP=e46d49fcda78e0271538bc69117e249b0ea50dd2265c5f603d64d4206dc7bb59
FROZEN_MOM_HPP=7feca5c4a3a22943c442d1fa162b4e860ab037b69a3c6a6ce0e6c413bb0e37af
mom_cpp=$(sha256sum src/physics/MomentumEquation.cpp | awk '{print $1}')
mom_hpp=$(sha256sum include/cfd/physics/MomentumEquation.hpp | awk '{print $1}')
if [ "$mom_cpp" = "$FROZEN_MOM_CPP" ] && [ "$mom_hpp" = "$FROZEN_MOM_HPP" ]; then
  echo "  verdict: UNTOUCHED -- both files hash to the freeze"
else
  echo "  verdict: *** TOUCHED -- L5 VIOLATED ***"
  echo "    $mom_cpp  vs  $FROZEN_MOM_CPP"
  echo "    $mom_hpp  vs  $FROZEN_MOM_HPP"
fi
echo ""

echo "### D1: default disabled -- source text AND behaviour ###"
echo "  header default: $(grep -c 'bool includeDilatationalStress = false' include/cfd/compressible/CompressibleRelaxedMomentum.hpp)  (1 = false)"
echo ""

echo "### build ###"
t0=$(date +%s)
cmake --build "$B" -j 16 > "$P17/logs/14_build.log" 2>&1
brc=$?
echo "  build exit: $brc  ($(( $(date +%s) - t0 )) s)"
if [ $brc -ne 0 ]; then
  echo "  *** BUILD FAILED -- stopping ***"
  grep -aE '(^|[^a-zA-Z])error:' "$P17/logs/14_build.log" | head -25 | sed 's/^/    /'
  echo "done: $(date -u +%FT%TZ)"; exit 1
fi
echo ""

echo "### binary separation -- CHECKED, not assumed ###"
mom_bin=$(ctest --test-dir "$B" --show-only=json-v1 2>/dev/null \
  | python3 -c 'import json,sys,os
d=json.load(sys.stdin)
s=set()
for t in d["tests"]:
    if any(k in t["name"] for k in ("DilatationalStress","CompressibleMomentum")):
        s.add(os.path.basename(t["command"][0]))
print(" ".join(sorted(s)))')
p16_bin=$(ctest --test-dir "$B" --show-only=json-v1 2>/dev/null \
  | python3 -c 'import json,sys,os,re
d=json.load(sys.stdin)
s=set()
rx=re.compile("Turbulence|EffectiveViscosity|WallFunction|WallTreatment|KEpsilon|KOmega")
for t in d["tests"]:
    if rx.search(t["name"]):
        s.add(os.path.basename(t["command"][0]))
print(" ".join(sorted(s)))')
echo "  P17.4 test binaries : $mom_bin"
echo "  P16 test binaries   : $p16_bin"
overlap=$(python3 -c "
a=set('''$mom_bin'''.split()); b=set('''$p16_bin'''.split()); print(' '.join(sorted(a&b)) or 'NONE')")
echo "  overlap             : $overlap   (NONE = the P16 result from attempt 4 is unaffected)"
echo ""

echo "### the momentum suite ###"
OMP_NUM_THREADS=1 ctest --test-dir "$B" -j 8 \
  -R 'CompressibleMomentum|DilatationalStress' --output-on-failure \
  > "$P17/logs/14_momentum.log" 2>&1
echo "  ctest exit: $?"
grep -aE 'tests passed|tests failed' "$P17/logs/14_momentum.log" | tail -1 | sed 's/^/  /'
echo "  the 15 P17.4 rows:"
for t in MatchesMinusTwoThirdsMuGradDivergenceTimesVolume IsExactlyZeroForADivergenceFreeField \
         UniformVelocityContributesNothing ScalesLinearlyWithViscosityAndVanishesAtZeroViscosity \
         RejectsInvalidInputs RepeatedAssemblyIsBitwiseIdentical \
         PressureForceMatchesMinusVolumeTimesAnalyticalGradient \
         BodyForceScalesLinearlyWithDensity TransientTermDistinguishesDRhoUDtFromRhoDUDt \
         IsWiredIntoTheRelaxedMomentumAssembly DefaultIsDisabledBehaviourally \
         UniformFlowResidualIsAtRoundOff ConvectiveAssemblyIsGloballyConservative \
         NonFiniteAssembledSystemThrowsNumericalError SymmetryCarriesZeroNormalMomentumFlux; do
  printf '    %-56s %s\n' "$t" \
    "$(grep -aoE "$t .*(Passed|Failed)" "$P17/logs/14_momentum.log" | grep -oE 'Passed|Failed' | head -1)"
done
echo ""

echo "### Group L: low-Mach + MMS, and the whole compressible suite ###"
OMP_NUM_THREADS=1 ctest --test-dir "$B" -j 8 -R 'LowMach|MMSCompressible|CompressibleSimpleMMS' \
  --output-on-failure > "$P17/logs/14_low_mach.log" 2>&1
echo "  low-Mach + MMS exit: $?   $(grep -aE 'tests passed|tests failed' "$P17/logs/14_low_mach.log" | tail -1)"
OMP_NUM_THREADS=1 ctest --test-dir "$B" -j 8 \
  -R 'Compressible|IdealGas|Thermodynamic|LowMach|MMSCompressible|Dilatational' --output-on-failure \
  > "$P17/logs/14_compressible_suite.log" 2>&1
echo "  compressible suite exit: $?   $(grep -aE 'tests passed|tests failed' "$P17/logs/14_compressible_suite.log" | tail -1)"
echo ""

echo "### G8: GPU compressible momentum -- must be NOT PRESENT ###"
for s in compressibleMomentum CompressibleRelaxed dilatational DilatationalStress; do
  printf '    %-24s %s\n' "$s" "$(grep -rliI -- "$s" cuda/ include/cfd/gpu/ 2>/dev/null | wc -l)"
done
echo ""
echo "done: $(date -u +%FT%TZ)"
} 2>&1 | tee "$log"
