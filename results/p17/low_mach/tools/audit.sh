#!/usr/bin/env bash
# P17.6 audit capture -- the UNCHANGED tree. Read-only on production source.
set -uo pipefail
ROOT=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
P17=$ROOT/results/p17/low_mach
cd "$ROOT" || exit 1
export PATH="/usr/local/cuda-12.9/bin:$PATH"
mkdir -p "$P17/logs"

T=tests/integration/compressible/test_low_mach_regression.cpp
out=$P17/logs/00_audit.log
{
echo "P17.6 AUDIT -- existing low-Mach validation, unchanged tree"
echo "date: $(date -u +%FT%TZ)"
echo "HEAD: $(git rev-parse --short=12 HEAD)"
echo ""

echo "### 1. the existing low-Mach test ###"
printf '  %-58s %s lines, %s TESTs\n' "$T" "$(wc -l < $T)" "$(grep -c '^TEST' $T)"
echo "  its TEST names:"
grep -oE '^TEST\(LowMachRegressionTest, [A-Za-z]+' $T | sed 's/^TEST(LowMachRegressionTest, /    /'
echo ""

echo "### 2. THE DECISIVE QUESTION: does it run the COMPRESSIBLE solver? ###"
echo "  CompressibleSIMPLE mentions in the low-Mach test : $(grep -c 'CompressibleSIMPLE' $T)"
echo "  incompressible SIMPLE solve calls               : $(grep -c 'simple.solve' $T)"
echo "  what it builds the compressible quantities FROM:"
grep -nE 'const VectorField& velocity = flow.velocity|thermo.density\(pressureAbsolute|machNumber\(speed' $T | sed 's/^/    /'
echo "  => the density, Mach number and compressible mass flux are all evaluated ON THE"
echo "     INCOMPRESSIBLE SOLUTION. There is no compressible solve, so 'the compressible"
echo "     solution approaches the incompressible one' is TRUE BY CONSTRUCTION and measures"
echo "     nothing. Authorization section 20 names this exact degenerate case."
echo ""

echo "### 3. where the compressible solver IS genuinely driven ###"
for f in tests/solver/compressible_simple/test_compressible_simple.cpp \
         tests/integration/mms/test_mms_compressible.cpp \
         tests/integration/case/test_compressible_coupled_production_case.cpp; do
  [ -f "$f" ] && printf '  %-64s solve() calls: %s\n' "$f" "$(grep -c 'compressibleSimple.solve\|\.solve(' "$f")"
done
echo ""

echo "### 4. the frozen G5 criterion (P17_GATES.md) ###"
sed -n '/^## G5 /,/^## G6 /p' results/p17/gates/P17_GATES.md | grep -E 'acceptance|INHERITED|NEW|required|measured' | sed 's/^/  /'
echo ""

echo "### 5. does P17.6 need the energy equation WIRED into the solver? ###"
echo "  CompressibleSIMPLE::solve temperature parameter is an INPUT:"
grep -nE 'temperature is fixed for this entire solve|no energy-equation' include/cfd/compressible/CompressibleSIMPLE.hpp | sed 's/^/    /'
echo "  P17.5's TotalEnergyState holds no field (stateless converter):"
echo "    ScalarField members in TotalEnergy.hpp: $(grep -cE '^\s+cfd::fields::ScalarField [a-z]+_;' include/cfd/compressible/TotalEnergy.hpp)"
echo "  => section 12 is answerable by EVALUATING the P17.5 state on the converged compressible"
echo "     fields. No implicit coupling is required, so the P17.8 scope overlap P17.5 flagged"
echo "     does NOT materialise for this phase."
echo ""

echo "### 6. the incompressible SIMPLE reference and its settings ###"
echo "  the low-Mach test's flow settings are inherited from the Poiseuille validation:"
grep -nE 'Same settings tests/integration/poiseuille|maxIterations = |velocityRelaxation|pressureRelaxation|velocityTolerance|pressureTolerance|continuityTolerance' $T | head -9 | sed 's/^/    /'
echo ""

echo "### 7. SIMPLE reference cases that must stay preserved ###"
for f in tests/integration/poiseuille/test_poiseuille_validation.cpp \
         tests/integration/cavity/test_cavity_ghia.cpp; do
  [ -f "$f" ] && printf '  %-62s %s TESTs\n' "$f" "$(grep -c '^TEST' "$f")"
done
echo "  P13-001 SIMPLE fingerprint probe: results/p13/restart/tools/solve_fingerprint.cpp"
echo ""

echo "### 8. baselines P17.6 must preserve ###"
echo "  Release+CUDA 2642 registered / 2580 executed   (the P17.5 close)"
echo "  CPU-only     2440 registered / 2380 executed"
echo "  compressible 210 · P14 68 · P15 161 · P16 315"
echo ""
echo "done: $(date -u +%FT%TZ)"
} 2>&1 | tee "$out"
