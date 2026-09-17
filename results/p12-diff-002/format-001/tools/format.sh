#!/usr/bin/env bash
# P12-DIFF-002-FORMAT-001: make the tree clang-format-18 clean (W10 and CI's format job) with a
# PROOF that nothing but layout changed:
#   (1) every reformatted file's non-whitespace character stream is identical before and after;
#   (2) the Release libcfdcore.a rebuilt after formatting is bit-identical to the frozen library
#       (143a1dda..., deterministic archives), so no production object code changed.
set -u
R=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
P=$R/results/p12-diff-002/format-001
mkdir -p "$P/logs" "$P/data/before"
LOG=$P/logs/01_format.log
cd "$R" || exit 1
nows() { tr -d ' \t\r\n\v\f' < "$1" | sha256sum | cut -d' ' -f1; }
{
  echo "# FORMAT-001 $(date -u +%Y-%m-%dT%H:%M:%SZ) git HEAD $(git rev-parse HEAD)"
  echo "# clang-format: $(clang-format-18 --version)"
  mapfile -t FILES < <(find include src apps tests \( -name '*.cpp' -o -name '*.hpp' -o -name '*.h' \) -print0 \
      | xargs -0 -n1 sh -c 'clang-format-18 --dry-run "$0" 2>&1 | grep -q warning: && echo "$0"')
  echo "# files needing formatting: ${#FILES[@]}"
  LIB0=$(sha256sum build/release/src/libcfdcore.a | cut -d' ' -f1)
  echo "# libcfdcore.a before: $LIB0"
  BAD=0
  for f in "${FILES[@]}"; do
    mkdir -p "$P/data/before/$(dirname "$f")"; cp "$f" "$P/data/before/$f"
    b_full=$(sha256sum "$f" | cut -d' ' -f1); b_nows=$(nows "$f")
    clang-format-18 -i "$f"
    a_full=$(sha256sum "$f" | cut -d' ' -f1); a_nows=$(nows "$f")
    same=$([ "$b_nows" = "$a_nows" ] && echo SAME || { BAD=1; echo DIFFERENT; })
    echo "$f  full $b_full -> $a_full  non-whitespace $b_nows -> $a_nows  $same"
  done
  echo "# non-whitespace streams: $([ $BAD = 0 ] && echo 'ALL IDENTICAL' || echo 'SOME DIFFER')"
  echo "# remaining violations: $(find include src apps tests \( -name '*.cpp' -o -name '*.hpp' -o -name '*.h' \) -print0 | xargs -0 clang-format-18 --dry-run --Werror 2>&1 | grep -c 'error:')"
  echo "# rebuilding libcfdcore (Release)"
  cmake --build build/release --target cfdcore -j16 > "$P/logs/01_build.txt" 2>&1 || { echo "BUILD FAILED"; exit 1; }
  LIB1=$(sha256sum build/release/src/libcfdcore.a | cut -d' ' -f1)
  echo "# libcfdcore.a after:  $LIB1  -> $([ "$LIB1" = 143a1dda0680bc1d481a0360095ca7c643ffc9e2a2dc53bbd1d55d5a8202132a ] && echo 'BIT-IDENTICAL to the frozen library' || echo 'CHANGED')"
  echo "# src/ + include/ tree after: $(find src include -type f -print0 | sort -z | xargs -0 sha256sum | sha256sum | cut -d' ' -f1)"
} > "$LOG" 2>&1
cat "$LOG"
