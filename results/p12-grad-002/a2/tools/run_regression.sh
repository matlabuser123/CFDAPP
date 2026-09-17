#!/usr/bin/env bash
# P12-GRAD-002 A2 step 5 (C13, C14): full regression of the final tree, sequential, every build a
# CLEAN-FIRST rebuild, failing closed on build failure or stale binaries (the procedure of
# results/p12-diff-002/w10/tools/run_w10.sh, with complete generated-output scopes):
#   0. snapshot of every generated output (results/validation, cases/*/results, tests/data/cases/*/results)
#   1. integrity (production tree, library) + clang-format-18 (CI's exact command)
#   2. build/release  Release, GUI off   (+ a copy of the generated outputs it wrote)
#   3. build/debug    Debug, GUI on, Qt offscreen
#   4. build/asan     Debug + ASan + UBSan (CI settings)
#   5. classification: Release-stage outputs vs snapshot, and final state vs snapshot
#   6. restore: every snapshot file whose Release-stage copy is value-identical to the snapshot is
#      restored from the snapshot (timing-only and sanitizer-build noise removed); anything else is
#      left in place and reported
# usage: run_regression.sh [stage...]   (default: all)
set -u
R=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
P=$R/results/p12-grad-002/a2
W=$HOME/g2/regr
mkdir -p "$W" "$P/logs"
cd "$R" || exit 1
STAGES=${*:-0 1 2 3 4 5 6}
fail() { echo "FAIL CLOSED: $*"; exit 1; }
scope() {
  git ls-files -co --exclude-standard -- 'results/validation' ':(glob)cases/*/results/**' \
    ':(glob)tests/data/cases/*/results/**'
}

build_stage() {  # name builddir log [env...]
  local name=$1 B=$2 LOG=$3; shift 3
  {
    echo "# A2 regression $name; $(date -u +%Y-%m-%dT%H:%M:%SZ); git HEAD $(git rev-parse HEAD)"
    echo "# cache: $(grep -E 'CMAKE_BUILD_TYPE:|CFDAPP_ENABLE_SANITIZERS:|CFDAPP_BUILD_GUI:' "$B/CMakeCache.txt" | tr '\n' ' ')"
    cmake -S "$R" -B "$B" > "$W/${name}_configure.txt" 2>&1 || fail "configure $name"
    local t0; t0=$(date +%s)
    cmake --build "$B" -j16 --clean-first > "$W/${name}_build.txt" 2>&1 || { tail -30 "$W/${name}_build.txt"; fail "build $name"; }
    echo "# build OK: $(wc -l < "$W/${name}_build.txt") lines, $(( $(date +%s) - t0 )) s, warnings $(grep -c 'warning:' "$W/${name}_build.txt")"
    grep 'warning:' "$W/${name}_build.txt" | sort -u | head -8
    cp "$W/${name}_build.txt" "$P/logs/regr_${name}_build.txt"
    local NT; NT=$(find src include tests apps -type f \( -name '*.cpp' -o -name '*.hpp' -o -name '*.qml' \) -printf '%T@\n' | sort -rn | head -1)
    local STALE=0 TOTAL=0
    while read -r t bin; do TOTAL=$((TOTAL+1)); awk -v a="$t" -v b="$NT" 'BEGIN{exit !(a<b)}' && { STALE=$((STALE+1)); echo "  STALE $bin"; }; done \
      < <(find "$B" -type f \( -name 'CFD*Tests' -o -name 'cfdapp*' -o -name '*_tests' \) -perm -u+x -printf '%T@ %p\n')
    echo "# executables checked $TOTAL, older than the newest source: $STALE"
    [ "$STALE" = 0 ] || fail "stale binaries in $name"
    echo "# libcfdcore.a $(sha256sum "$(find "$B" -name libcfdcore.a | head -1)" | cut -d' ' -f1)"
    echo "## ctest"
    local c0; c0=$(date +%s)
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
      scope > "$W/scope_before.txt"
      while read -r f; do mkdir -p "$W/snapshot/$(dirname "$f")"; cp -p "$f" "$W/snapshot/$f"; done < "$W/scope_before.txt"
      echo "== snapshot: $(find "$W/snapshot" -type f | wc -l) generated-output files" | tee "$P/logs/regr_00_snapshot.log"
      ;;
    1)
      {
        echo "# A2 regression integrity + format; $(date -u +%Y-%m-%dT%H:%M:%SZ)"
        echo "src/ + include/ tree: $(find src include -type f -print0 | sort -z | xargs -0 sha256sum | sha256sum | cut -d' ' -f1)  (A2 freeze: 42c3d9df...)"
        echo "build/release libcfdcore.a (before rebuild): $(sha256sum build/release/src/libcfdcore.a | cut -d' ' -f1)"
        echo "test file: $(sha256sum tests/unit/discretization/test_gradient_boundary_consistency.cpp | cut -d' ' -f1); CMakeLists: $(sha256sum tests/unit/discretization/CMakeLists.txt | cut -d' ' -f1)"
        echo "clang-format-18 files: $(find include src apps tests \( -name '*.cpp' -o -name '*.hpp' -o -name '*.h' \) | wc -l)"
        find include src apps tests \( -name '*.cpp' -o -name '*.hpp' -o -name '*.h' \) -print0 | xargs -0 clang-format-18 --dry-run --Werror > "$W/format.txt" 2>&1
        echo "clang-format-18 --dry-run --Werror exit $?  violations $(grep -c 'error:' "$W/format.txt")"
      } > "$P/logs/regr_01_integrity_format.log" 2>&1
      cat "$P/logs/regr_01_integrity_format.log"
      ;;
    2)
      CTEST_ARGS="-j16" build_stage release "$R/build/release" "$P/logs/regr_02_release.log" CFDAPP_A2=1
      rm -rf "$W/release_outputs"; mkdir -p "$W/release_outputs"
      scope | while read -r f; do mkdir -p "$W/release_outputs/$(dirname "$f")"; cp -p "$f" "$W/release_outputs/$f"; done
      ;;
    3) CTEST_ARGS="-j16" build_stage debug_gui "$R/build/debug" "$P/logs/regr_03_debug_gui.log" QT_QPA_PLATFORM=offscreen ;;
    4)
      CTEST_ARGS="--preset asan -j$(nproc) --timeout 7200" build_stage asan_ubsan "$R/build/asan" "$P/logs/regr_04_asan_ubsan.log" \
        ASAN_OPTIONS=detect_leaks=1:halt_on_error=0 UBSAN_OPTIONS=print_stacktrace=1:halt_on_error=0
      {
        echo "# sanitizer diagnostics: AddressSanitizer $(grep -c 'ERROR: AddressSanitizer' "$P/logs/regr_04_asan_ubsan.log"), UBSan runtime errors $(grep -c 'runtime error:' "$P/logs/regr_04_asan_ubsan.log"), LeakSanitizer $(grep -c 'ERROR: LeakSanitizer' "$P/logs/regr_04_asan_ubsan.log"), timeouts $(grep -c '\*\*\*Timeout' "$P/logs/regr_04_asan_ubsan.log")"
      } >> "$P/logs/regr_04_asan_ubsan.log"
      tail -1 "$P/logs/regr_04_asan_ubsan.log"
      ;;
    5)
      scope > "$W/scope_after.txt"
      cat "$W/scope_before.txt" "$W/scope_after.txt" | sort -u > "$W/scope_all.txt"
      {
        echo "## Release-stage outputs vs the pre-regression snapshot (production code is unchanged since W10)"
        python3 "$P/tools/classify_scope.py" "$W/snapshot" "$W/release_outputs" "$W/scope_all.txt"
        echo "exit $?"
        echo "## final working tree (last writer: the ASan build) vs the snapshot"
        python3 "$P/tools/classify_scope.py" "$W/snapshot" "$R" "$W/scope_all.txt"
        echo "exit $?"
      } > "$P/logs/regr_05_generated_outputs.log" 2>&1
      grep -E "^checked|^exit|^##" "$P/logs/regr_05_generated_outputs.log"
      ;;
    6)
      {
        echo "# restore $(date -u +%Y-%m-%dT%H:%M:%SZ)"
        n=0; kept=0
        while read -r f; do
          [ -f "$W/snapshot/$f" ] || continue
          if printf '%s\n' "$f" > "$W/one.txt" && python3 "$P/tools/classify_scope.py" "$W/snapshot" "$W/release_outputs" "$W/one.txt" > /dev/null; then
            cmp -s "$W/snapshot/$f" "$f" || { cp -p "$W/snapshot/$f" "$f"; n=$((n+1)); }
          else
            kept=$((kept+1)); echo "KEPT (Release values differ from the snapshot): $f"
          fi
        done < "$W/scope_before.txt"
        echo "restored $n files from the snapshot; kept $kept"
        echo "snapshot files differing from the working tree afterwards: $(cd "$W/snapshot" && find . -type f | while read -r f; do cmp -s "$f" "$R/$f" || echo "$f"; done | wc -l)"
      } > "$P/logs/regr_06_restore.log" 2>&1
      cat "$P/logs/regr_06_restore.log"
      ;;
  esac
done
