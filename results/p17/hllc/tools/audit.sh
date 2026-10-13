#!/usr/bin/env bash
# P17.7 section 1 -- audit BEFORE any implementation.
# P17.0 recorded "0 hits for HLLC/HLL/Rusanov/conserved-state" and that the Roe/Euler hits are false
# positives. That is a claim from an earlier phase; it is RE-VERIFIED here rather than trusted.
set -uo pipefail
ROOT=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
P17=$ROOT/results/p17/hllc
cd "$ROOT" || exit 1
mkdir -p "$P17/logs" "$P17/tools"

SRC=(include src cuda tests apps)

log=$P17/logs/00_audit.log
{
echo "P17.7 AUDIT -- existing Riemann / Euler-flux surface"
echo "date: $(date -u +%FT%TZ)"
echo "HEAD: $(git rev-parse HEAD)"
echo "searched roots: ${SRC[*]}"
echo ""

echo "### 1a. the section 1 term list, case-insensitive, FILE counts then LINE counts ###"
printf '  %-26s %8s %8s\n' TERM FILES LINES
for t in 'Euler flux' 'Riemann' 'HLLC' 'HLL' 'Roe' 'Rusanov' 'Lax-Friedrichs' 'LaxFriedrichs' \
         'Godunov' 'shock' 'ConservedState' 'conserved state' 'PrimitiveState' 'primitive state' \
         'face reconstruction' 'reconstruct' 'wave speed' 'waveSpeed' 'contact speed' 'contactSpeed' \
         'MUSCL' 'WENO' 'slope limiter' 'minmod' 'vanLeer' 'superbee'; do
  f=$(grep -rliI -- "$t" "${SRC[@]}" 2>/dev/null | wc -l)
  l=$(grep -rniI -- "$t" "${SRC[@]}" 2>/dev/null | wc -l)
  printf '  %-26s %8s %8s\n' "$t" "$f" "$l"
done
echo ""

echo "### 1b. every hit for the DECISIVE terms, in full -- these decide MISSING vs EXTEND ###"
for t in HLLC Riemann Rusanov Godunov 'Lax-Friedrichs' MUSCL WENO; do
  n=$(grep -rniI -- "$t" "${SRC[@]}" 2>/dev/null | wc -l)
  echo "  --- $t ($n) ---"
  grep -rniI -- "$t" "${SRC[@]}" 2>/dev/null | sed 's/^/      /' | head -20
done
echo ""

echo "### 1c. HLL as a WHOLE WORD (so 'HLLC' and prose do not inflate it) ###"
echo "  word-boundary HLL: $(grep -rnwiI -- 'HLL' "${SRC[@]}" 2>/dev/null | wc -l)"
grep -rnwiI -- 'HLL' "${SRC[@]}" 2>/dev/null | sed 's/^/    /' | head -10
echo ""

echo "### 1d. 'Roe' -- P17.0 called these FALSE POSITIVES. Verify that directly. ###"
echo "  total 'Roe' lines: $(grep -rniI -- 'Roe' "${SRC[@]}" 2>/dev/null | wc -l)"
echo "  as a whole word  : $(grep -rnwiI -- 'Roe' "${SRC[@]}" 2>/dev/null | wc -l)"
echo "  the whole-word hits:"
grep -rnwiI -- 'Roe' "${SRC[@]}" 2>/dev/null | sed 's/^/    /' | head -20
echo "  (a Roe-average or Roe-flux implementation would appear as an identifier, not inside a word)"
echo ""

echo "### 1e. 'Euler' -- 107 hits were called false positives. Classify them. ###"
echo "  total 'Euler' lines: $(grep -rniI -- 'Euler' "${SRC[@]}" 2>/dev/null | wc -l)"
echo "  breakdown by what they actually are:"
printf '    %-42s %s\n' "backward/forward Euler TIME integration" \
  "$(grep -rniI -E '(backward|forward|implicit|explicit|semi)[ -]?Euler' "${SRC[@]}" 2>/dev/null | wc -l)"
printf '    %-42s %s\n' "Euler EQUATIONS / Euler flux (what P17.7 needs)" \
  "$(grep -rniI -E 'Euler (equation|flux|system|state)' "${SRC[@]}" 2>/dev/null | wc -l)"
printf '    %-42s %s\n' "Euler as an identifier (eulerStep etc.)" \
  "$(grep -rnI -E '[A-Za-z_]euler|euler[A-Z]' "${SRC[@]}" 2>/dev/null | wc -l)"
echo "  any 'Euler equation/flux/system/state' hits, in full:"
grep -rniI -E 'Euler (equation|flux|system|state)' "${SRC[@]}" 2>/dev/null | sed 's/^/    /' | head -20
echo ""

echo "### 1f. what a conserved-state / primitive-state type would look like ###"
for pat in 'struct ConservedState' 'class ConservedState' 'struct PrimitiveState' \
           'class PrimitiveState' 'rhoU' 'rhoE' 'conservedTotalEnergy' 'recoverPrimitive'; do
  printf '  %-30s %s\n' "$pat" "$(grep -rnI -- "$pat" "${SRC[@]}" 2>/dev/null | wc -l)"
done
echo ""
echo "  WHERE rhoE / conserved-energy machinery already lives (P17.5):"
grep -rlnI -E 'conservedTotalEnergyField|recoverPrimitiveFields' "${SRC[@]}" 2>/dev/null | sed 's/^/    /'
echo ""

echo "### 1g. the P17.2/P17.5 pieces P17.7 must REUSE, not duplicate ###"
for sym in soundSpeed machNumber IdealGasEOS ThermodynamicProperties TotalEnergyState \
           specificInternalEnergy temperatureFromInternalEnergy conservedTotalEnergyField \
           recoverPrimitiveFields; do
  printf '  %-34s declared in: %s\n' "$sym" \
    "$(grep -rlnI -- "$sym" include/ 2>/dev/null | tr '\n' ' ')"
done
echo ""

echo "### 1h. existing FLUX vocabulary -- is there a numerical-flux abstraction to extend? ###"
for pat in 'numericalFlux' 'NumericalFlux' 'faceFlux' 'FaceFlux' 'interfaceFlux' 'fluxFunction'\
           'calculateCompressibleMassFlux' 'massFlux'; do
  printf '  %-34s %s\n' "$pat" "$(grep -rnI -- "$pat" "${SRC[@]}" 2>/dev/null | wc -l)"
done
echo ""

echo "### 1i. the GPU surface -- is there any compressible/flux device code? ###"
for s in HLLC Riemann ConservedState rhoE soundSpeed compressible IdealGas Euler; do
  printf '  %-20s files in cuda/ + include/cfd/gpu/: %s\n' "$s" \
    "$(grep -rliI -- "$s" cuda/ include/cfd/gpu/ 2>/dev/null | wc -l)"
done
echo ""
echo "  every 'Euler' or 'compressible' line in the GPU tree, in full:"
grep -rniI -E 'Euler|compressible' cuda/ include/cfd/gpu/ 2>/dev/null | sed 's/^/    /' | head -10
echo ""

echo "### 1j. is there a Vector3/3D state at all? (the scope asks for rho w and n_z) ###"
printf '  %-28s %s\n' 'Vector2 alias of Vector3' "$(grep -rnI -E 'using Vector2' include/ | head -2 | tr '\n' ' ')"
printf '  %-28s %s\n' 'Vector3 definition' "$(grep -rlnI -E 'struct Vector3|class Vector3' include/ | tr '\n' ' ')"
echo ""
echo "done: $(date -u +%FT%TZ)"
} 2>&1 | tee "$log"
