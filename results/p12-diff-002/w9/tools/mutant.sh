#!/usr/bin/env bash
# W9A-6 non-vacuity: a scratch library whose correctFaceMassFlux OMITS the Dirichlet-boundary flux
# correction (a genuine conservation defect at the p = 0 outlet), run on three open-channel cases
# through the same W9 program. W9A must reject it (W9A-1 not converged, or W9A-2/3 imbalance above
# the resolution floor). Scratch tree only; the repository is not touched.
set -u
R=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
P=$R/results/p12-diff-002/w9
M=$HOME/w9_mutant
rm -rf "$M"; mkdir -p "$M"
LOG=$P/logs/06_w9a_mutant.log
{
  echo "# W9A-6 mutant control; $(date -u +%Y-%m-%dT%H:%M:%SZ)"
  rsync -a --exclude build --exclude results --exclude .git --exclude docs "$R/" "$M/src_tree/"
  F=$M/src_tree/src/pressure_velocity/PressureCorrectionEquation.cpp
  python3 - "$F" <<'EOF'
import sys
p = sys.argv[1]; s = open(p).read()
old = "    const Real fluxCorrection = faceCoefficient[faceId] * (pOwner - pNeighbor);"
new = "    const Real fluxCorrection = face.isBoundary() ? 0.0 : faceCoefficient[faceId] * (pOwner - pNeighbor);  // W9A-6 MUTANT"
assert s.count(old) == 1
open(p, "w").write(s.replace(old, new))
EOF
  echo "# mutation: $(grep -n 'W9A-6 MUTANT' "$F")"
  cmake -S "$M/src_tree" -B "$M/build" -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF \
    -DCFDAPP_BUILD_GUI=OFF -DCFDAPP_BUILD_BENCHMARKS=OFF \
    -DFETCHCONTENT_SOURCE_DIR_GOOGLETEST="$R/build/release/_deps/googletest-src" \
    -DFETCHCONTENT_SOURCE_DIR_NLOHMANN_JSON="$R/build/release/_deps/nlohmann_json-src" \
    -DFETCHCONTENT_FULLY_DISCONNECTED=ON > "$M/configure.txt" 2>&1 || { echo "CONFIGURE FAILED"; exit 1; }
  cmake --build "$M/build" --target cfdcore -j16 > "$M/build.txt" 2>&1 || { echo "BUILD FAILED"; tail -20 "$M/build.txt"; exit 1; }
  LIB=$(find "$M/build" -name libcfdcore.a | head -1)
  echo "# mutant libcfdcore.a $(sha256sum "$LIB" | cut -d' ' -f1)"
  c++ -std=c++20 -O3 -DNDEBUG -I"$M/src_tree/include" -I"$M/build/generated/include" \
    -I"$R/build/release/_deps/nlohmann_json-src/include" "$P/tools/w9_run.cpp" "$LIB" -o "$M/w9_mutant" || exit 1
  mkdir -p "$M/run_B"
  for c in poiseuille_flow channel_transpiration_graded poiseuille_distorted; do
    rm -rf "$M/run_B/$c"; cp -r "$R/cases/$c" "$M/run_B/$c"; rm -rf "$M/run_B/$c/results"
    "$M/w9_mutant" "$M/run_B/$c" > "$M/run_B/$c.out" 2>&1; echo "exit $?" >> "$M/run_B/$c.out"
    echo "## $c"; sed 's/^/  /' "$M/run_B/$c.out"
    python3 - "$M/run_B/$c" <<'EOF'
import json, math, os, sys
d = sys.argv[1]
try:
    meta = json.load(open(os.path.join(d, "results", "metadata.json")))
except OSError:
    print("  (no metadata.json: the run did not export)"); sys.exit(0)
ps = json.load(open(os.path.join(d, "solver.json"))).get("pressure_linear_solver", {})
floor = math.sqrt(meta["mesh"]["cells"]) * max(ps.get("absolute_tolerance", 1e-12),
                                               ps.get("relative_tolerance", 1e-10) * meta["residuals"]["p"])
imb = meta["conservation"]["global_mass_imbalance"]
print(f"  W9A: solver.converged {meta['solver'].get('converged')} status {meta['solver'].get('status')}"
      f" | imbalance {imb:.3e} floor {floor:.3e} -> {'REJECTED' if (imb > floor or not meta['solver'].get('converged')) else 'accepted'}")
EOF
  done
  echo "# end $(date -u +%Y-%m-%dT%H:%M:%SZ)"
} > "$LOG" 2>&1
cat "$LOG"
