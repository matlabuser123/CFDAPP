#!/usr/bin/env bash
# P12-DIFF-002-INV-002 / INV-F5 + F6: natural-convection De Vahl Davis comparison and refinement.
#
# Each build is run from ITS OWN tree root, so each writes its own validation.json and reads its own
# cases/ -- the earlier mistake was running both from the authoritative tree, where the second run
# overwrote the first's output.
set -u
R=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
BS=$HOME/invf_baseline/src
BB=$HOME/invf_baseline/build
LOG=$R/results/p12-diff-002/investigation-f/logs/05_F5_F6_natconv.log
D=$R/results/p12-diff-002/investigation-f/natural_convection

dump() {  # dump <label> <validation.json>
  python3 - "$1" "$2" <<'PY'
import json, sys
label, path = sys.argv[1], sys.argv[2]
d = json.load(open(path))
v, c, s = d['validation'], d['conservation'], d['solver']
print(f"  {label:<14} nu_avg {v['nu_avg_computed']:.5f} (ref {v['nu_avg_reference']}, err {v['nu_avg_error']:.6f})")
print(f"  {'':<14} u_max  {v['u_max_computed']:.5f} at y={v['u_max_computed_y']} (ref {v['u_max_reference']}, err {v['u_max_error']:.6f})")
print(f"  {'':<14} v_max  {v['v_max_computed']:.5f} at x={v['v_max_computed_x']} (ref {v['v_max_reference']}, err {v['v_max_error']:.6f})")
print(f"  {'':<14} theta  min {v['min_theta']:.6f} max {v['max_theta']:.6f}")
print(f"  {'':<14} mass imbalance {c['global_mass_imbalance']:.3e}  heat imbalance {c['heat_imbalance']:.3e}  q_hot {c['q_hot']:.6f}")
print(f"  {'':<14} flow it {s['flow_iterations']}  thermal it {s['thermal_iterations']}  outer it {s['outer_iterations']}  final dT {s['final_outer_temperature_change']:.3e}")
PY
}

mkdir -p "$D"
{
  echo "# INV-F5/F6 natural convection; $(date -u +%Y-%m-%dT%H:%M:%SZ)"
  echo "# reference: De Vahl Davis Ra=1e3 -- an INDEPENDENT literature benchmark, not redefined here."
  echo "# baseline library $(sha256sum $BB/src/libcfdcore.a | cut -c1-16) | current $(sha256sum $R/build/release/src/libcfdcore.a | cut -c1-16)"
  echo
  echo "## PRE-DIFF-002 baseline (run from its own tree)"
  (cd "$BS" && "$BB/tests/integration/thermal/CFDNaturalConvectionValidationTests" \
      --gtest_filter='NaturalConvectionValidation.Grid10x10MatchesDeVahlDavisRa1e3:NaturalConvectionValidation.Grid15x15MatchesDeVahlDavisRa1e3' \
      > /dev/null 2>&1)
  for g in 10x10 15x15; do
    f="$BS/results/validation/natural_convection/Ra1e3/$g/validation.json"
    [ -f "$f" ] && { echo "  grid $g:"; dump "PRE-DIFF-002" "$f"; cp "$f" "$D/baseline_$g.json"; }
  done
  echo
  echo "## CURRENT (run from the authoritative tree)"
  (cd "$R" && ./build/release/tests/integration/thermal/CFDNaturalConvectionValidationTests \
      --gtest_filter='NaturalConvectionValidation.Grid10x10MatchesDeVahlDavisRa1e3:NaturalConvectionValidation.Grid15x15MatchesDeVahlDavisRa1e3' \
      > /dev/null 2>&1)
  for g in 10x10 15x15; do
    f="$R/results/validation/natural_convection/Ra1e3/$g/validation.json"
    [ -f "$f" ] && { echo "  grid $g:"; dump "CURRENT" "$f"; cp "$f" "$D/current_$g.json"; }
  done
  echo
  echo "## thresholds for reference: 10x10 u_max<0.15 v_max<0.12 nu_avg<0.10 ; 15x15 u<0.10 v<0.10 nu<0.08"
} > "$LOG" 2>&1
cat "$LOG"
