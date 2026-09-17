#!/usr/bin/env bash
# Compare each control tree's W8 inputs with the repo's pre-amendment state.
R=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
casehash() { (cd "$1" && find "$2" -type f ! -path '*/results/*' -print0 | sort -z | xargs -0 sha256sum | sha256sum | cut -d' ' -f1); }
echo "repo (pre-amendment frozen): structured d20c8443..., multiblock bcbbadb7..."
for c in cases/curved_channel_multiblock cases/poiseuille_distorted; do
  [ -d "$R/$c" ] || { echo "repo lacks $c"; continue; }
  echo "repo  $c  $(casehash "$R" "$c")"
done
ls "$R/cases" | grep -iE "poiseuille|curved"
for t in /root/uf001_ctrl_signflip /root/uf001_ctrl_farx2 /root/uf001_baseline; do
  echo "== $t"
  sha256sum "$t/tests/integration/case/test_structured_quad_production_case.cpp" \
            "$t/tests/integration/case/test_multiblock_production_case.cpp" | cut -c1-16
  for c in cases/curved_channel_multiblock cases/poiseuille_distorted; do
    [ -d "$t/$c" ] && echo "tree  $c  $(casehash "$t" "$c")" || echo "tree lacks $c"
  done
  # helper sources the two tests compile against
  for f in tests/support/CaseFixtureCopy.hpp tests/support/CaseFixtureCopy.cpp tests/integration/case/CMakeLists.txt \
           include/cfd/validation/GridConvergenceStudy.hpp src/validation/GridConvergenceStudy.cpp; do
    if [ -f "$t/$f" ]; then
      a=$(sha256sum "$R/$f" 2>/dev/null | cut -c1-16); b=$(sha256sum "$t/$f" | cut -c1-16)
      [ "$a" = "$b" ] && echo "  same  $f" || echo "  DIFF  $f  repo $a tree $b"
    else echo "  missing in tree: $f"; fi
  done
done
