#!/usr/bin/env bash
# P12-MESH-007 Amendment A3 (G9) -- the G9 baseline "nom7": the CURRENT working tree with every
# MESH-007 change removed and every later, separately gated phase kept (P12-GRAD-002 and its
# DRIFT-001 / VAL-001 / A2 work, P12-DIFF-002 and its sub-phases, P12-ASAN-001, FORMAT-001).
#
# MESH-007's changes (summary.md section 3; confirmed by diffing BASE = $HOME/m7ref/base against the
# current tree):
#   new files          MeshMotion.{hpp,cpp}, AlePISO.{hpp,cpp}, PisoStep.hpp; tests
#                      MeshMotionCases.hpp, test_mesh_motion.cpp, test_ale_operators.cpp,
#                      test_ale_piso.cpp                                   -> removed
#   changed files      Cell.hpp, Face.hpp, Mesh.{hpp,cpp}, TimeDerivative.{hpp,cpp},
#   (MESH-007 only)    MassFlux.{hpp,cpp}, TransientMomentum.{hpp,cpp}, PISO.cpp,
#                      TransientSolver.{hpp,cpp}, src/CMakeLists.txt, tests/solver/piso and
#                      tests/unit/mesh CMakeLists.txt                     -> BASE version
#   shared files       MeshGeometry.{hpp,cpp}: MESH-007's inserted blocks (marked "P12-MESH-007")
#                      and the includes only they use are removed; the GRAD-002 and DIFF-002
#                      insertions stay; tests/unit/discretization/CMakeLists.txt: the MESH-007
#                      lines are removed
#   later tests that need the motion API: test_gradient_boundary_consistency.cpp (GRAD-002 A2)
#                      -> removed from nom7 (it generates no output file)
# The script verifies the result: nom7 differs from BASE only in later-phase files, and from the
# current tree only in MESH-007 files. Release build, GUI off, all tests.
set -u
R=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
B=/root/m7ref/base
P=$R/results/p12-mesh-007
T=$HOME/m7g9/nom7
LOG=$P/logs/${1:-30_a3_build_nom7.log}
mkdir -p "$T" "$P/logs"
{
  echo "# nom7 build $(date -u +%Y-%m-%dT%H:%M:%SZ); git HEAD $(cd $R && git rev-parse HEAD)"
  rsync -a --delete --exclude build --exclude results --exclude .git --exclude '__pycache__' \
    --exclude '.venv' "$R/" "$T/src_tree/" || { echo "rsync failed"; exit 1; }
  S=$T/src_tree
  for f in include/cfd/mesh/MeshMotion.hpp src/mesh/MeshMotion.cpp include/cfd/pressure_velocity/AlePISO.hpp \
           src/pressure_velocity/AlePISO.cpp src/pressure_velocity/PisoStep.hpp tests/support/MeshMotionCases.hpp \
           tests/unit/mesh/test_mesh_motion.cpp tests/unit/discretization/test_ale_operators.cpp \
           tests/solver/piso/test_ale_piso.cpp tests/unit/discretization/test_gradient_boundary_consistency.cpp; do
    rm -f "$S/$f" && echo "removed  $f"
  done
  for f in include/cfd/mesh/Cell.hpp include/cfd/mesh/Face.hpp include/cfd/mesh/Mesh.hpp src/mesh/Mesh.cpp \
           include/cfd/discretization/TimeDerivative.hpp src/discretization/TimeDerivative.cpp \
           include/cfd/physics/MassFlux.hpp src/physics/MassFlux.cpp \
           include/cfd/pressure_velocity/TransientMomentum.hpp src/pressure_velocity/TransientMomentum.cpp \
           src/pressure_velocity/PISO.cpp include/cfd/solver/TransientSolver.hpp src/solver/TransientSolver.cpp \
           src/CMakeLists.txt tests/solver/piso/CMakeLists.txt tests/unit/mesh/CMakeLists.txt; do
    cp "$B/$f" "$S/$f" && echo "BASE     $f"
  done
  python3 - "$B" "$S" <<'PY' || { echo "shared-file surgery failed"; exit 1; }
import difflib, sys
base, tree = sys.argv[1], sys.argv[2]
def surgery(rel, drop_includes):
    b = open(f"{base}/{rel}").read().splitlines(keepends=True)
    c = open(f"{tree}/{rel}").read().splitlines(keepends=True)
    out, kept = [], []
    sm = difflib.SequenceMatcher(a=b, b=c, autojunk=False)
    for op, i1, i2, j1, j2 in sm.get_opcodes():
        block = c[j1:j2]
        if op == "equal":
            out += block
        elif op == "insert":
            text = "".join(block)
            if "P12-MESH-007" in text:
                print(f"  {rel}: dropped MESH-007 block of {len(block)} lines (current {j1+1}-{j2})")
            elif all(l.strip() in drop_includes for l in block):
                print(f"  {rel}: dropped MESH-007 include(s) {[l.strip() for l in block]}")
            else:
                out += block
                kept.append((j1 + 1, j2, block[0].strip()[:60]))
        else:
            raise SystemExit(f"{rel}: unexpected '{op}' hunk at base {i1+1}-{i2} / current {j1+1}-{j2}")
    open(f"{tree}/{rel}", "w").write("".join(out))
    for k in kept:
        print(f"  {rel}: kept insertion current {k[0]}-{k[1]} starting '{k[2]}'")
surgery("include/cfd/mesh/MeshGeometry.hpp", {"#include <array>", "#include <string>"})
surgery("src/mesh/MeshGeometry.cpp", {"#include <map>", "#include <tuple>"})
# tests/unit/discretization/CMakeLists.txt: current minus the MESH-007 lines and the motion-dependent test
rel = "tests/unit/discretization/CMakeLists.txt"
lines = open(f"{tree}/{rel}").read().splitlines(keepends=True)
drop = ("test_ale_operators.cpp", "test_gradient_boundary_consistency.cpp", "P12-MESH-007", "MeshMotionCases",
        "../../support")
kept = [l for l in lines if not any(d in l for d in drop)]
text = "".join(kept).replace("target_include_directories(CFDDiscretizationTests\n  PRIVATE\n)\n", "")
open(f"{tree}/{rel}", "w").write(text)
print(f"  {rel}: dropped {len(lines) - len(kept)} MESH-007 / motion-test line(s)")
PY
  echo "## verification (generated output directories named 'results' excluded, as in the rsync above)"
  lst() { find src include tests apps -name results -prune -o -name __pycache__ -prune -o -type f -print | sort; }
  (cd $S && for f in $(lst); do
     if [ ! -f "$R/$f" ]; then echo "  nom7-only?! $f"; elif ! cmp -s "$f" "$R/$f"; then echo "  differs from current: $f"; fi; done)
  (cd $R && for f in $(lst); do [ -f "$S/$f" ] || echo "  absent from nom7: $f"; done)
  (cd $S && for f in $(lst); do
     if [ ! -f "$B/$f" ]; then echo "  not in BASE: $f"; elif ! cmp -s "$f" "$B/$f"; then echo "  differs from BASE: $f"; fi; done)
  (cd $B && for f in $(lst); do [ -f "$S/$f" ] || echo "  in BASE, absent from nom7: $f"; done)
  echo "## MeshGeometry against BASE (only GRAD-002 / DIFF-002 insertions may remain)"
  diff $B/include/cfd/mesh/MeshGeometry.hpp $S/include/cfd/mesh/MeshGeometry.hpp | grep -E '^[0-9]'
  diff $B/src/mesh/MeshGeometry.cpp $S/src/mesh/MeshGeometry.cpp | grep -E '^[0-9]'
  echo "## fidelity verdicts F1-F3 (tools/nom7_fidelity.py)"
  python3 $P/tools/nom7_fidelity.py $B $S $R || { echo "FIDELITY FAILED: nom7 is not the current tree minus MESH-007"; exit 1; }
  D=$R/build/release/_deps
  cmake -S "$S" -B "$T/build" -DCMAKE_BUILD_TYPE=Release -DCFDAPP_BUILD_GUI=OFF \
    -DFETCHCONTENT_FULLY_DISCONNECTED=ON -DFETCHCONTENT_SOURCE_DIR_GOOGLETEST="$D/googletest-src" \
    -DFETCHCONTENT_SOURCE_DIR_NLOHMANN_JSON="$D/nlohmann_json-src" > "$T/configure.txt" 2>&1 \
    || { tail -20 "$T/configure.txt"; echo "configure failed"; exit 1; }
  nice -n 10 cmake --build "$T/build" -j${2:-16} > "$T/build.txt" 2>&1 || { grep -m20 -E "error" "$T/build.txt"; echo "build failed"; exit 1; }
  echo "build OK, warnings $(grep -c 'warning:' "$T/build.txt")"
  echo "nom7 libcfdcore.a $(sha256sum "$T/build/src/libcfdcore.a" | cut -d' ' -f1); cfdapp $(sha256sum "$T/build/apps/cli/cfdapp" | cut -d' ' -f1)"
  echo "# end $(date -u +%Y-%m-%dT%H:%M:%SZ)"
} > "$LOG" 2>&1
cat "$LOG"
