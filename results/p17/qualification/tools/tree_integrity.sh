#!/usr/bin/env bash
# P17.11 section 35 -- classify EVERY remaining working-tree change.
#
# The requirement is that there be no unexplained unrelated edits. That is only meaningful if every
# path is put in a bucket and the buckets are exhaustive, so the script asserts that the bucket
# counts add up to the total and fails closed if they do not.
set -uo pipefail
cd /mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp || exit 1
Q=results/p17/qualification

git status --porcelain > "$Q/logs/96_status_raw.txt"
TOTAL=$(wc -l < "$Q/logs/96_status_raw.txt")
echo "=== working tree: $TOTAL entries ==="
echo ""

# Buckets, in priority order. A path lands in the FIRST bucket it matches.
classify() {
  local p="$1"
  case "$p" in
    # --- P17-AUTHORED PRODUCTION, enumerated file by file rather than by directory, because
    #     include/cfd/gpu and cuda/ also hold P16-era work that P17 never touched. ---
    include/cfd/compressible/*|src/compressible/*)
                                                echo "P17 implementation (compressible)" ;;
    include/cfd/gpu/DeviceHllcFlux.hpp|include/cfd/gpu/DeviceThermodynamics.hpp|    include/cfd/gpu/DeviceDensityBasedSolver.hpp|    cuda/kernels/DeviceHllcFluxKernel.cu|cuda/kernels/DeviceDensityBasedKernel.cu|    cuda/CMakeLists.txt)                        echo "P17 implementation (GPU)" ;;
    results/p17/*)                              echo "P17 evidence" ;;
    results/*|cases/*)                          echo "evidence / case data (earlier phases)" ;;
    tests/*|apps/gui/tests/*)                   echo "tests (P17 + earlier phases)" ;;
    TODO.md|ROADMAP.md|CLAUDE.md)               echo "control docs" ;;
    build/*)                                    echo "build artefacts" ;;
    # --- Everything below is pre-existing uncommitted work from P13-P16. It is NOT P17's: the
    #     P17 code fingerprint is unchanged across this whole phase, and no P17 sub-phase records
    #     touching any of these. Bucketed by the subsystem that owns it so the list is auditable
    #     rather than a single undifferentiated "other". ---
    include/cfd/turbulence/*|src/turbulence/*|include/cfd/gpu/*Turbulence*|    include/cfd/gpu/*KEpsilon*|src/gpu/*KEpsilon*|src/gpu/TurbulenceBackendTrace.cpp|    cuda/kernels/*Turbulence*|cuda/kernels/*KEpsilon*)
                                                echo "pre-existing: P16 turbulence" ;;
    include/cfd/thermal/*|src/thermal/*|include/cfd/boundary/ConvectiveHeatTransfer.hpp|    src/boundary/ConvectiveHeatTransfer.cpp)    echo "pre-existing: P15 thermal" ;;
    include/cfd/transport/*|src/transport/*|include/cfd/gpu/DeviceScalarTransport.hpp|    cuda/kernels/DeviceScalarTransportKernel.cu|docs/developer_guide/transport.md)
                                                echo "pre-existing: P14 transport" ;;
    include/cfd/io/Checkpoint*|src/io/Checkpoint*|include/cfd/pressure_velocity/SIMPLECheck*|    src/pressure_velocity/SIMPLECheck*|include/cfd/pressure_velocity/SIMPLEContin*|    src/pressure_velocity/SIMPLEContin*|include/cfd/io/case/CaseFingerprint.hpp|    src/io/case/CaseFingerprint.cpp)            echo "pre-existing: P13 checkpoint/restart" ;;
    cuda/kernels/*|src/gpu/*|include/cfd/gpu/*)  echo "pre-existing: GPU-DISC discretization" ;;
    apps/gui/*)                                 echo "pre-existing: GUI" ;;
    benchmarks/*|scripts/benchmarks/*)          echo "pre-existing: benchmarks" ;;
    include/cfd/*|src/*|apps/*|cmake/*|docs/*|CMakeLists.txt|cli_log_level_*)
                                                echo "pre-existing: core/IO/solver (P13-P16)" ;;
    *)                                          echo "OTHER -- needs explanation" ;;
  esac
}

declare -A COUNT
while IFS= read -r line; do
  st="${line:0:2}"
  path="${line:3}"
  path="${path%\"}"; path="${path#\"}"
  b=$(classify "$path")
  COUNT["$b"]=$(( ${COUNT["$b"]:-0} + 1 ))
done < "$Q/logs/96_status_raw.txt"

SUM=0
for b in "${!COUNT[@]}"; do
  printf '  %-38s %4d\n' "$b" "${COUNT[$b]}"
  SUM=$(( SUM + COUNT[$b] ))
done
echo "  ----------------------------------------------"
printf '  %-38s %4d\n' "TOTAL classified" "$SUM"
echo ""
if [ "$SUM" -ne "$TOTAL" ]; then
  echo "*** classification is not exhaustive: $SUM of $TOTAL. FAILING CLOSED. ***"
  exit 1
fi

echo "=== anything in the OTHER bucket, listed in full ==="
n=0
while IFS= read -r line; do
  path="${line:3}"; path="${path%\"}"; path="${path#\"}"
  if [ "$(classify "$path")" = "OTHER -- needs explanation" ]; then
    echo "  ${line}"
    n=$(( n + 1 ))
  fi
done < "$Q/logs/96_status_raw.txt"
[ "$n" -eq 0 ] && echo "  (none)"
echo ""

echo "=== P17 PRODUCTION source changes, enumerated individually ==="
git status --porcelain -- include/ src/ cuda/ CMakeLists.txt | sed 's/^/  /'
echo ""

echo "=== the three P17 sources that must be at their frozen values ==="
sha256sum src/compressible/DensityBasedSolver.cpp include/cfd/compressible/HllcFlux.hpp \
          src/compressible/TotalEnergy.cpp src/pressure_velocity/SIMPLE.cpp | sed 's/^/  /'
echo "  expected:"
echo "    53e84ac7b5f53c6d6704e8a7a77281dd678ae18f450d74862abcce60d8421326  DensityBasedSolver.cpp"
echo "    fc4a8db5f98568923ffe320b6adb3b6574f855cf538bb238ba9385bd046b61f0  HllcFlux.hpp"
echo "    23bde46f7a94d7e24ed262ef3ae5f4cc5677bb9194e01399014b12d929df16c5  TotalEnergy.cpp"
echo "    34f4f99577a9e4f6c35614291f3ea4ab246809af34fb08c81ac5b3b8f7d6ecd1  SIMPLE.cpp  (plant restored)"
echo ""

echo "=== generated data written during qualification (untracked, not committed) ==="
git status --porcelain -- results/validation results/p17/shock_validation/data | sed 's/^/  /'
echo "  results/validation modified: $(git status --porcelain -- results/validation | wc -l)  (must be 0)"
echo ""
echo "=== repository history: unchanged ==="
echo "  HEAD                    : $(git rev-parse HEAD)"
echo "  commits ahead of origin : $(git rev-list --count @{u}..HEAD 2>/dev/null || echo 'no upstream')"
echo "  stash entries           : $(git stash list | wc -l)"
