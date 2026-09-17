#!/usr/bin/env bash
# P12-DIFF-002-INV-002 Step 1: build an ISOLATED variant that restores the PRE-A2 wall-flux gating in
# ONE equation only, so the natural-convection change can be attributed to the thermal wall flux or
# the momentum wall shear separately. 2x2 factorial with the existing baseline (neither) and the
# authoritative tree (both).
#
# Mechanism: A2 made each assembler pass an always-non-null gradient to the BOUNDARY branch while
# keeping the internal-face gate. Restoring the pre-A2 behaviour for one equation = pass that
# equation's INTERNAL (gated) pointer to its boundary call as well. The natural-convection case runs
# non_orthogonal_corrections = 0, so the gated pointer is null and that equation falls back to the
# historical two-point wall treatment.
#
# The authoritative tree is never written to. Usage: build_variant.sh <momentum_old|thermal_old>
set -u
V=$1
R=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
S=$HOME/invf_$V/src
B=$HOME/invf_$V/build
LOG=$R/results/p12-diff-002/investigation-f/logs/10_variant_$V.log
mkdir -p "$S" "$B" "$(dirname "$LOG")"
{
  echo "# Step 1 isolated variant '$V'; $(date -u +%Y-%m-%dT%H:%M:%SZ)"
  rsync -a --delete --exclude 'build/' --exclude 'results/' --exclude '.git/' "$R/" "$S/" >/dev/null 2>&1
  echo "copied source tree"
  python3 - "$S" "$V" <<'PY'
import sys
root, variant = sys.argv[1], sys.argv[2]
if variant == 'momentum_old':
    p = root + '/src/physics/MomentumEquation.cpp'
    s = open(p, encoding='utf-8').read()
    n = s.count('mesh, face, dynamicViscosity, distance, gradPhi,')
    s = s.replace('mesh, face, dynamicViscosity, distance, gradPhi,\n          prescribesVelocity(mesh, face, velocityBoundaries));',
                  'mesh, face, dynamicViscosity, distance, internalGradPhi,\n          internalGradPhi != nullptr && prescribesVelocity(mesh, face, velocityBoundaries));')
    s = s.replace('mesh, face, muFace, distance, gradPhi,\n          prescribesVelocity(mesh, face, velocityBoundaries));',
                  'mesh, face, muFace, distance, internalGradPhi,\n          internalGradPhi != nullptr && prescribesVelocity(mesh, face, velocityBoundaries));')
    open(p, 'w', encoding='utf-8', newline='\n').write(s)
    print(f'  momentum boundary branch reverted to the pre-A2 gated pointer (const-mu sites seen: {n})')
elif variant == 'thermal_old':
    p = root + '/src/thermal/EnergyEquation.cpp'
    s = open(p, encoding='utf-8').read()
    s = s.replace('mesh, face, conductivity, distance, gradTPtr,\n          prescribesTemperature(mesh, face, temperatureBoundaries));',
                  'mesh, face, conductivity, distance, internalGradT,\n          internalGradT != nullptr && prescribesTemperature(mesh, face, temperatureBoundaries));')
    s = s.replace('mesh, face, kFace, distance, gradTPtr,\n          prescribesTemperature(mesh, face, temperatureBoundaries));',
                  'mesh, face, kFace, distance, internalGradT,\n          internalGradT != nullptr && prescribesTemperature(mesh, face, temperatureBoundaries));')
    open(p, 'w', encoding='utf-8', newline='\n').write(s)
    print('  thermal boundary branch reverted to the pre-A2 gated pointer')
else:
    raise SystemExit('unknown variant ' + variant)
PY
  echo "  momentum  $(sha256sum "$S/src/physics/MomentumEquation.cpp" | cut -c1-16)  (authoritative $(sha256sum "$R/src/physics/MomentumEquation.cpp" | cut -c1-16))"
  echo "  thermal   $(sha256sum "$S/src/thermal/EnergyEquation.cpp" | cut -c1-16)  (authoritative $(sha256sum "$R/src/thermal/EnergyEquation.cpp" | cut -c1-16))"
  cmake -S "$S" -B "$B" -DCMAKE_BUILD_TYPE=Release > "$B/configure.log" 2>&1 && echo "  configure OK" || { echo "  configure FAILED"; tail -5 "$B/configure.log"; exit 1; }
  cmake --build "$B" --target CFDNaturalConvectionValidationTests -j"$(nproc)" > "$B/build.log" 2>&1 \
    && echo "  build OK  library $(sha256sum "$B/src/libcfdcore.a" | cut -c1-16)" \
    || { echo "  build FAILED"; grep -E 'error' "$B/build.log" | head -10; exit 1; }
} > "$LOG" 2>&1
cat "$LOG"
