#!/usr/bin/env bash
# P18.9 sections 36-39 -- P14 / P15 / P16 / P17 PRESERVATION, plus sections 40-42's canonical suites.
#
# EACH SUITE IS DEFINED BY ITS TEST BINARIES, not by a keyword grep. A grep for "conservation" or
# "species" picks up P14's TransportConservation, P15's ThermalConservation, P17's HllcConservation
# and P19's PhaseConservation, which would both inflate P18's figures and blur whose preservation is
# being measured. The binaries are owned by directory, so the attribution is unambiguous.
#
# "No regressions" is not an acceptable report (section 59): every suite below reports exact counts.
set -u
cd /mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp || exit 1
OUT="${1:?usage: run_preservation.sh <output-log>}"
B=build/qual-final

# phase|label|binaries
SUITES="
P14|generic transport|tests/unit/transport/CFDTransportTests tests/integration/transport/CFDTransportVerificationTests
P15|thermal / energy|tests/unit/thermal/CFDThermalTests tests/integration/thermal/CFDThermalValidationTests tests/integration/thermal/CFDBoussinesqCouplingTests tests/integration/thermal/CFDHeatedCavityValidationTests tests/integration/thermal/CFDConjugateHeatTransferValidationTests
P16|turbulence|tests/unit/turbulence/CFDTurbulenceTests
P17|qualified compressible|tests/unit/compressible/CFDCompressibleTests tests/solver/compressible_simple/CFDCompressibleSimpleTests tests/integration/compressible/CFDLowMachFamilyTests tests/integration/compressible/CFDLowMachRegressionTests tests/integration/compressible/CFDDensityBasedCanonicalTests tests/integration/compressible/CFDShockValidationTests
P18-SPECIES|canonical species (section 40)|tests/integration/species/CFDSpeciesAdvectionDiffusionValidationTests tests/integration/species/CFDSpeciesConservationValidationTests tests/integration/species/CFDSpeciesCouplingValidationTests tests/integration/species/CFDSpeciesDiffusionValidationTests
"

run_binary() {  # $1 path -> echoes "ran passed failed"
  local b="$1"
  if [ ! -x "$B/$b" ]; then echo "0 0 MISSING"; return; fi
  OMP_NUM_THREADS=1 "$B/$b" > /tmp/pres.out 2>&1
  local p f r
  p=$(grep -c '^\[       OK \]' /tmp/pres.out)
  f=$(grep -c '^\[  FAILED  \]' /tmp/pres.out)
  r=$(grep -aoE '^\[==========\] [0-9]+ tests? from' /tmp/pres.out | tail -1 | grep -oE '[0-9]+')
  # gtest prints FAILED twice per failure (inline + summary); halve and round up
  f=$(( (f + 1) / 2 ))
  echo "${r:-0} ${p:-0} ${f:-0}"
}

{
  echo "============================================================"
  echo "P18.9 sections 36-42 -- PRESERVATION AND CANONICAL SUITES"
  echo "============================================================"
  echo "date (WSL): $(date -u +%Y-%m-%dT%H:%M:%SZ)"
  echo "build: $B (Release, CUDA ON, OpenMP ON), OMP_NUM_THREADS=1"
  echo "git HEAD: $(git rev-parse HEAD)"
  echo ""
  printf '%-12s %-34s %7s %7s %7s  %s\n' PHASE SUITE RAN PASSED FAILED STATUS
  printf '%s\n' "----------------------------------------------------------------------------------------"
  echo "$SUITES" | while IFS='|' read -r phase label bins; do
    [ -n "$phase" ] || continue
    tr=0; tp=0; tf=0; missing=0
    for b in $bins; do
      set -- $(run_binary "$b")
      if [ "$3" = "MISSING" ]; then missing=$((missing+1)); continue; fi
      tr=$((tr + $1)); tp=$((tp + $2)); tf=$((tf + $3))
    done
    st="PASS"
    [ "$tf" -eq 0 ] || st="FAIL ($tf)"
    [ "$missing" -eq 0 ] || st="$st, $missing binary missing"
    [ "$tr" -gt 0 ] || st="NO TESTS RAN -- vacuous"
    printf '%-12s %-34s %7s %7s %7s  %s\n' "$phase" "$label" "$tr" "$tp" "$tf" "$st"
  done
  echo ""
  echo "--- per-binary detail ---"
  echo "$SUITES" | while IFS='|' read -r phase label bins; do
    [ -n "$phase" ] || continue
    for b in $bins; do
      set -- $(run_binary "$b")
      printf '  %-12s %-62s ran %-5s passed %-5s failed %s\n' "$phase" "$(basename "$b")" "$1" "$2" "$3"
    done
  done
} > "$OUT" 2>&1
cat "$OUT"
