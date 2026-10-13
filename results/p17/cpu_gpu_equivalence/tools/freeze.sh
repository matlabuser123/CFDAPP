#!/usr/bin/env bash
# P17.10 -- record the frozen hashes, and re-verify the inherited ones.
set -uo pipefail
cd /mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp || exit 1
P17=results/p17/cpu_gpu_equivalence
echo "=== P17.10 frozen at $(date -u +%Y-%m-%dT%H:%M:%SZ) ==="
sha256sum "$P17/acceptance_gate.md" "$P17/tolerance_policy.md" "$P17/kernel_inventory.md" "$P17/audit.md"
echo
echo "=== inherited frozen gates, must be unmoved ==="
sha256sum results/p17/density_based_solver/acceptance_gate.md results/p17/shock_validation/acceptance_gate.md
echo
echo "=== P17.9 amendments, must be present ==="
ls -1 results/p17/shock_validation/amendments/
echo
echo "=== production sources at their P17.8/P17.9 values ==="
sha256sum src/compressible/DensityBasedSolver.cpp include/cfd/compressible/HllcFlux.hpp src/compressible/TotalEnergy.cpp
