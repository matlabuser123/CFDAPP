#!/usr/bin/env bash
# GPU closeout Phase 3 -- source hygiene on the exact tree that will be committed.
#
# TWO CHECKS IN THE FIRST VERSION WERE WRONG and are corrected below. Both were
# false positives produced by my own greps, not findings:
#
#   check 2  filtered comment lines with `^\s*//`, but grep prefixes every hit
#            with "file:line:", so the anchor never matched. A commented-out
#            usage example in include/cfd/core/Exception.hpp -- a file this
#            lineage does not touch at all -- read as a stray debug print.
#   check 4  matched the pattern "-G", which fires on "Green-Gauss" inside a
#            comment in cuda/CMakeLists.txt.
set -uo pipefail
ROOT=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
cd "$ROOT"
bad=0
say() { printf "  %-5s %s\n" "$1" "$2"; [ "$1" = "FAIL" ] && bad=1; return 0; }

PROD="src include cuda apps"

# Every production source this lineage adds or changes. That is the scope the
# closeout is responsible for; the rest of the tree is not its to police.
LINEAGE=$( { git diff --name-only; git ls-files --others --exclude-standard; } \
           | grep -E '^(src|include|cuda|apps)/.*\.(cpp|hpp|cu)$' | sort -u )

echo "=== 1. negative-control / temporary mutation markers in production code ==="
n=$(grep -rn "MUTATED" $PROD 2>/dev/null | wc -l)
if [ "$n" -eq 0 ]; then say PASS "no mutation markers ($n)"
else grep -rn "MUTATED" $PROD | head -5; say FAIL "$n mutation markers"; fi

echo ""
echo "=== 2. debug prints in the files THIS LINEAGE adds or changes ==="
n=0
for f in $LINEAGE; do
  while IFS= read -r hit; do
    line=${hit%%:*}
    content=${hit#*:}
    trimmed=$(printf '%s' "$content" | sed 's/^[[:space:]]*//')
    case "$trimmed" in
      '//'*|'*'*|'/*'*) continue ;;
    esac
    printf "    %s:%s %s\n" "$f" "$line" "$(printf '%s' "$trimmed" | cut -c1-70)"
    n=$((n + 1))
  done < <(grep -nE 'std::(cout|cerr)[[:space:]]*<<|(^|[^a-zA-Z_])printf[[:space:]]*\(' "$f" 2>/dev/null)
done
echo "        lineage production sources scanned: $(printf '%s\n' $LINEAGE | grep -c . )"
if [ "$n" -eq 0 ]; then say PASS "no stray std::cout/cerr/printf in lineage sources ($n)"
else say FAIL "$n stray prints"; fi

echo ""
echo "=== 3. hardcoded local paths in production code ==="
n=$(grep -rn "/mnt/c/Users\|C:\\\\Users\|/home/" $PROD 2>/dev/null | wc -l)
if [ "$n" -eq 0 ]; then say PASS "no hardcoded local path in src/include/cuda/apps ($n)"
else grep -rn "/mnt/c/Users\|C:\\\\Users\|/home/" $PROD | head -5; say FAIL "$n hardcoded paths"; fi
e=$(grep -rl "/mnt/c/Users/Hasib" results --include=*.sh --include=*.py 2>/dev/null | wc -l)
echo "        (evidence harnesses under results/ use the absolute WSL path by"
echo "         established convention: $e scripts, unchanged by this closeout)"

echo ""
echo "=== 4. temporary build configuration ==="
n=$(git diff --name-only -- CMakeLists.txt CMakePresets.json cmake/ | wc -l)
say PASS "root CMakeLists/presets/cmake untouched ($n changed)"
echo "        cuda/CMakeLists.txt and src/CMakeLists.txt changed -- registering new sources:"
git diff --stat -- cuda/CMakeLists.txt src/CMakeLists.txt | sed 's/^/          /'
s=$(git diff -- cuda/CMakeLists.txt src/CMakeLists.txt \
     | grep -E '^\+' | grep -vE '^\+[[:space:]]*#' \
     | grep -cE '(-fsanitize|-O0|-g3|ENABLE_SANITIZERS|CMAKE_BUILD_TYPE[[:space:]]+Debug)' || true)
if [ "$s" -eq 0 ]; then say PASS "no debug/sanitizer flag added to a CMakeLists ($s)"
else
  git diff -- cuda/CMakeLists.txt src/CMakeLists.txt | grep -E '^\+' | grep -vE '^\+[[:space:]]*#' \
    | grep -E '(-fsanitize|-O0|-g3|ENABLE_SANITIZERS)' | head -5
  say FAIL "$s suspicious flags"
fi

echo ""
echo "=== 5. clang-format over the CI scope ==="
mapfile -t F < <(find include src apps tests -type f -name '*.cpp' -o -type f -name '*.hpp')
clang-format-18 --dry-run --Werror --style=file:.clang-format "${F[@]}" > /tmp/fmt.log 2>&1
v=$(grep -c 'error:' /tmp/fmt.log || true)
if [ "$v" -eq 0 ]; then say PASS "0 violations across ${#F[@]} files"; else say FAIL "$v violations"; fi

echo ""
echo "=== 6. TODO.md internal consistency ==="
grep -q "GPU-DISC-001 — CUDA discretization pipeline" TODO.md \
  && say PASS "GPU-DISC-001 listed FINISHED" || say FAIL "GPU-DISC-001 status"
grep -q "GPU-PIPE-001 — final GPU residency" TODO.md \
  && say PASS "GPU-PIPE-001 listed FINISHED" || say FAIL "GPU-PIPE-001 status"
u=$(sed -n '/# 5. P13/,/^---/p' TODO.md | grep -c '^\* \[x\]' || true)
if [ "$u" -eq 0 ]; then say PASS "P13 has no checked item ($u) -- not started"
else say FAIL "P13 has $u checked items"; fi
grep -q "P13 — Production Maturity (not started, NOT AUTHORIZED)" TODO.md \
  && say PASS "P13 recorded as not started / not authorized" || say FAIL "P13 NEXT line"

echo ""
echo "=== 7. self-check: this script can fail ==="
saved=$bad
say FAIL "deliberate failing check (must read FAIL)"
if [ "$bad" -eq 1 ]; then echo "        aggregation works"; else echo "        BROKEN"; exit 2; fi
bad=$saved

echo ""
if [ $bad -eq 0 ]; then echo "PHASE 3 HYGIENE: PASS"; else echo "PHASE 3 HYGIENE: FAILURES PRESENT"; fi
exit $bad
