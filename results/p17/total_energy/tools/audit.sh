#!/usr/bin/env bash
# P17.5 audit capture -- the UNCHANGED tree, before any total-energy code exists.
# Read-only on production source.
set -uo pipefail
ROOT=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
P17=$ROOT/results/p17/total_energy
cd "$ROOT" || exit 1
export PATH="/usr/local/cuda-12.9/bin:$PATH"
mkdir -p "$P17/logs"

out=$P17/logs/00_audit.log
{
echo "P17.5 AUDIT -- existing energy infrastructure, unchanged tree"
echo "date: $(date -u +%FT%TZ)"
echo "HEAD: $(git rev-parse --short=12 HEAD)"
echo ""

echo "### 1. does a total-energy state exist at all? (0 = MISSING) ###"
for s in rhoE totalEnergy internalEnergy specificEnergy totalEnthalpy eTotal hTotal \
         pressureWork viscousWork viscousDissipation; do
  printf '    %-22s %s\n' "$s" "$(grep -rliI -- "$s" include/ src/ 2>/dev/null | wc -l)"
done
echo ""

echo "### 2. what P15 energy infrastructure DOES exist ###"
for s in EnergyEquation SensibleEnthalpy TemperatureDependentProperty ThermalProperties \
         TurbulentConductivity assembleThermalConvectionContribution \
         assembleThermalDiffusionContribution assembleThermalTransientContribution; do
  printf '    %-42s %s\n' "$s" "$(grep -rliI -- "$s" include/ src/ 2>/dev/null | wc -l)"
done
echo ""

echo "### 3. THE KEY QUESTION: what does the production energy equation transport? ###"
echo "  'cp*T' mentions in the energy equation header:"
grep -c 'cp\*T\|cp \* T' include/cfd/thermal/EnergyEquation.hpp | sed 's/^/    /'
echo "  Enthalpy.hpp's own verdict on that form:"
grep -nE 'h    = cp \* T|wrong when cp varies' include/cfd/thermal/Enthalpy.hpp | sed 's/^/    /'
echo ""
echo "  SensibleEnthalpy consumers (production vs test):"
for d in src/io src/app src/thermal src/compressible tests; do
  printf '    %-20s %s\n' "$d" "$(grep -rliI 'SensibleEnthalpy' "$d" 2>/dev/null | wc -l)"
done
echo "  => library-only if src/io and src/app are both 0: a case cannot reach it."
echo ""

echo "### 4. does the compressible solver have an energy equation? ###"
echo "  CompressibleSIMPLE scope statement:"
grep -nE 'never updated by this solver itself|no energy-equation|out of scope' \
  include/cfd/compressible/CompressibleSIMPLE.hpp | sed 's/^/    /'
echo "  energy/rhoE references inside the compressible module:"
for s in rhoE totalEnergy EnergyEquation SensibleEnthalpy; do
  printf '    %-20s %s\n' "$s" \
    "$(grep -rliI -- "$s" include/cfd/compressible/ src/compressible/ 2>/dev/null | wc -l)"
done
echo ""

echo "### 5. the DUAL-cp hazard, concretely ###"
echo "  the two independent case keys:"
grep -rn '"specific_heat"' src/io/case/PhysicsConfigParser.cpp | head -2 | sed 's/^/    /'
grep -rn '"specific_heat_pressure"' src/io/case/PhysicsConfigParser.cpp | head -2 | sed 's/^/    /'
echo "  any code that cross-validates the two: (excluding parse/write/plumbing)"
n=$(grep -rn 'specificHeatPressure' src/ include/ 2>/dev/null \
    | grep -viE 'ThermodynamicProperties\.(hpp|cpp)|PhysicsConfigParser|CaseWriter|JSONWriter|CaseBuilder|PhysicsConfig\.hpp|ProjectRunner' | wc -l)
echo "    cross-validation sites: $n   (0 = nothing detects a disagreement)"
echo "  how they meet at runtime, in ProjectRunner:"
grep -nE 'temperatureField\[i\] = thermalResult|c.thermodynamics' src/app/ProjectRunner.cpp \
  | head -4 | sed 's/^/    /'
echo "    => in a thermal-coupled compressible case, T is produced by a P15 solve using"
echo "       thermal.specific_heat and then consumed by thermodynamics built from"
echo "       compressible.specific_heat_pressure. Nothing requires them to agree."
echo ""

echo "### 6. GPU: is there ANY compressible or energy path to be equivalent to? ###"
for s in compressible IdealGas EquationOfState ThermodynamicProperties Enthalpy rhoE \
         totalEnergy machNumber; do
  printf '    %-24s %s\n' "$s" "$(grep -rliI -- "$s" cuda/ include/cfd/gpu/ 2>/dev/null | wc -l)"
done
echo "  the single 'compressible' hit, in context:"
grep -rniI 'compressible' cuda/ include/cfd/gpu/ 2>/dev/null | head -2 | sed 's/^/    /'
echo "  GPU scalar-transport kernels that DO exist (the nearest reusable machinery):"
ls cuda/kernels/ | grep -iE 'scalar|convection|diffusion' | sed 's/^/    /'
echo ""

echo "### 7. thermodynamic authorities to reuse, NOT duplicate ###"
printf '    %-46s %s\n' "IdealGasEOS (P17.2)" "$(ls include/cfd/compressible/IdealGasEOS.hpp 2>/dev/null)"
printf '    %-46s %s\n' "ThermodynamicProperties R/cp/cv/gamma (P17.2)" "$(ls include/cfd/compressible/ThermodynamicProperties.hpp 2>/dev/null)"
printf '    %-46s %s\n' "SensibleEnthalpy h(T), T(h) (P15)" "$(ls include/cfd/thermal/Enthalpy.hpp 2>/dev/null)"
printf '    %-46s %s\n' "ThermalProperties k, cp (P15)" "$(ls include/cfd/thermal/ThermalProperties.hpp 2>/dev/null)"
printf '    %-46s %s\n' "compressible mass flux (P17.3)" "$(ls include/cfd/compressible/CompressibleMassFlux.hpp 2>/dev/null)"
printf '    %-46s %s\n' "dilatational stress (P17.4)" "$(ls include/cfd/compressible/DilatationalStress.hpp 2>/dev/null)"
echo ""
echo "  SensibleEnthalpy inversion tolerance, as the class documents it:"
grep -n 'kInversionToleranceKelvin' include/cfd/thermal/Enthalpy.hpp | sed 's/^/    /'
echo ""

echo "### 8. test-suite sizes that P17.5 must preserve ###"
for f in tests/unit/thermal/test_enthalpy_properties.cpp \
         tests/unit/compressible/test_thermodynamic_properties.cpp \
         tests/unit/compressible/test_ideal_gas_eos.cpp; do
  [ -f "$f" ] && printf '    %-58s %s TESTs\n' "$f" "$(grep -c '^TEST' "$f")"
done
echo ""
echo "done: $(date -u +%FT%TZ)"
} 2>&1 | tee "$out"
