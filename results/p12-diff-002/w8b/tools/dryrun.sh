#!/usr/bin/env bash
# P12-DIFF-002 W8B: PRE-FREEZE DRY-RUN of every candidate criterion, entirely OUTSIDE the repo.
#
# Each variant is a copy of the repo's sources in $HOME/w8b_dry/<variant> with:
#   tests : the candidate test files (w8b/data/candidate/), or the ORIGINAL ones ("orig")
#   cases : the two W8 cases with "face_flux": "rhie_chow" (the DRIFT-001 candidate), or unchanged
#   lib   : the current production sources, or one control's NonOrthogonalDiffusion.cpp
# built (Release, dependencies from the repo's already-fetched sources, no network) and run with
# the variant root as working directory -- exactly ctest's WORKING_DIRECTORY convention.
#
# usage: dryrun.sh <variant>...    variants: cand linear nodiff farx2 signflip
set -u
R=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
P=$R/results/p12-diff-002/w8b
D=$HOME/w8b_dry
FILTER='StructuredQuadProductionCase.*:MultiBlockProductionCase.*'
mkdir -p "$D" "$P/logs"

one() {
  local v=$1 tests cases lib
  case "$v" in
    cand)     tests=candidate; cases=rc;     lib=current ;;
    linear)   tests=candidate; cases=linear; lib=current ;;
    nodiff)   tests=candidate; cases=rc;     lib=/root/uf001_baseline ;;
    farx2)    tests=candidate; cases=rc;     lib=/root/uf001_ctrl_farx2 ;;
    signflip) tests=candidate; cases=rc;     lib=/root/uf001_ctrl_signflip ;;
    *) echo "unknown variant $v"; return 2 ;;
  esac
  local T=$D/$v LOG=$P/logs/${LOGPREFIX:-dry}_$v.log
  rm -rf "$T"; mkdir -p "$T"
  {
    echo "# W8B dry-run variant=$v tests=$tests cases=$cases lib=$lib"
    echo "# start $(date -u +%Y-%m-%dT%H:%M:%SZ)  repo HEAD $(git -C "$R" rev-parse HEAD)"
    rsync -a --exclude build --exclude results --exclude .git --exclude docs "$R/" "$T/src_tree/"
    local S=$T/src_tree
    mkdir -p "$S/results/validation/production"
    if [ "$tests" = candidate ]; then
      cp "$P/data/candidate/test_structured_quad_production_case.cpp" "$P/data/candidate/test_multiblock_production_case.cpp" "$S/tests/integration/case/"
    fi
    if [ "$cases" = rc ]; then
      python3 - "$S" <<'EOF'
import json, sys, pathlib
for case in ("poiseuille_distorted", "curved_channel_multiblock"):
    p = pathlib.Path(sys.argv[1]) / "cases" / case / "solver.json"
    d = json.loads(p.read_text()); d["face_flux"] = "rhie_chow"
    p.write_text(json.dumps(d, indent=2) + "\n")
EOF
    fi
    if [ "$cases" = linear ]; then  # post-freeze: the repo now carries the edit; remove it again
      for c in poiseuille_distorted curved_channel_multiblock; do
        sed -i '/^  "face_flux": "rhie_chow",$/d' "$S/cases/$c/solver.json"
      done
    fi
    if [ "$lib" != current ]; then cp "$lib/src/discretization/NonOrthogonalDiffusion.cpp" "$S/src/discretization/"; fi
    echo "# test sources: $(sha256sum "$S/tests/integration/case/test_structured_quad_production_case.cpp" "$S/tests/integration/case/test_multiblock_production_case.cpp" | cut -c1-16 | tr '\n' ' ')"
    echo "# NonOrthogonalDiffusion.cpp: $(sha256sum "$S/src/discretization/NonOrthogonalDiffusion.cpp" | cut -c1-16)"
    echo "# poiseuille_distorted/solver.json face_flux: $(grep -o '"face_flux"[^,}]*' "$S/cases/poiseuille_distorted/solver.json" || echo '(none)')"
    echo "# curved_channel_multiblock/solver.json face_flux: $(grep -o '"face_flux"[^,}]*' "$S/cases/curved_channel_multiblock/solver.json" || echo '(none)')"
    cmake -S "$S" -B "$T/build" -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON \
      -DCFDAPP_BUILD_GUI=OFF -DCFDAPP_BUILD_BENCHMARKS=OFF \
      -DFETCHCONTENT_SOURCE_DIR_GOOGLETEST="$R/build/release/_deps/googletest-src" \
      -DFETCHCONTENT_SOURCE_DIR_NLOHMANN_JSON="$R/build/release/_deps/nlohmann_json-src" \
      -DFETCHCONTENT_FULLY_DISCONNECTED=ON > "$T/configure.txt" 2>&1 || { echo "CONFIGURE FAILED"; tail -20 "$T/configure.txt"; return 1; }
    cmake --build "$T/build" --target CFDCaseIntegrationTests -j8 > "$T/build.txt" 2>&1 || { echo "BUILD FAILED"; grep -m20 -E 'error|Error' "$T/build.txt"; return 1; }
    local BIN; BIN=$(find "$T/build" -type f -name CFDCaseIntegrationTests | head -1)
    echo "# libcfdcore.a $(sha256sum "$T/build/src/libcfdcore.a" | cut -d' ' -f1)"
    echo "# binary       $(sha256sum "$BIN" | cut -d' ' -f1)"
    echo "# warnings in the two test sources: $(grep -cE 'test_(structured_quad|multiblock)_production_case\.cpp:[0-9]+:[0-9]+: warning' "$T/build.txt")"
    cd "$S" || return 1
    "$BIN" --gtest_filter="$FILTER"
    echo "exit $?"
    echo "# end $(date -u +%Y-%m-%dT%H:%M:%SZ)"
  } > "$LOG" 2>&1
}

pids=()
for v in "$@"; do one "$v" & pids+=($!); done
for p in "${pids[@]}"; do wait "$p"; done
for v in "$@"; do
  echo "== $v"
  grep -E '^\[  (FAILED|PASSED) |exit |FAILED$|CONFIGURE|BUILD FAILED' "$P/logs/${LOGPREFIX:-dry}_$v.log" | head -20
done
