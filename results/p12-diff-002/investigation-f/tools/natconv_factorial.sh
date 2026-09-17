#!/usr/bin/env bash
# Step 1: 2x2 factorial attribution of the natural-convection change.
#
#   neither      = isolated pre-DIFF-002 baseline (no reconstruction anywhere)
#   thermal only = momentum reverted to the pre-A2 gate, thermal keeps DIFF-002
#   momentum only= thermal reverted to the pre-A2 gate, momentum keeps DIFF-002
#   both         = the authoritative tree
#
# Each build runs from its own tree so none overwrites another's validation.json.
set -u
R=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
LOG=$R/results/p12-diff-002/investigation-f/logs/11_natconv_factorial.log
F='NaturalConvectionValidation.Grid10x10MatchesDeVahlDavisRa1e3:NaturalConvectionValidation.Grid15x15MatchesDeVahlDavisRa1e3'
BIN=tests/integration/thermal/CFDNaturalConvectionValidationTests

row() { python3 - "$1" "$2" "$3" <<'PY'
import json,sys
lab,grid,p=sys.argv[1],sys.argv[2],sys.argv[3]
d=json.load(open(p)); v=d['validation']; c=d['conservation']; s=d['solver']
print(f"  {lab:<14} {grid:<6} Nu_err {v['nu_avg_error']:.6f}  u_max {v['u_max_computed']:.5f} (err {v['u_max_error']:.6f})  v_max {v['v_max_computed']:.5f} (err {v['v_max_error']:.6f})  theta [{v['min_theta']:.5f},{v['max_theta']:.5f}]  q_hot {c['q_hot']:.6f}  heat_imb {c['heat_imbalance']:.2e}  flow_it {s['flow_iterations']} outer {s['outer_iterations']}")
PY
}

one() {  # one <label> <tree> <build>
  local lab=$1 tree=$2 build=$3
  (cd "$tree" && "$build/$BIN" --gtest_filter="$F" > /dev/null 2>&1)
  for g in 10x10 15x15; do
    f="$tree/results/validation/natural_convection/Ra1e3/$g/validation.json"
    [ -f "$f" ] && row "$lab" "$g" "$f"
  done
}

{
  echo "# Step 1 natural-convection 2x2 factorial; $(date -u +%Y-%m-%dT%H:%M:%SZ)"
  echo "# De Vahl Davis Ra=1e3 reference unchanged. No production or benchmark tolerance modified."
  echo "# thresholds: 10x10 u<0.15 v<0.12 Nu<0.10 | 15x15 u<0.10 v<0.10 Nu<0.08"
  echo
  one "neither"       "$HOME/invf_baseline/src"     "$HOME/invf_baseline/build"
  echo
  one "thermal only"  "$HOME/invf_momentum_old/src" "$HOME/invf_momentum_old/build"
  echo
  one "momentum only" "$HOME/invf_thermal_old/src"  "$HOME/invf_thermal_old/build"
  echo
  one "both"          "$R"                          "$R/build/release"
} > "$LOG" 2>&1
cat "$LOG"
