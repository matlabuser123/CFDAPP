#!/usr/bin/env bash
# P17.6 gate freeze. Run BEFORE any production edit.
set -uo pipefail
ROOT=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
P17=$ROOT/results/p17/low_mach
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
echo "P17.6 GATE FREEZE"
echo "date: $(date -u +%FT%TZ)"
echo "HEAD: $(git rev-parse HEAD)"
echo ""

echo "### the frozen documents ###"
sha256sum "$P17/acceptance_gate.md" "$P17/audit.md" \
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

echo "### files P17.6 MAY create or touch ###"
echo "  NEW  tests/integration/compressible/test_low_mach_family.cpp"
echo "  EDIT tests/integration/compressible/CMakeLists.txt   (one line)"
echo "  NOTHING ELSE. P17.6 is a VALIDATION phase: no production source is expected to change."
echo ""
echo "### the EXISTING low-Mach and MMS tests, which G5 requires UNCHANGED ###"
sha256sum tests/integration/compressible/test_low_mach_regression.cpp           tests/integration/mms/test_mms_compressible.cpp
echo ""
echo "### the incompressible SIMPLE references, which must not move ###"
sha256sum tests/integration/poiseuille/test_poiseuille_validation.cpp           tests/integration/cavity/test_cavity_ghia.cpp           src/pressure_velocity/SIMPLE.cpp include/cfd/pressure_velocity/SIMPLE.hpp
echo ""
echo "### P17.1-P17.5 production files, which must not move ###"
sha256sum src/compressible/CompressibleSIMPLE.cpp src/compressible/CompressibleMassFlux.cpp           src/compressible/CompressibleContinuity.cpp src/compressible/IdealGasEOS.cpp           src/compressible/ThermodynamicProperties.cpp src/compressible/DilatationalStress.cpp           src/compressible/TotalEnergy.cpp src/compressible/TotalEnergyEquation.cpp           src/compressible/CompressibleRelaxedMomentum.cpp
echo ""
echo "### baselines P17.6 must preserve -- the P17.5 CLOSE, not an earlier phase ###"
echo "  P17_CODE_FINGERPRINT at the P17.5 close : $(code_fp)"
echo "  Release+CUDA 2642 registered / 2580 executed"
echo "  CPU-only     2440 registered / 2380 executed"
echo "  compressible 210 (176 + P17.5's 34) · P14 68 · P15 161 · P16 315"
for f in tests/integration/compressible/test_low_mach_regression.cpp          tests/integration/poiseuille/test_poiseuille_validation.cpp          tests/integration/cavity/test_cavity_ghia.cpp; do
  printf '  %-62s %s TESTs
' "$f" "$(grep -c '^TEST' "$f")"
done
echo ""
echo "### derived thresholds, from logs/01_dryrun.log ###"
echo "  INHERITED VERBATIM from G5: machMax<0.1, >1.25*inletMach, drho/rho<1e-2,"
echo "                              maxEosError<1e-9, fluxRelDiff<0.15, massImbalance<=bound"
echo "  DERIVED here:"
echo "    keRelDiff < 1e-3 at every level            measured 1.6233e-4 .. 1.9409e-5"
echo "    drho/rho exponent in [0.80, 1.25]          theory = 1, measured 1.0178"
echo "    every other error exponent > 0.5           measured 0.88 .. 1.04"
echo "    all error metrics strictly DECREASING      monotone across 4 levels"
echo "    momentum viscous/Poiseuille within 10%     measured 1.0713 .. 1.0559, approaching 1"
echo "    T recovery error == 0                      measured exactly 0"
echo "    energy BALANCE reported, NOT gated below S/N 1  measured S/N 46.97 .. 0.82"
echo ""
echo "done: $(date -u +%FT%TZ)"
} 2>&1 | tee "$out"
