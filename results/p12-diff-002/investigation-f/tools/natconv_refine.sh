#!/usr/bin/env bash
# INV-F6: natural-convection refinement in BOTH builds, including the DISABLED 20x20 grid, so the
# trend against the same independent benchmark is visible. Each build runs from its own tree.
set -u
R=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
BS=$HOME/invf_baseline/src; BB=$HOME/invf_baseline/build
LOG=$R/results/p12-diff-002/investigation-f/logs/06_F6_refinement.log
F='NaturalConvectionValidation.Grid10x10MatchesDeVahlDavisRa1e3:NaturalConvectionValidation.Grid15x15MatchesDeVahlDavisRa1e3:NaturalConvectionValidation.DISABLED_Grid20x20MatchesDeVahlDavisRa1e3'
row() { python3 - "$1" "$2" <<'PY'
import json,sys
lab,p=sys.argv[1],sys.argv[2]
d=json.load(open(p)); v=d['validation']; c=d['conservation']
print(f"  {lab:<13} {d['mesh']['nx']:>3}x{d['mesh']['ny']:<3} nu_err {v['nu_avg_error']:.6f}  u_max_err {v['u_max_error']:.6f}  v_max_err {v['v_max_error']:.6f}  heat_imb {c['heat_imbalance']:.3e}  mass_imb {c['global_mass_imbalance']:.3e}")
PY
}
{
  echo "# INV-F6 natural-convection refinement; $(date -u +%Y-%m-%dT%H:%M:%SZ)"
  echo "# same independent De Vahl Davis Ra=1e3 reference at every grid; benchmark NOT redefined."
  echo
  echo "## PRE-DIFF-002"
  (cd "$BS" && "$BB/tests/integration/thermal/CFDNaturalConvectionValidationTests" --gtest_also_run_disabled_tests --gtest_filter="$F" >/dev/null 2>&1)
  for g in 10x10 15x15 20x20; do f="$BS/results/validation/natural_convection/Ra1e3/$g/validation.json"; [ -f "$f" ] && row "PRE-DIFF-002" "$f"; done
  echo
  echo "## CURRENT"
  (cd "$R" && ./build/release/tests/integration/thermal/CFDNaturalConvectionValidationTests --gtest_also_run_disabled_tests --gtest_filter="$F" >/dev/null 2>&1)
  for g in 10x10 15x15 20x20; do f="$R/results/validation/natural_convection/Ra1e3/$g/validation.json"; [ -f "$f" ] && row "CURRENT" "$f"; done
} > "$LOG" 2>&1
cat "$LOG"
