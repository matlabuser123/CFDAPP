#!/usr/bin/env bash
# Diagnostic (not a gate item): spanwise mirror asymmetry of the light 16^3 Re=100 cube as a
# function of the outer tolerance, through the CLI. Iterative asymmetry scales with the tolerance;
# a discretization asymmetry does not.
R=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
W=$HOME/m6sym
rm -rf $W; mkdir -p $W
for tol in 1e-6 1e-8 1e-10 1e-12; do
  cp -r $R/cases/lid_driven_cavity_3d $W/t$tol
  rm -rf $W/t$tol/results
  python3 - "$W/t$tol/solver.json" "$tol" <<'EOF'
import json, sys
p, tol = sys.argv[1], float(sys.argv[2])
s = json.load(open(p))
s["velocity_tolerance"] = s["pressure_tolerance"] = s["continuity_tolerance"] = tol
s["max_iterations"] = 20000
s["momentum_linear_solver"]["absolute_tolerance"] = 1e-16
s["pressure_linear_solver"]["absolute_tolerance"] = 1e-16
json.dump(s, open(p, "w"), indent=2)
EOF
  $R/build/release/apps/cli/cfdapp --case $W/t$tol > $W/t$tol.out 2>&1
  python3 - "$W/t$tol/results/fields.csv" "$tol" "$(grep -m1 -i 'iterations' $W/t$tol.out)" <<'EOF'
import csv, sys
rows = list(csv.DictReader(open(sys.argv[1])))
n = 16; h = 1.0 / n
f = {}
for r in rows:
    i, j, k = (int(float(r[c]) / h) for c in ("x", "y", "z"))
    f[(i, j, k)] = (float(r["velocity_x"]), float(r["velocity_y"]), float(r["velocity_z"]))
asym = 0.0
for (i, j, k), (u, v, w) in f.items():
    um, vm, wm = f[(i, j, n - 1 - k)]
    asym = max(asym, abs(u - um), abs(v - vm), abs(w + wm))
print(f"tolerance {sys.argv[2]:>6}: spanwise asymmetry {asym:.3e}   [{sys.argv[3].strip()}]")
EOF
done
