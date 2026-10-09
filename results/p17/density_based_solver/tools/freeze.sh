#!/usr/bin/env bash
# P17.8 gate freeze. Run BEFORE any implementation source exists.
set -uo pipefail
ROOT=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
P17=$ROOT/results/p17/density_based_solver
cd "$ROOT" || exit 1
mkdir -p "$P17/logs"

log=$P17/logs/02_freeze.log
{
echo "P17.8 GATE FREEZE"
echo "date: $(date -u +%FT%TZ)"
echo "HEAD: $(git rev-parse HEAD)"
echo ""

echo "### the frozen documents ###"
(cd "$P17" && sha256sum acceptance_gate.md architecture.md audit.md tools/audit.sh)
echo ""

echo "### proof that no implementation exists yet ###"
for f in include/cfd/compressible/DensityBasedSolver.hpp src/compressible/DensityBasedSolver.cpp \
         tests/unit/compressible/test_density_based_solver.cpp \
         tests/integration/compressible/test_density_based_canonical.cpp; do
  printf '  %-62s %s\n' "$f" \
    "$([ -e "$f" ] && echo '*** ALREADY EXISTS ***' || echo 'absent, as required')"
done
echo ""

echo "### A1/A2/A5: the files that MUST stay untouched -- the dispatch and both SIMPLE paths ###"
sha256sum src/app/ProjectRunner.cpp include/cfd/app/ProjectRunner.hpp \
          src/pressure_velocity/SIMPLE.cpp include/cfd/pressure_velocity/SIMPLE.hpp \
          src/compressible/CompressibleSIMPLE.cpp include/cfd/compressible/CompressibleSIMPLE.hpp
echo ""
echo "### A5: the EXISTING CFL and transient facilities -- extended, never modified ###"
sha256sum include/cfd/solver/CFL.hpp src/solver/CFL.cpp \
          include/cfd/solver/TransientSolver.hpp src/solver/TransientSolver.cpp
echo ""
echo "### A6: P17.1 - P17.7 production, and the core types ###"
sha256sum include/cfd/compressible/HllcFlux.hpp src/compressible/HllcFlux.cpp \
          include/cfd/gpu/DeviceHllcFlux.hpp cuda/kernels/DeviceHllcFluxKernel.cu \
          include/cfd/compressible/TotalEnergy.hpp src/compressible/TotalEnergy.cpp \
          include/cfd/compressible/TotalEnergyEquation.hpp src/compressible/TotalEnergyEquation.cpp \
          include/cfd/compressible/IdealGasEOS.hpp src/compressible/IdealGasEOS.cpp \
          include/cfd/compressible/ThermodynamicProperties.hpp \
          src/compressible/ThermodynamicProperties.cpp \
          src/compressible/CompressibleMassFlux.cpp src/compressible/CompressibleContinuity.cpp \
          src/compressible/CompressibleRelaxedMomentum.cpp src/compressible/DilatationalStress.cpp \
          include/cfd/core/Vector3.hpp include/cfd/core/Types.hpp
echo ""
echo "### the validation suites section 44 protects ###"
sha256sum tests/integration/compressible/test_low_mach_regression.cpp \
          tests/integration/compressible/test_low_mach_family.cpp \
          tests/unit/compressible/test_hllc_flux.cpp \
          tests/unit/gpu/test_hllc_gpu_equivalence.cpp \
          tests/integration/mms/test_mms_compressible.cpp \
          tests/integration/poiseuille/test_poiseuille_validation.cpp \
          tests/integration/cavity/test_cavity_ghia.cpp \
          tests/unit/solver/test_cfl.cpp
echo ""

echo "### the frozen baseline counts, from the P17.7 close ###"
echo "  Release+CUDA   2696 registered / 2634 executed"
echo "  CPU-only       2490 registered / 2430 executed"
echo "  (results/p17/hllc/regression.md -- every one matched its prediction exactly)"
echo ""

echo "### THE SCOPE DECISIONS, frozen with the gate (architecture.md) ###"
echo "  Euler-only, inviscid                 -- NO viscous stress, NO conduction, NS NOT claimed"
echo "  GPU density-based solver             -- NOT IMPLEMENTED (G8 is not in P17.8's gate row)"
echo "  restart                              -- UNSUPPORTED, with an explicit refusal (section 29)"
echo "  reachability                         -- LIBRARY-LEVEL; not reachable from a case file"
echo "  dispatch                             -- UNCHANGED; no case-format key added"
echo "  boundary states                      -- inviscid wall, supersonic inflow, supersonic outflow"
echo "  time integration                     -- single-stage explicit Euler; global AND local modes"
echo "  reconstruction                       -- first-order piecewise constant (P17.7's)"
echo ""
echo "### the key derived relation the gate rests on (criterion K4) ###"
echo "  P17.8's dt fed back through the EXISTING solver::calculateCFL must give Co = CFL / 2,"
echo "  because that function carries a documented 1/2 factor. Asserting Co == CFL would be wrong."
echo ""
echo "done: $(date -u +%FT%TZ)"
} 2>&1 | tee "$log"
