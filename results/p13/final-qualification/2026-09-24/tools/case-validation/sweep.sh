#!/usr/bin/env bash
# P13-002 -- run the mutation matrix over every case FAMILY, not just one case.
#
# A single 2D laminar cavity exercises a fraction of the configuration surface.
# Each case below brings a different block into play -- thermal, species,
# multiphase, compressible, turbulence, 3D, graded, multiblock -- and the
# matrix generates its rows from whatever that case's own JSON contains, so
# each family is swept without the harness knowing anything about it.
#
# Every copy's max_iterations is lowered to 2, so a mutation the validation
# ACCEPTS returns in milliseconds instead of solving. That changes what a
# rejected case does not reach; it cannot make an accepted one look rejected.
set -uo pipefail
ROOT=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
E=$ROOT/results/p13/final-qualification/2026-09-24
T=$ROOT/results/p13/final-qualification/2026-09-24/tools/case-validation
L=$ROOT/results/p13/final-qualification/2026-09-24/case_validation_logs
B=$ROOT/build/final/apps/cli/cfdapp
WORK=$(mktemp -d /tmp/p13sweep.XXXXXX)
cd "$ROOT"
mkdir -p "$L"

FAMILIES=(
  "cavity-2d|cases/lid_driven_cavity"
  "duct-3d|cases/duct_3d"
  "thermal|cases/heated_cavity"
  "species|cases/species_diffusion"
  "thermal-species|cases/heated_species_diffusion"
  "multiphase|cases/multiphase_validation"
  "compressible|cases/compressible_validation"
  "compressible-coupled|cases/compressible_channel_coupled"
  "graded|cases/channel_transpiration_graded"
  "multiblock|cases/curved_channel_multiblock"
  "quad-2d|cases/poiseuille_distorted"
)

total=0
accepted=0
vague=0
weak=0
: > "$L/sweep.log"

prepare() {  # prepare <dest> <source>
  rm -rf "$1"; cp -r "$2" "$1"; rm -rf "$1/results"
  python3 - "$1/solver.json" <<'PY'
import json, sys
p = sys.argv[1]
d = json.load(open(p))
d["max_iterations"] = 2
json.dump(d, open(p, "w"), indent=2)
PY
}

# A turbulence case: none is shipped under cases/, so one is derived from the
# cavity. Without it the k_epsilon / k_omega / sst configuration surface would
# go unswept.
prepare "$WORK/turbulence" "$ROOT/cases/lid_driven_cavity"
python3 - "$WORK/turbulence/physics.json" <<'PY'
import json, sys
p = sys.argv[1]
d = json.load(open(p))
d["turbulence"] = {"model": "k_epsilon", "initial_k": 0.02, "initial_epsilon": 0.005}
json.dump(d, open(p, "w"), indent=2)
PY
FAMILIES+=("turbulence|$WORK/turbulence")

for entry in "${FAMILIES[@]}"; do
  name=${entry%%|*}
  path=${entry#*|}
  [ -d "$path" ] || { echo "  SKIP $name (no such case: $path)" | tee -a "$L/sweep.log"; continue; }
  case "$path" in
    /*) src=$path ;;
    *)  src=$ROOT/$path ;;
  esac
  dest=$WORK/$name
  [ "$src" = "$dest" ] || prepare "$dest" "$src"
  out=$L/matrix-$name.log
  python3 "$T/mutation_matrix.py" "$dest" "$B" --out "$L/matrix-$name.csv" > "$out" 2>&1
  rc=$?
  n=$(grep -m1 "^mutations applied" "$out" | awk '{print $3}')
  a=$(grep -m1 "^ACCEPTED" "$out" | awk '{print $3}')
  v=$(grep -m1 "^rejected but vague" "$out" | awk '{print $4}')
  w=$(grep -m1 "^rejected but weak" "$out" | awk '{print $4}')
  printf '%-22s mutations %-5s accepted %-4s vague %-4s weak %s\n' \
    "$name" "${n:-?}" "${a:-?}" "${v:-?}" "${w:-?}" | tee -a "$L/sweep.log"
  total=$((total + ${n:-0}))
  accepted=$((accepted + ${a:-0}))
  vague=$((vague + ${v:-0}))
  weak=$((weak + ${w:-0}))
  [ $rc -eq 0 ] || echo "    (harness exit $rc -- see $out)" | tee -a "$L/sweep.log"
done

{ echo ""
  echo "TOTAL   mutations $total   accepted $accepted   vague $vague   weak $weak"
  echo "        vague = no file AND no field (criterion M2)"
  echo "        weak  = no file OR  no field (proxy, M2b) -- see acceptance_gate_A2.md"; } | tee -a "$L/sweep.log"

if [ "$accepted" -gt 0 ]; then
  echo ""
  echo "=== every ACCEPTED row across the sweep ===" | tee -a "$L/sweep.log"
  grep -h -A200 "^=== ACCEPTED" "$L"/matrix-*.log 2>/dev/null \
    | grep -E "^  [a-z]" | sort -u | tee -a "$L/sweep.log"
fi
echo ""
echo "work dir: $WORK"
