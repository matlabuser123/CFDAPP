#!/usr/bin/env bash
# P17.10 -- audit of GPU coverage for every P17.1..P17.9 capability. Run BEFORE any CUDA source is
# written. Section 1 of the authorization.
#
# Every count is paired with the file it came from, and every zero with a sanity control, so an
# absence can be told from a blind grep.
set -uo pipefail
cd /mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp || exit 1

echo "P17.10 AUDIT -- GPU coverage of the P17 capability set"
echo "date: $(date -u +%FT%TZ)"
echo "HEAD: $(git rev-parse HEAD)"
echo ""

echo "### 1. does a GPU DENSITY-BASED SOLVER exist at all? ###"
for pat in 'DensityBasedSolver' 'ConservedField' 'assembleResidual' 'localTimeSteps' 'applyUpdate'; do
  n=$(grep -rl "$pat" cuda/ include/cfd/gpu/ 2>/dev/null | wc -l)
  printf '  %-26s files under cuda/ or include/cfd/gpu/: %s\n' "$pat" "$n"
done
echo "  SANITY -- the same search DOES find the host implementation:"
echo "    DensityBasedSolver under src/ + include/cfd/compressible/: $(grep -rl 'DensityBasedSolver' src/ include/cfd/compressible/ 2>/dev/null | wc -l) files   (must be > 0)"
echo ""

echo "### 2. what the GPU layer DOES have for P17 ###"
echo "  HLLC on device:"
ls -1 include/cfd/gpu/DeviceHllcFlux.hpp cuda/kernels/DeviceHllcFluxKernel.cu 2>/dev/null | sed 's/^/    /'
echo "    entry points:"
grep -nE '^\[\[nodiscard\]\]|^bool ' include/cfd/gpu/DeviceHllcFlux.hpp | sed 's/^/      /'
echo "    formulas restated inside the .cu (must be 0 -- it compiles the shared header):"
echo "      $(grep -cE 'sqrt\(|gamma \*|0\.5 \*' cuda/kernels/DeviceHllcFluxKernel.cu) arithmetic lines"
echo "    shared host/device marker in the flux header:"
echo "      CFD_HLLC_HOST_DEVICE occurrences: $(grep -c 'CFD_HLLC_HOST_DEVICE' include/cfd/compressible/HllcFlux.hpp)"
echo ""
echo "  DeviceMesh -- what topology is already device-resident:"
grep -oE '\[\[nodiscard\]\] const cfd::(Real|Index)\* [a-zA-Z]+\(\)' include/cfd/gpu/DeviceMesh.hpp \
  | sed -E 's/.*\* ([a-zA-Z]+)\(\)/    \1/' | tr '\n' ' ' ; echo
echo ""

echo "### 3. is each cell's face list ASCENDING? (decides whether bitwise residuals are possible) ###"
echo "  MeshGeometry assigns face ids from a monotonically increasing counter and calls addFace as"
echo "  each face is created, so a cell's list can only be ascending. Verified by construction:"
grep -n 'nextFaceId++' src/mesh/MeshGeometry.cpp | head -4 | sed 's/^/    /'
echo "  addFace appends without sorting:"
grep -n 'void Cell::addFace' src/mesh/Cell.cpp | sed 's/^/    /'
echo "  CONSEQUENCE: a per-cell GATHER over cellFaceIds visits faces in the same order the CPU's"
echo "  face loop accumulates them, so the residual can be BITWISE rather than merely close --"
echo "  and no atomics are needed, which is also what makes the GPU residual deterministic."
echo ""

echo "### 4. the numerical policy already established for P17 GPU work ###"
grep -n 'fmad' cuda/CMakeLists.txt | sed 's/^/  /'
echo ""

echo "### 5. thermodynamics on device -- what blocks it ###"
echo "  TotalEnergyState holds SensibleEnthalpy holds TemperatureDependentProperty, which is"
echo "  std::vector-backed and therefore HOST ONLY. But the recovery this solver uses is a CLOSED"
echo "  FORM, so only scalars are needed on device:"
grep -n 'const Real temperature = (specificInternalEnergy' src/compressible/TotalEnergy.cpp | sed 's/^/    /'
grep -n 'out.pressure = thermo_.equationOfState().pressure' src/compressible/TotalEnergy.cpp | sed 's/^/    /'
echo "  std::vector occurrences in TotalEnergy.hpp (why it cannot cross to device as-is):"
echo "    $(grep -c 'std::vector' include/cfd/compressible/TotalEnergy.hpp)"
echo ""

echo "### 6. existing GPU test + diagnostic infrastructure to REUSE ###"
ls -1 tests/unit/gpu/ 2>/dev/null | head -20 | sed 's/^/    /'
echo "  CUDA diagnostic tooling already in the repo:"
grep -rln 'compute-sanitizer' scripts/ results/ .github/ 2>/dev/null | head -6 | sed 's/^/    /'
echo ""
echo "done: $(date -u +%FT%TZ)"
