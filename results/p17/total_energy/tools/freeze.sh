#!/usr/bin/env bash
# P17.5 gate freeze. Run BEFORE any production edit.
set -uo pipefail
ROOT=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
P17=$ROOT/results/p17/total_energy
cd "$ROOT" || exit 1
export PATH="/usr/local/cuda-12.9/bin:$PATH"
mkdir -p "$P17/logs"

DOCEXC=(. ':(exclude)results/' ':(exclude)TODO.md' ':(exclude)ROADMAP.md' ':(exclude)CLAUDE.md'
        ':(exclude)README.md' ':(exclude)CONTRIBUTING.md' ':(exclude)QUALITY_GATE.md'
        ':(exclude)PROJECT_STRUCTURE.md' ':(exclude)RELEASE_NOTES_*.md' ':(exclude)docs/')

code_fp() {
  local T U
  T=$(git diff --binary HEAD -- "${DOCEXC[@]}" | sha256sum | awk '{print $1}')
  U=$(git ls-files --others --exclude-standard -- "${DOCEXC[@]}" \
      | LC_ALL=C sort | while read -r f; do sha256sum "$f"; done | sha256sum | awk '{print $1}')
  printf '%s %s' "$T" "$U" | sha256sum | awk '{print $1}'
}

out=$P17/logs/02_freeze.log
{
echo "P17.5 GATE FREEZE"
echo "date: $(date -u +%FT%TZ)"
echo "HEAD: $(git rev-parse HEAD)"
echo ""

echo "### the frozen documents ###"
sha256sum "$P17/acceptance_gate.md" "$P17/audit.md" "$P17/thermodynamic_contract.md" \
          "$P17/tools/dryrun.cpp" | sed "s|$P17/||"
echo ""

echo "### NOT to be touched -- P15 thermodynamics (SHA-256, never git diff HEAD) ###"
sha256sum include/cfd/thermal/Enthalpy.hpp src/thermal/Enthalpy.cpp \
          include/cfd/thermal/EnergyEquation.hpp src/thermal/EnergyEquation.cpp \
          include/cfd/thermal/ThermalProperties.hpp \
          include/cfd/thermal/TemperatureDependentProperty.hpp
echo ""

echo "### NOT to be touched -- P17.2 EOS and P17.3 mass flux ###"
sha256sum include/cfd/compressible/IdealGasEOS.hpp src/compressible/IdealGasEOS.cpp \
          include/cfd/compressible/ThermodynamicProperties.hpp \
          src/compressible/ThermodynamicProperties.cpp \
          include/cfd/compressible/CompressibleMassFlux.hpp \
          src/compressible/CompressibleMassFlux.cpp
echo ""

echo "### NOT to be touched -- P17.4 momentum ###"
sha256sum include/cfd/compressible/DilatationalStress.hpp src/compressible/DilatationalStress.cpp \
          include/cfd/physics/MomentumEquation.hpp src/physics/MomentumEquation.cpp
echo ""

echo "### files P17.5 MAY create or touch ###"
echo "  NEW  include/cfd/compressible/TotalEnergy.hpp"
echo "  NEW  src/compressible/TotalEnergy.cpp"
echo "  NEW  include/cfd/compressible/TotalEnergyEquation.hpp"
echo "  NEW  src/compressible/TotalEnergyEquation.cpp"
echo "  NEW  tests/unit/compressible/test_total_energy.cpp"
echo "  EDIT src/CMakeLists.txt        (one line, in the MAIN list, before"
echo "                                  if(NOT CFDAPP_ENABLE_CUDA) -- the CPU-only blind spot)"
echo "  EDIT tests/unit/compressible/CMakeLists.txt  (if the test list is explicit)"
echo "  current src/CMakeLists.txt sha256:"
sha256sum src/CMakeLists.txt | sed 's/^/    /'
echo ""

echo "### baselines P17.5 must preserve ###"
echo "  P17_CODE_FINGERPRINT at the P17.4 close : $(code_fp)"
echo "  Release+CUDA 2608 registered / 2546 executed"
echo "  CPU-only     2406 registered / 2346 executed"
echo "  compressible 176 · P14 68 · P15 161 · P16 315"
for f in tests/unit/thermal/test_enthalpy_properties.cpp \
         tests/unit/compressible/test_thermodynamic_properties.cpp \
         tests/unit/compressible/test_ideal_gas_eos.cpp \
         tests/unit/compressible/test_compressible_momentum.cpp; do
  printf '  %-58s %s TESTs\n' "$f" "$(grep -c '^TEST' "$f")"
done
echo ""
echo "### derived thresholds, from logs/01_dryrun.log ###"
echo "  algebraic identities (T1-T5, T8, C1, C4, E5, E7, G5)  : EXACTLY 0"
echo "  identities through a division or product (T6, T7, C7) : <= 4 eps * |quantity|"
echo "  round trips (C2, C5, C6, E3, E8)                      : <= 8 eps relative"
echo "  temperature recovery (C3, C5)                         : <= 1e-12 K, INHERITED from P15"
echo "  conservation (G2, G3, G4)                             : kAccum * eps * N * scale, kAccum = 8 EMPIRICAL"
echo ""
echo "done: $(date -u +%FT%TZ)"
} 2>&1 | tee "$out"
