#!/usr/bin/env bash
# LOWMACH-001 pre-freeze dry-run, outside the repository.
#   cand     candidate test, current library
#   base     candidate test, pre-DIFF-002 library (UF-001 baseline source)
#   mutant   candidate test, current library with every BOUNDARY compressible mass flux scaled by
#            (1 + 1e-3) -- a spurious boundary flux / boundary-density error of 0.1 %
#   orig     ORIGINAL test, current library (reproduces the recorded failure)
# usage: [LOGPREFIX=...] dryrun.sh <variant>...
set -u
R=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
P=$R/results/p12-diff-002/lowmach-001
D=$HOME/lowmach_dry
mkdir -p "$D" "$P/logs"
one() {
  local v=$1 T=$D/$1 LOG=$P/logs/${LOGPREFIX:-dry}_$1.log
  rm -rf "$T"; mkdir -p "$T"
  {
    echo "# LOWMACH-001 dry-run variant=$v; $(date -u +%Y-%m-%dT%H:%M:%SZ)"
    rsync -a --exclude build --exclude results --exclude .git --exclude docs "$R/" "$T/src_tree/"
    local S=$T/src_tree
    [ "$v" != orig ] && cp "$P/data/candidate/test_low_mach_regression.cpp" "$S/tests/integration/compressible/"
    [ "$v" = orig ] && git -C "$R" show HEAD:tests/integration/compressible/test_low_mach_regression.cpp > "$S/tests/integration/compressible/test_low_mach_regression.cpp"
    [ "$v" = base ] && cp /root/uf001_baseline/src/discretization/NonOrthogonalDiffusion.cpp "$S/src/discretization/"
    if [ "$v" = mutant ]; then
      # (the first draft scaled EVERY boundary face, which scales inflow and outflow alike and
      #  leaves the balance intact -- a vacuous control, caught by this dry-run; logs/dry_mutant.log)
      python3 - "$S/src/compressible/CompressibleMassFlux.cpp" <<'EOF'
import sys
p = sys.argv[1]; s = open(p).read()
old = "    massFlux[faceId] = faceDensity[faceId] * dot(faceVelocity, face.areaVector());"
new = old + "\n    if (face.isBoundary() && massFlux[faceId] > 0.0) massFlux[faceId] *= (1.0 + 1e-3);  // LOWMACH-001 MUTANT: outflow density +0.1 %"
assert s.count(old) == 1
open(p, "w").write(s.replace(old, new))
EOF
    fi
    if [ "$v" = bugfield ]; then
      # the candidate test with the ORIGINAL defect reinstated: compressible quantities from the
      # uniform initial guess instead of the solution
      python3 - "$S/tests/integration/compressible/test_low_mach_regression.cpp" <<'EOF'
import sys
p = sys.argv[1]; s = open(p).read()
old = "  const VectorField& velocity = flow.velocity;"
new = "  const VectorField velocity(mesh.numberOfCells(), Vector2{kMeanVelocity, 0.0});  // BUGFIELD CONTROL"
assert s.count(old) == 1
open(p, "w").write(s.replace(old, new))
EOF
    fi
    echo "# test source $(sha256sum "$S/tests/integration/compressible/test_low_mach_regression.cpp" | cut -c1-16)"
    cmake -S "$S" -B "$T/build" -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON \
      -DCFDAPP_BUILD_GUI=OFF -DCFDAPP_BUILD_BENCHMARKS=OFF \
      -DFETCHCONTENT_SOURCE_DIR_GOOGLETEST="$R/build/release/_deps/googletest-src" \
      -DFETCHCONTENT_SOURCE_DIR_NLOHMANN_JSON="$R/build/release/_deps/nlohmann_json-src" \
      -DFETCHCONTENT_FULLY_DISCONNECTED=ON > "$T/configure.txt" 2>&1 || { echo "CONFIGURE FAILED"; return 1; }
    cmake --build "$T/build" --target CFDLowMachRegressionTests -j8 > "$T/build.txt" 2>&1 || { echo "BUILD FAILED"; grep -m20 -E 'error' "$T/build.txt"; return 1; }
    echo "# warnings in the test source: $(grep -c 'test_low_mach_regression.cpp:[0-9]*:[0-9]*: warning' "$T/build.txt")"
    echo "# libcfdcore.a $(sha256sum "$T/build/src/libcfdcore.a" | cut -d' ' -f1)"
    local BIN; BIN=$(find "$T/build" -type f -name CFDLowMachRegressionTests | head -1)
    cd "$S" && "$BIN"
    echo "exit $?"
  } > "$LOG" 2>&1
}
pids=()
for v in "$@"; do one "$v" & pids+=($!); done
for p in "${pids[@]}"; do wait "$p"; done
for v in "$@"; do echo "== $v"; grep -E '^\[  (PASSED|FAILED)|^\[       OK|Failure|actual|global imbalance|BUILD|CONFIG' "$P/logs/${LOGPREFIX:-dry}_$v.log"; done
