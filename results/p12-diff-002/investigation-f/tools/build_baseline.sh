#!/usr/bin/env bash
# P12-DIFF-002-INV-002 / INV-F2: build an ISOLATED pre-DIFF-002 baseline.
#
# INV-F2 permits "an isolated temporary baseline build/worktree" and forbids destructively reverting
# the working tree. So: copy the source (not build/, not results/) into a scratch directory outside
# the repo, remove ONLY the DIFF-002 reconstruction block from the copy's
# boundaryFaceDiffusionTerms, and configure + build there. Everything else -- GRAD-002, A2's
# activation change, every test, every case -- is byte-identical to the authoritative tree, so the
# difference between the two builds is DIFF-002's boundary reconstruction and nothing else.
#
# The authoritative tree is never written to. Verified by hashing production before and after.
set -u
R=/mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp
S=$HOME/invf_baseline/src
B=$HOME/invf_baseline/build
LOG=$R/results/p12-diff-002/investigation-f/logs/02_baseline_build.log
mkdir -p "$S" "$B" "$(dirname "$LOG")"

{
  echo "# INV-F2 isolated pre-DIFF-002 baseline build; $(date -u +%Y-%m-%dT%H:%M:%SZ)"
  echo "# authoritative tree (read-only here): $R"
  echo "# isolated copy: $S"
  echo

  rsync -a --delete \
    --exclude 'build/' --exclude 'results/' --exclude '.git/' --exclude '*.o' \
    "$R/" "$S/" 2>&1 | tail -2
  echo "copied source tree"

  F=$S/src/discretization/NonOrthogonalDiffusion.cpp
  echo "before patch: $(sha256sum "$F" | cut -c1-16)  (authoritative $(sha256sum "$R/src/discretization/NonOrthogonalDiffusion.cpp" | cut -c1-16))"

  # Remove the DIFF-002 stencil block: from the `const auto stencil = ...` line through the closing
  # brace of `if (stencil.valid) { ... return terms; }`. python does the edit on the COPY only.
  python3 - "$F" <<'PY'
import re, sys
p = sys.argv[1]
s = open(p, encoding='utf-8').read()
start = s.index('    const auto stencil = MeshGeometry::boundaryInwardStencil(mesh, face);')
end = s.index('    // No usable inward stencil', start)
s = s[:start] + ('    // INV-F2 BASELINE COPY ONLY: the P12-DIFF-002 reconstruction block is removed\n'
                 '    // here so this isolated build reproduces the pre-DIFF-002 boundary treatment.\n') + s[end:]
open(p, 'w', encoding='utf-8', newline='\n').write(s)
print('  removed the DIFF-002 reconstruction block from the COPY')
PY
  echo "after patch:  $(sha256sum "$F" | cut -c1-16)"
  echo "authoritative file unchanged: $(sha256sum "$R/src/discretization/NonOrthogonalDiffusion.cpp" | cut -c1-16)"
  echo

  echo "## configure + build the baseline"
  if cmake -S "$S" -B "$B" -DCMAKE_BUILD_TYPE=Release > "$B/configure.log" 2>&1; then
    echo "  configure OK"
  else
    echo "  configure FAILED"; tail -20 "$B/configure.log"; exit 1
  fi
  if cmake --build "$B" -j"$(nproc)" > "$B/build.log" 2>&1; then
    echo "  build OK"
    echo "  baseline library $(sha256sum "$B/src/libcfdcore.a" | cut -c1-16)"
    echo "  current  library $(sha256sum "$R/build/release/src/libcfdcore.a" | cut -c1-16)"
  else
    echo "  build FAILED"; grep -E 'error' "$B/build.log" | head -20; exit 1
  fi
} > "$LOG" 2>&1
cat "$LOG"
