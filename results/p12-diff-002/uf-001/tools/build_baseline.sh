#!/usr/bin/env bash
# UF-001: build the pre-DIFF-002 baseline library in an ISOLATED copy outside the repo.
# The authoritative tree is never written to. The single removed block is the DIFF-002 far-cell
# reconstruction in boundaryFaceDiffusionTerms; the function then falls through to the
# pre-existing two-point treatment, which on an orthogonal mesh is exactly Gamma|S|/d.
set -e
R=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
P=$R/results/p12-diff-002/uf-001
B=$HOME/uf001_baseline
mkdir -p "$P/logs"

rm -rf "$B"
mkdir -p "$B"
rsync -a --exclude /build --exclude /results --exclude /.git \
      "$R"/ "$B"/ > /dev/null

python3 - "$B/src/discretization/NonOrthogonalDiffusion.cpp" <<'PY'
import sys, re
path = sys.argv[1]
src = open(path, encoding='utf-8').read()
start = src.index('    const auto stencil = MeshGeometry::boundaryInwardStencil(mesh, face);')
end_marker = '      return terms;\n    }\n'
end = src.index(end_marker, start) + len(end_marker)
removed = src[start:end]
assert 'terms.farCellCoefficient = gammaArea * cF;' in removed, "wrong block"
assert 'terms.higherOrder = true;' in removed, "wrong block"
src = src[:start] + (
    '    // UF-001 BASELINE ONLY: the P12-DIFF-002 far-cell reconstruction block is removed here,\n'
    '    // so this falls through to the pre-existing two-point treatment.\n'
) + src[end:]
assert 'farCellCoefficient = gammaArea * cF' not in src, "removal incomplete"
open(path, 'w', encoding='utf-8', newline='').write(src)
print("removed %d characters" % len(removed))
PY

cd "$B"
cmake -S . -B build/release -DCMAKE_BUILD_TYPE=Release -DCFDAPP_BUILD_GUI=OFF \
      > "$P/logs/03_baseline_configure.log" 2>&1
cmake --build build/release --target cfdcore -j"$(nproc)" \
      > "$P/logs/03_baseline_build.log" 2>&1
LIB=$(find "$B/build" -name libcfdcore.a | head -1)
{
  echo "# UF-001 pre-DIFF-002 baseline library"
  echo "path   $LIB"
  echo "sha256 $(sha256sum "$LIB" | cut -d' ' -f1)"
  echo "A5 recorded the reconstruction-removed library as 90473bfb... (different compiler/flags"
  echo "or a different isolated tree will legitimately give a different object hash; what is"
  echo "verified here is the SOURCE difference, shown below)."
  echo
  echo "## the only source difference against the authoritative tree"
  diff -u "$R/src/discretization/NonOrthogonalDiffusion.cpp" \
          "$B/src/discretization/NonOrthogonalDiffusion.cpp" || true
  echo
  echo "## every other production numerical file byte-identical?"
  for f in src/physics/MomentumEquation.cpp src/thermal/EnergyEquation.cpp \
           src/thermal/ThermalSolver.cpp src/physics/BoussinesqBuoyancy.cpp \
           src/pressure_velocity/SIMPLE.cpp src/solver/SolverRobustness.cpp \
           src/discretization/Diffusion.cpp; do
    a=$(sha256sum "$R/$f" | cut -d' ' -f1)
    b=$(sha256sum "$B/$f" | cut -d' ' -f1)
    if [ "$a" = "$b" ]; then echo "  SAME  $f"; else echo "  DIFF  $f  <<< UNEXPECTED"; fi
  done
} > "$P/logs/03_baseline.log" 2>&1
cat "$P/logs/03_baseline.log"
echo "$LIB" > "$P/data/baseline_lib_path.txt"
