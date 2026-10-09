#!/usr/bin/env bash
# P17.10 -- integrity audit of this phase's own evidence.
#
# WRITTEN FROM SCRATCH FOR P17.10, not renamed from P17.9's. That is a deliberate response to a
# recorded failure: P17.9's integrity script WAS a sed-rename of P17.8's, and it PASSED while
# describing P17.8's test files and P17.8's markers. A script that narrates the wrong phase and
# reports success is worse than no script, because it is quoted later as evidence.
#
# Every check below names a P17.10 artefact explicitly. Every count that is expected to be POSITIVE
# is asserted to be positive, because a grep that returns 0 for a renamed symbol prints exactly what
# success looks like.
set -uo pipefail
cd /mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp || exit 1
P17=results/p17/cpu_gpu_equivalence
FAIL=0

ok()   { printf '  PASS  %s\n' "$1"; }
bad()  { printf '  FAIL  %s\n' "$1"; FAIL=$((FAIL+1)); }

# expect_hash <file> <expected sha256> <label>
expect_hash() {
  if [ ! -f "$1" ]; then bad "$3: $1 does not exist"; return; fi
  local live
  live=$(sha256sum "$1" | awk '{print $1}')
  if [ "$live" = "$2" ]; then ok "$3 unmoved ($2)"; else bad "$3 MOVED: $live != $2"; fi
}

# expect_count <actual> <expected> <label>
expect_count() {
  if [ "$1" = "$2" ]; then ok "$3 = $1"; else bad "$3 = $1, expected $2"; fi
}

# expect_positive <actual> <label> -- for any count whose zero would be a silent probe failure
expect_positive() {
  if [ "${1:-0}" -gt 0 ] 2>/dev/null; then ok "$2 = $1 (positive)"; else bad "$2 = ${1:-0}, which is the fail-open value"; fi
}

echo "P17.10 integrity audit -- $(date -u +%FT%TZ)"
echo ""

echo "### 1. The frozen documents of THIS phase"
expect_hash "$P17/acceptance_gate.md"  c5b86211dce2128152f5fe8d5a93b8ba739fc596da562f485af18d3ce6efbb51 "P17.10 acceptance_gate.md"
expect_hash "$P17/tolerance_policy.md" e69d8ef2b8cb5f8c7bac320ab7b153dd0b914a7f658718f2f774845863b8d116 "P17.10 tolerance_policy.md"
expect_hash "$P17/kernel_inventory.md" 953efdeeaa91e67195a6472d206476d0fe5e5f946c435b9134c4a11fba1e7536 "P17.10 kernel_inventory.md"
expect_hash "$P17/audit.md"            478933fcc859d8f2f9a3d618dfce371690e272b5e1bd52370dc504f086c98fc9 "P17.10 audit.md"
echo ""

echo "### 2. The INHERITED frozen gates, which P17.10 must not have touched"
expect_hash results/p17/density_based_solver/acceptance_gate.md a88738e481b3964d2a962e9eb83d7dda92c0d9142f997547673dc3cddabe615e "P17.8 gate"
expect_hash results/p17/shock_validation/acceptance_gate.md     b7185ceac2c0a79c103679218c14c1a17b70b7fc6acca626807dc2ade0910f40 "P17.9 gate"
for a in A1_stationary_contact_width A2_rarefaction_head_scaling A3_plateau_contamination coverage_map; do
  if [ -f "results/p17/shock_validation/amendments/$a.md" ]; then
    ok "P17.9 amendment present: $a.md"
  else
    bad "P17.9 amendment MISSING: $a.md"
  fi
done
echo ""

echo "### 3. Production sources P17.10 must NOT have modified"
expect_hash src/compressible/DensityBasedSolver.cpp 53e84ac7b5f53c6d6704e8a7a77281dd678ae18f450d74862abcce60d8421326 "DensityBasedSolver.cpp"
expect_hash include/cfd/compressible/HllcFlux.hpp   fc4a8db5f98568923ffe320b6adb3b6574f855cf538bb238ba9385bd046b61f0 "HllcFlux.hpp"
expect_hash src/compressible/TotalEnergy.cpp        23bde46f7a94d7e24ed262ef3ae5f4cc5677bb9194e01399014b12d929df16c5 "TotalEnergy.cpp"
echo ""

echo "### 4. The sources P17.10 DID add exist and are non-trivial"
for f in include/cfd/gpu/DeviceThermodynamics.hpp \
         include/cfd/gpu/DeviceDensityBasedSolver.hpp \
         cuda/kernels/DeviceDensityBasedKernel.cu \
         tests/integration/compressible/EquivalenceCompare.hpp \
         tests/integration/compressible/test_p17_10_dryrun.cpp \
         tests/unit/compressible/test_device_thermodynamics_equivalence.cpp \
         tests/unit/gpu/test_density_based_gpu_equivalence.cpp; do
  if [ -f "$f" ]; then
    n=$(wc -l < "$f")
    if [ "$n" -gt 50 ]; then ok "$f ($n lines)"; else bad "$f exists but is only $n lines"; fi
  else
    bad "$f is MISSING"
  fi
done
echo ""

echo "### 5. Test counts -- the numbers regression.md predicts from"
expect_count "$(grep -c '^TEST(' tests/integration/compressible/test_p17_10_dryrun.cpp)" 8 "dryrun TEST macros"
expect_count "$(grep -c '^TEST(' tests/unit/compressible/test_device_thermodynamics_equivalence.cpp)" 6 "thermo-equivalence TEST macros"
expect_count "$(grep -c '^TEST(' tests/unit/gpu/test_density_based_gpu_equivalence.cpp)" 17 "gpu-equivalence TEST macros"
echo ""

echo "### 6. The six kernels are all present, and the gather really has no atomics"
for k in recoverKernel faceFluxKernel gatherResidualKernel localTimeStepKernel \
         minReduceKernel minIndexReduceKernel applyUpdateKernel fillKernel; do
  expect_positive "$(grep -c "__global__ void $k" cuda/kernels/DeviceDensityBasedKernel.cu)" "kernel $k defined"
done
ATOMICS=$(grep -cE 'atomicAdd|atomicSub|atomicExch|atomicCAS|atomicMin|atomicMax' cuda/kernels/DeviceDensityBasedKernel.cu)
expect_count "$ATOMICS" 0 "atomic operations in the new kernel file"
# ...and the CONTROL for that zero: the pattern must be able to find an atomic when one exists.
CONTROL=$(grep -crE 'atomicAdd|atomicSub|atomicExch|atomicCAS|atomicMin|atomicMax' cuda/kernels/ | grep -v ':0$' | wc -l)
expect_positive "$CONTROL" "other cuda/kernels files that DO contain atomics (proves the pattern works)"
echo ""

echo "### 7. --fmad=false is applied to the new translation unit"
expect_positive "$(grep -c 'kernels/DeviceDensityBasedKernel.cu' cuda/CMakeLists.txt)" "kernel referenced in cuda/CMakeLists.txt"
if grep -A1 'set_source_files_properties(kernels/DeviceDensityBasedKernel.cu PROPERTIES' cuda/CMakeLists.txt \
     | grep -q -- '--fmad=false'; then
  ok "--fmad=false set on DeviceDensityBasedKernel.cu"
else
  bad "--fmad=false is NOT set on DeviceDensityBasedKernel.cu"
fi
echo ""

echo "### 8. Every log the evidence cites exists and is non-empty"
for l in 00_audit.log 01_dryrun.log 02_freeze.log 04_thermo_equiv.log 05_gpu_equiv.log \
         07_dryrun_and_finding.log 08_new_criteria.log 24_sanitizers_summary.log \
         34_fma_summary.log 50_p17_10_regress.log; do
  if [ -s "$P17/logs/$l" ]; then ok "logs/$l ($(wc -l < "$P17/logs/$l") lines)"; else bad "logs/$l missing or empty"; fi
done
echo ""

echo "### 9. Headline numbers quoted in the evidence are actually IN the logs"
check_in_log() {   # check_in_log <log> <pattern> <label>
  local n
  n=$(grep -ac -- "$2" "$P17/logs/$1")
  if [ "$n" -gt 0 ]; then ok "$3"; else bad "$3 -- not found in logs/$1"; fi
}
check_in_log 01_dryrun.log        'TOTAL cell-face references examined: 702' "dry-run: 702 ascending cell-face references"
check_in_log 05_gpu_equiv.log     'A7 recovery: 1736 values compared, 0 differing' "A7: 1736 values, 0 differing"
check_in_log 05_gpu_equiv.log     'volumeRatio=6849.147' "B4: the graded mesh's 6849:1 volume ratio"
check_in_log 05_gpu_equiv.log     'D2: no divergence through 50 steps' "D2: no divergence through 50 steps"
check_in_log 05_gpu_equiv.log     'V3 reversed gather order' "V3: the reversed-gather control ran"
check_in_log 05_gpu_equiv.log     'trap6: cpu refused=YES, device refused=YES' "trap 6: both backends refuse"
check_in_log 24_sanitizers_summary.log 'CONTROL OK: memcheck reported the deliberate fault.'  "N1 control fired"
check_in_log 24_sanitizers_summary.log 'CONTROL OK: racecheck reported the deliberate fault.' "N2 control fired"
check_in_log 34_fma_summary.log   'restore verified by sha256' "V7: the fmad experiment restored cleanly"
check_in_log 07_dryrun_and_finding.log 'head-on collisions violate' "the flux finding reproduced"
echo ""

echo "### 10. The finding is recorded AND has an executable reproducer"
if [ -s "$P17/BLOCKING_FINDINGS.md" ]; then ok "BLOCKING_FINDINGS.md present"; else bad "BLOCKING_FINDINGS.md missing"; fi
expect_positive "$(grep -c 'DOCUMENTS_A_DEFECT_ToroWaveSpeedsDoNotBoundAHeadOnCollision' tests/integration/compressible/test_p17_10_dryrun.cpp)" "reproducer test present"
echo ""

echo "### 11. The coverage map admits the gaps rather than only listing passes"
expect_positive "$(grep -c 'NOT MEASURED' $P17/coverage_map.md)" "coverage_map.md contains NOT MEASURED rows"
echo ""

echo "### 12. The FINAL regression, and the two preserved earlier runs"
for f in STALENESS_FAILURE.md HARNESS_FINDINGS.md regression.md; do
  if [ -s "$P17/$f" ]; then ok "$f present"; else bad "$f missing"; fi
done
for l in 41_r3_summary.log 55_runtime_noise.log 50_p17_10_regress.log \
         50_p17_10_regress_FAILED_STALENESS.log 50_p17_10_regress_INVALID_DEBUG_CPUONLY.log; do
  if [ -s "$P17/logs/$l" ]; then ok "logs/$l present"; else bad "logs/$l missing or empty"; fi
done
check_in_log 41_r3_summary.log 'IDENTICAL: every GPU-computed value agrees' "R3 separate-process determinism"
check_in_log 41_r3_summary.log 'CONTROL OK: a one-character perturbation is detected' "R3's control fired"
check_in_log 55_runtime_noise.log 'VERDICT: every changed line under results/validation is a timing value' "runtime-noise audit"
check_in_log 50_p17_10_regress.log '188e50397ed13718f449f369ef996768cc4011ac1048955dc312da92716390f6' "release_cuda libcfdcore.a unchanged"
check_in_log 50_p17_10_regress.log '1305a82aa18ba3a5f139a08d4d165c131a7eb3c3881fcb3f454cc99acc68762a' "cpu_only libcfdcore.a unchanged"

# BOTH configurations of the FINAL run must report 0 failures...
FINAL_OK=$(grep -acE 'failed +: 0 +PREDICTED 0' "$P17/logs/50_p17_10_regress.log")
expect_count "$FINAL_OK" 2 "configurations reporting 0 failures in the FINAL run"
# ...and the CONTROL: the preserved invalid run must still show only ONE such line, because its
# cpu_only configuration reported 3. If this reads 2, the preserved file has been overwritten with
# the good run and the "failures preserved" claim is empty.
INVALID_OK=$(grep -acE 'failed +: 0 +PREDICTED 0' "$P17/logs/50_p17_10_regress_INVALID_DEBUG_CPUONLY.log")
expect_count "$INVALID_OK" 1 "the preserved INVALID run still shows its failing configuration"
echo ""

echo "############################################################"
if [ "$FAIL" -eq 0 ]; then
  echo "INTEGRITY: 0 failures"
else
  echo "INTEGRITY: $FAIL FAILURES -- see above"
fi
echo "############################################################"
exit "$FAIL"
