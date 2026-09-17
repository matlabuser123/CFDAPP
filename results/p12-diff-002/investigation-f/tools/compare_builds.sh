#!/usr/bin/env bash
# P12-DIFF-002-INV-002 / INV-F2 + F9: run the four sub-class F tests in BOTH the isolated
# pre-DIFF-002 baseline build and the current build, so every reported number has a trustworthy
# before/after pair. The two builds differ only by the DIFF-002 reconstruction block.
set -u
R=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
BB=$HOME/invf_baseline/build
CB=$R/build/release
LOG=$R/results/p12-diff-002/investigation-f/logs/04_F2_baseline_vs_current.log
cd $R

one() {  # one <label> <build-root> <relative-binary> <gtest filter> <grep pattern>
  local label=$1 root=$2 rel=$3 filter=$4 pat=$5
  local bin="$root/$rel"
  if [ ! -x "$bin" ]; then echo "  $label: BINARY MISSING ($bin)"; return; fi
  echo "  --- $label  (binary $(sha256sum "$bin" | cut -c1-16)) ---"
  "$bin" --gtest_filter="$filter" 2>&1 | grep -E "$pat" | head -12 | sed 's/^/    /'
}

{
  echo "# INV-F2/F9 baseline vs current; $(date -u +%Y-%m-%dT%H:%M:%SZ)"
  echo "# baseline library $(sha256sum $BB/src/libcfdcore.a | cut -c1-16)  (DIFF-002 block removed)"
  echo "# current  library $(sha256sum $CB/src/libcfdcore.a | cut -c1-16)"
  echo

  echo "## F1 SpeciesConservationTest.OpenChannelWithVolumetricSource..."
  for b in "PRE-DIFF-002 $BB" "CURRENT $CB"; do
    set -- $b
    one "$1" "$2" tests/integration/species/CFDSpeciesConservationValidationTests \
      'SpeciesConservationTest.OpenChannelWithVolumetricSourceBalancesNetOutflowAgainstSource' \
      'actual:|OK \]|FAILED \]'
  done
  echo

  echo "## F2 LowMachRegressionTest.GlobalMassImbalanceIsSmall"
  for b in "PRE-DIFF-002 $BB" "CURRENT $CB"; do
    set -- $b
    one "$1" "$2" tests/integration/compressible/CFDLowMachRegressionTests \
      'LowMachRegressionTest.*' 'actual:|OK \]|FAILED \]|imbalance|Mach'
  done
  echo

  echo "## F3/F4 NaturalConvectionValidation De Vahl Davis 10x10 (both variants) + 15x15"
  for b in "PRE-DIFF-002 $BB" "CURRENT $CB"; do
    set -- $b
    one "$1" "$2" tests/integration/thermal/CFDNaturalConvectionValidationTests \
      'NaturalConvectionValidation.Grid10x10MatchesDeVahlDavisRa1e3:NaturalConvectionValidation.Grid10x10ConstantPropertyModelsMatchDeVahlDavisRa1e3:NaturalConvectionValidation.Grid15x15MatchesDeVahlDavisRa1e3' \
      'actual:|OK \]|FAILED \]|u_max|v_max|Nu|nusselt|imbalance'
  done
} > "$LOG" 2>&1
cat "$LOG"
