#!/usr/bin/env bash
# P17.11 section 35 -- attribute the NON-TIMING changes in results/validation/mms/.
#
# The runtime-noise audit, run after the three-configuration regression, found l1/l2/linf ERROR
# NORMS changed in distorted_mesh_mms.json -- not a timing key. Every previous P17 phase saw
# timing-only changes, and every previous P17 phase ran ONLY Release configurations. P17.11 is the
# first to run Debug+GUI. The hypothesis is therefore that a Debug build writes slightly different
# norms than a Release build.
#
# This tests that hypothesis by running the SAME test from each tree in turn and diffing what it
# writes, rather than asserting the explanation.
set -uo pipefail
cd /mnt/c/Users/Hasib/Desktop/CFDAPP/CFDApp || exit 1
Q=results/p17/qualification
F=results/validation/mms/distorted_mesh_mms.json

echo "=== baseline: the tracked file is clean before we start ==="
git checkout -- results/validation 2>/dev/null
echo "  modified under results/validation: $(git status --porcelain -- results/validation | wc -l)"
echo "  tracked $F sha256: $(sha256sum $F | awk '{print $1}')"
echo ""

probe() {  # probe <label> <tree>
  local label="$1" tree="$2"
  local bin="$tree/tests/integration/mms/CFDMMSValidationTests"
  if [ ! -x "$bin" ]; then echo "  $label: binary absent ($bin)"; return; fi
  git checkout -- results/validation 2>/dev/null
  OMP_NUM_THREADS=1 "$bin" --gtest_filter='*DistortedMesh*' > "$Q/logs/85_mms_$label.log" 2>&1
  local rc=$?
  local changed
  changed=$(git status --porcelain -- results/validation | wc -l)
  echo "  $label: test exit=$rc, files changed=$changed, sha256=$(sha256sum $F | awk '{print $1}')"
  # classify what changed
  git diff -U0 -- "$F" | grep -E '^[+-]' | grep -vE '^(\+\+\+|---)' \
    | sed -E 's/^[+-][[:space:]]*//; s/^"([^"]+)".*/\1/' | sort | uniq -c | sed 's/^/      /'
  cp "$F" "$Q/logs/85_mms_${label}_written.json" 2>/dev/null
}

echo "=== the same MMS test, run from each configuration's own binary ==="
probe release_cuda build/qual-final
probe cpu_only     build/qual-cpuonly
probe debug_gui    build/qual-debug
echo ""

echo "=== do the Release and Debug binaries write DIFFERENT norms? ==="
if [ -f "$Q/logs/85_mms_release_cuda_written.json" ] && [ -f "$Q/logs/85_mms_debug_gui_written.json" ]; then
  if diff -q "$Q/logs/85_mms_release_cuda_written.json" "$Q/logs/85_mms_debug_gui_written.json" >/dev/null; then
    echo "  IDENTICAL -- so Debug is NOT the explanation and this needs another cause."
  else
    echo "  DIFFERENT -- Debug and Release write different values. Sample:"
    diff "$Q/logs/85_mms_release_cuda_written.json" "$Q/logs/85_mms_debug_gui_written.json" \
      | grep -E '^[<>]' | head -8 | sed 's/^/    /'
    echo "  differing lines: $(diff "$Q/logs/85_mms_release_cuda_written.json" "$Q/logs/85_mms_debug_gui_written.json" | grep -c '^[<>]')"
  fi
fi
echo ""
echo "=== restore ==="
git checkout -- results/validation 2>/dev/null
echo "  modified under results/validation: $(git status --porcelain -- results/validation | wc -l)"
