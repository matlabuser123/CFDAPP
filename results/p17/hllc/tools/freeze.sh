#!/usr/bin/env bash
# P17.7 gate freeze. Run BEFORE any implementation source exists.
set -uo pipefail
ROOT=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
P17=$ROOT/results/p17/hllc
cd "$ROOT" || exit 1
mkdir -p "$P17/logs"

log=$P17/logs/02_freeze.log
{
echo "P17.7 GATE FREEZE"
echo "date: $(date -u +%FT%TZ)"
echo "HEAD: $(git rev-parse HEAD)"
echo ""

echo "### the frozen documents ###"
(cd "$P17" && sha256sum acceptance_gate.md audit.md tools/exact_riemann.py tools/audit.sh)
echo ""

echo "### proof that no implementation exists yet -- the gate is frozen FIRST ###"
for f in include/cfd/compressible/HllcFlux.hpp src/compressible/HllcFlux.cpp \
         cuda/kernels/DeviceHllcFluxKernel.cu tests/unit/compressible/test_hllc_flux.cpp; do
  printf '  %-52s %s\n' "$f" "$([ -e "$f" ] && echo '*** ALREADY EXISTS ***' || echo 'absent, as required')"
done
echo ""

echo "### NOT to be touched -- P17.2 EOS / thermodynamics (SHA-256, never git diff HEAD) ###"
sha256sum include/cfd/compressible/IdealGasEOS.hpp src/compressible/IdealGasEOS.cpp \
          include/cfd/compressible/ThermodynamicProperties.hpp \
          src/compressible/ThermodynamicProperties.cpp \
          include/cfd/compressible/EquationOfState.hpp
echo ""
echo "### NOT to be touched -- P17.5 total energy (the conversion P17.7 REUSES) ###"
sha256sum include/cfd/compressible/TotalEnergy.hpp src/compressible/TotalEnergy.cpp \
          include/cfd/compressible/TotalEnergyEquation.hpp src/compressible/TotalEnergyEquation.cpp
echo ""
echo "### NOT to be touched -- P17.3/P17.4 and both SIMPLE paths (section 35) ###"
sha256sum src/compressible/CompressibleMassFlux.cpp src/compressible/CompressibleContinuity.cpp \
          src/compressible/CompressibleRelaxedMomentum.cpp src/compressible/CompressibleSIMPLE.cpp \
          src/compressible/DilatationalStress.cpp \
          src/pressure_velocity/SIMPLE.cpp include/cfd/pressure_velocity/SIMPLE.hpp
echo ""
echo "### NOT to be touched -- the core types P17.7 must NOT modify ###"
sha256sum include/cfd/core/Vector3.hpp include/cfd/core/Vector2.hpp include/cfd/core/Types.hpp
echo ""
echo "### NOT to be touched -- the validation suites section 39 protects ###"
sha256sum tests/integration/compressible/test_low_mach_regression.cpp \
          tests/integration/compressible/test_low_mach_family.cpp \
          tests/integration/mms/test_mms_compressible.cpp \
          tests/integration/poiseuille/test_poiseuille_validation.cpp \
          tests/integration/cavity/test_cavity_ghia.cpp
echo ""

echo "### the frozen baseline counts, from the P17.6 close ###"
echo "  Release+CUDA   2657 registered / 2595 executed"
echo "  CPU-only       2455 registered / 2395 executed"
echo "  (results/p17/low_mach/regression.md -- every one matched its prediction exactly)"
echo ""

echo "### the reference's own key values, frozen so they cannot drift ###"
echo "  gas: R = 287.05, cp = 1005, cv = 717.95, gamma = 1.399818928894769830768"
echo "  signal bounding, measured BEFORE implementation:"
echo "    Davis (10.48)     FAILS in 8 of 14 cases   -> rejected, kept as control V1"
echo "    Toro PVRS (10.59) passes 14 of 14          -> the production estimator"
echo "  HLLC == exact Godunov flux (|diff|/scale) for the algebraically exact cases:"
echo "    uniform x / oblique, stationary + moving contact, supersonic L/R, near-sonic:  0.0"
echo "    strong density ratio:                                    7.97e-65 / 1.44e-70"
echo "  the scheme's O(1) error on strong discontinuities, REPORTED not gated:"
echo "    sod_literature         0.0061 mass  0.0609 momentum  0.2961 energy"
echo "    strong_pressure_ratio  0.0768       0.1026           0.3177"
echo "    low_pressure           0.0301       0.0617           0.2565"
echo "  the V2 control's measured signature (contact wave deleted):"
echo "    stationary contact spurious mass flux  243.4595971726513 kg/(m^2 s)  vs a true 0"
echo "    strong density ratio                  11897.60990707693"
echo ""
echo "done: $(date -u +%FT%TZ)"
} 2>&1 | tee "$log"
