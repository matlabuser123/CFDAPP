#!/usr/bin/env bash
# Verify the tree after the W8-amendment STOP against logs/00_freeze.log (attempt 2) and the
# DIFF-002 lineage gate hashes. Every expected value is read from a frozen log, not retyped.
R=/c/Users/Hasib/Desktop/CFDAPP/CFDApp
cd "$R" || exit 1
echo "# W8 amendment STOP-state verification; $(date -u +%Y-%m-%dT%H:%M:%SZ)"
echo
echo "## against w8a/logs/00_freeze.log (18 entries)"
bad=0; n=0
while read -r want star; do
  f=${star#\*}
  n=$((n+1))
  got=$(sha256sum "$f" | cut -d' ' -f1)
  if [ "$got" = "$want" ]; then echo "  OK        $f"; else echo "  CHANGED   $f"; bad=$((bad+1)); fi
done < <(grep -E '^[0-9a-f]{64} \*' results/p12-diff-002/w8a/logs/00_freeze.log)
echo "  checked $n, changed $bad"
echo "  (acceptance_gate.md is expected to be unchanged too: the stop is recorded in summary.md, not in the gate)"
echo
echo "## DIFF-002 lineage + other frozen gates (expected values from w8-inv-001/logs/00_freeze.log)"
bad2=0; n2=0
while read -r want f; do
  case "$f" in results/*acceptance_gate*.md) ;; *) continue ;; esac
  n2=$((n2+1))
  got=$(sha256sum "$f" 2>/dev/null | cut -d' ' -f1)
  if [ "$got" = "$want" ]; then echo "  OK        $f"; else echo "  CHANGED   $f"; bad2=$((bad2+1)); fi
done < <(grep -E '^[0-9a-f]{64}  results/' results/p12-diff-002/w8-inv-001/logs/00_freeze.log)
echo "  checked $n2, changed $bad2"
echo
echo "## the four earlier P12 gates (full hashes as recorded in their own phases' freeze logs)"
while read -r want f; do
  got=$(sha256sum "$f" | cut -d' ' -f1)
  [ "$got" = "$want" ] && echo "  OK        $f" || echo "  CHANGED   $f"
done <<'LIST'
2c45e4381bb18099117ce9a4e69cfcbac4b6b9fbb7f284833a052dfcdbeb9f91 results/p12-grad-001/acceptance_gate.md
a46973ed5ba4190a008a1c3f3138f21612a6b1ca0f11ca32deb6d0899602d2eb results/p12-grad-002/acceptance_gate.md
353b72ef11345922c849af0a456e2c8082e92188a8a10647b7c9fde2e249f186 results/p12-grad-002/acceptance_gate_A1.md
5b45fed9c473b3b4eca923fa2e4b8ce064022270b67259fb23caf0dbdae5faa8 results/p12-mesh-007/acceptance_gate.md
LIST
echo
echo "## attempted (reverted) StructuredQuad edit, preserved"
sha256sum results/p12-diff-002/w8a/data/test_structured_quad_production_case.cpp.attempted results/p12-diff-002/w8a/data/attempted_structured_quad.patch
echo
echo "## W8 report files (no run happened, so they must equal the pre-amendment snapshot)"
for f in poiseuille_distorted_grid_convergence.json curved_channel_multiblock_grid_convergence.json; do
  a=$(sha256sum "results/validation/production/$f" | cut -d' ' -f1)
  b=$(sha256sum "results/p12-diff-002/w8a/data/reports_before/$f" | cut -d' ' -f1)
  [ "$a" = "$b" ] && echo "  unchanged $f" || echo "  CHANGED   $f"
done
echo
echo "## git"
echo "  HEAD $(git rev-parse HEAD)"
echo "  git diff --check: $(git diff --check >/dev/null 2>&1 && echo clean || echo ISSUES)"
echo "  porcelain: $(git status --porcelain | grep -c '^ M\|^M ') modified, $(git status --porcelain | grep -c '^??') untracked"
