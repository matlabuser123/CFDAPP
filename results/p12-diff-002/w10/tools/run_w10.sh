#!/usr/bin/env bash
# P12-DIFF-002 W10 -- full regression (acceptance_gate.md W10), sequential, never two ctest runs at
# once; every build is a CLEAN-FIRST rebuild and every stage fails closed on build failure or stale
# binaries:
#   0. snapshot of every generated output ctest rewrites (for the runtime-noise classification)
#   1. integrity (production tree, library) + clang-format-18 (CI's exact command)
#   2. build/release  Release, GUI off
#   3. build/debug    Debug, GUI on, Qt offscreen
#   4. build/asan     Debug + ASan + UBSan (CI "sanitizers": ctest --preset asan -j nproc
#                     --timeout 7200, ASAN_OPTIONS/UBSAN_OPTIONS as CI)
#   5. generated-output classification (MESH-006's classifier, evidence prefix adapted)
# usage: run_w10.sh [stage...]   (default: all)
set -u
R=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
P=$R/results/p12-diff-002/w10
W=$HOME/w10
mkdir -p "$W" "$P/logs"
cd "$R" || exit 1
STAGES=${*:-0 1 2 3 4 5}
fail() { echo "FAIL CLOSED: $*"; exit 1; }

build_stage() {  # name builddir log [env-prefix for ctest...]
  local name=$1 B=$2 LOG=$3; shift 3
  {
    echo "# W10 $name; $(date -u +%Y-%m-%dT%H:%M:%SZ); git HEAD $(git rev-parse HEAD)"
    echo "# cache: $(grep -E 'CMAKE_BUILD_TYPE:|CFDAPP_ENABLE_SANITIZERS:|CFDAPP_BUILD_GUI:' "$B/CMakeCache.txt" | tr '\n' ' ')"
    cmake -S "$R" -B "$B" > "$W/${name}_configure.txt" 2>&1 || fail "configure $name"
    local t0; t0=$(date +%s)
    cmake --build "$B" -j16 --clean-first > "$W/${name}_build.txt" 2>&1 || { tail -30 "$W/${name}_build.txt"; fail "build $name"; }
    echo "# build OK: $(wc -l < "$W/${name}_build.txt") lines, $(( $(date +%s) - t0 )) s, warnings $(grep -c 'warning:' "$W/${name}_build.txt")"
    cp "$W/${name}_build.txt" "$P/logs/${name}_build.txt"
    local NT; NT=$(find src include tests apps -type f \( -name '*.cpp' -o -name '*.hpp' -o -name '*.qml' \) -printf '%T@\n' | sort -rn | head -1)
    local STALE=0 TOTAL=0
    while read -r t bin; do TOTAL=$((TOTAL+1)); awk -v a="$t" -v b="$NT" 'BEGIN{exit !(a<b)}' && { STALE=$((STALE+1)); echo "  STALE $bin"; }; done \
      < <(find "$B" -type f \( -name 'CFD*Tests' -o -name 'cfdapp*' -o -name '*_tests' \) -perm -u+x -printf '%T@ %p\n')
    echo "# executables checked $TOTAL, older than the newest source: $STALE"
    [ "$STALE" = 0 ] || fail "stale binaries in $name"
    echo "# libcfdcore.a $(sha256sum "$(find "$B" -name libcfdcore.a | head -1)" | cut -d' ' -f1)"
    echo "## ctest"
    local c0; c0=$(date +%s)
    # A --preset run must start in the source root (CMakePresets.json), exactly as CI does.
    if [[ "$CTEST_ARGS" == *--preset* ]]; then
      ( cd "$R" && env "$@" ctest --output-on-failure $CTEST_ARGS )
    else
      ( cd "$B" && env "$@" ctest --output-on-failure $CTEST_ARGS )
    fi
    echo "ctest exit $?  ($(( $(date +%s) - c0 )) s)"
    echo "# end $(date -u +%Y-%m-%dT%H:%M:%SZ)"
  } > "$LOG" 2>&1
  echo "== $name: $(grep -E 'tests passed|tests failed out of' "$LOG" | tail -1) | $(grep -E '^ctest exit' "$LOG")"
  sed -n '/The following tests FAILED:/,/^Errors while running/p' "$LOG"
}

for s in $STAGES; do
  case $s in
    0)
      rm -rf "$W/snapshot"; mkdir -p "$W/snapshot"
      git ls-files -co --exclude-standard -- 'results/validation' 'cases/*/results' 'tests/data/cases/*/results' \
        | while read -r f; do mkdir -p "$W/snapshot/$(dirname "$f")"; cp -p "$f" "$W/snapshot/$f"; done
      echo "== snapshot: $(find "$W/snapshot" -type f | wc -l) generated-output files"
      ;;
    1)
      {
        echo "# W10 integrity + format; $(date -u +%Y-%m-%dT%H:%M:%SZ)"
        echo "src/ + include/ tree: $(find src include -type f -print0 | sort -z | xargs -0 sha256sum | sha256sum | cut -d' ' -f1)  (FORMAT-001 post-format value 42c3d9df...)"
        echo "build/release libcfdcore.a (before rebuild): $(sha256sum build/release/src/libcfdcore.a | cut -d' ' -f1)"
        echo "clang-format-18 files: $(find include src apps tests \( -name '*.cpp' -o -name '*.hpp' -o -name '*.h' \) | wc -l)"
        find include src apps tests \( -name '*.cpp' -o -name '*.hpp' -o -name '*.h' \) -print0 | xargs -0 clang-format-18 --dry-run --Werror > "$W/format.txt" 2>&1
        echo "clang-format-18 --dry-run --Werror exit $?  violations $(grep -c 'error:' "$W/format.txt")"
      } > "$P/logs/01_integrity_format.log" 2>&1
      cat "$P/logs/01_integrity_format.log"
      ;;
    2) CTEST_ARGS="-j16" build_stage release "$R/build/release" "$P/logs/02_release.log" CFDAPP_W10=1 ;;
    3) CTEST_ARGS="-j16" build_stage debug_gui "$R/build/debug" "$P/logs/03_debug_gui.log" QT_QPA_PLATFORM=offscreen ;;
    4)
      CTEST_ARGS="--preset asan -j$(nproc) --timeout 7200" build_stage asan_ubsan "$R/build/asan" "$P/logs/04_asan_ubsan.log" \
        ASAN_OPTIONS=detect_leaks=1:halt_on_error=0 UBSAN_OPTIONS=print_stacktrace=1:halt_on_error=0
      {
        echo "# sanitizer diagnostics: AddressSanitizer $(grep -c 'ERROR: AddressSanitizer' "$P/logs/04_asan_ubsan.log"), UBSan runtime errors $(grep -c 'runtime error:' "$P/logs/04_asan_ubsan.log"), LeakSanitizer $(grep -c 'ERROR: LeakSanitizer' "$P/logs/04_asan_ubsan.log")"
      } >> "$P/logs/04_asan_ubsan.log"
      tail -1 "$P/logs/04_asan_ubsan.log"
      ;;
    5)
      sed 's|^EXCLUDED_PREFIX = .*|EXCLUDED_PREFIX = "results/p12-diff-002/"|' \
        "$R/results/p12-mesh-006/a3/tools/classify_generated_outputs.py" > "$W/classify.py"
      python3 "$W/classify.py" "$W/snapshot" > "$P/logs/05_generated_outputs.log" 2>&1
      echo "classify exit $?" >> "$P/logs/05_generated_outputs.log"
      grep -cE '^(IDENTICAL|RUNTIME-ONLY|VALUES|NEW)' "$P/logs/05_generated_outputs.log"
      grep -E '^(VALUES|NEW)' "$P/logs/05_generated_outputs.log" | head -30
      ;;
  esac
done
