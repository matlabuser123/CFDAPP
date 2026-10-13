#!/usr/bin/env bash
# P17.9 GATE FREEZE. Run AFTER the dry-run passes and BEFORE any benchmark source is written.
set -uo pipefail
cd /mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp || exit 1
P17=results/p17/shock_validation

echo "P17.9 GATE FREEZE"
echo "date: $(date -u +%FT%TZ)"
echo "HEAD: $(git rev-parse HEAD)"
echo ""

echo "### the frozen documents ###"
(cd "$P17" && sha256sum acceptance_gate.md configuration.md \
   tools/riemann_reference.py tools/dry_run_detectors.py tools/audit.sh)
echo ""

echo "### the dry-run that justified the freeze ###"
echo "  logs/01_dryrun.log                 $(sha256sum "$P17/logs/01_dryrun.log" | awk '{print $1}')"
echo "  exit status recorded in the log; it passed with 0 failures."
echo "  PRESERVED_01_dryrun_detectors_broken.log holds the FAILING first attempt, which found"
echo "  three detector defects. None of them was fixed by moving a threshold:"
echo "    1. the fan detector swept in the star-right region      -> confined to x < contact"
echo "    2. the shock detector reported a position on a slope    -> plateau discriminator added"
echo "    3. the fan TAIL was fooled by the contact's smearing    -> first arrival, not last departure"
echo ""

echo "### proof that no benchmark implementation exists yet ###"
for f in tests/integration/compressible/test_shock_validation.cpp \
         tests/unit/compressible/test_wave_detectors.cpp \
         include/cfd/validation/ShockDetectors.hpp; do
  if [ -e "$f" ]; then echo "  *** $f EXISTS -- the freeze is not clean ***"; else echo "  $f  absent, as required"; fi
done
echo ""

echo "### the P17.1-P17.8 production sources that must stay UNTOUCHED (criterion A2) ###"
for f in include/cfd/compressible/DensityBasedSolver.hpp src/compressible/DensityBasedSolver.cpp \
         include/cfd/compressible/HllcFlux.hpp src/compressible/HllcFlux.cpp \
         include/cfd/compressible/TotalEnergy.hpp src/compressible/TotalEnergy.cpp \
         include/cfd/compressible/IdealGasEOS.hpp src/compressible/IdealGasEOS.cpp \
         include/cfd/compressible/ThermodynamicProperties.hpp src/compressible/ThermodynamicProperties.cpp \
         src/compressible/CompressibleSIMPLE.cpp include/cfd/compressible/CompressibleSIMPLE.hpp \
         src/pressure_velocity/SIMPLE.cpp include/cfd/pressure_velocity/SIMPLE.hpp \
         src/app/ProjectRunner.cpp include/cfd/app/ProjectRunner.hpp \
         include/cfd/solver/CFL.hpp cuda/kernels/DeviceHllcFluxKernel.cu; do
  printf '  %-62s %s\n' "$f" "$(sha256sum "$f" | awk '{print $1}')"
done
echo ""

echo "### the P17.8 baseline this phase must not move ###"
echo "  Release+CUDA  2732 registered / 2670 executed"
echo "  CPU-only      2526 registered / 2466 executed"
echo "  P13-001 SIMPLE fingerprint: BITWISE IDENTICAL, 0-byte diff"
echo ""
echo "done: $(date -u +%FT%TZ)"
